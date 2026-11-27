// main.c - Snap3DS: 3DS camera, screens, buttons and touch
#include <3ds.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "art.h"

#define FRAME_BYTES (CW * CH * 2)
#define PHOTO_DIR "sdmc:/3ds/Snap3DS"

// Three camera pictures: one being filled, one just finished, one being shown.
static u16 *bufs[3];
static int backBuf = 0, readyBuf = 1, frontBuf = 2;
static bool freshFrame;
static LightLock frameLock;
static Thread camThread;
static volatile bool camQuit;
static u16 photo[CW * CH];           // mirrored picture + filter
static u16 ui[CW * CH];              // bottom screen
static Handle evError;
static u32 transferBytes;
static bool camRunning, camReady;
static Result camError;
static aptHookCookie hookCookie;

/* ---------------- camera ---------------- */

#define TRY(call) do { Result r_ = (call); if (R_FAILED(r_)) { camError = r_; return false; } } while (0)

static bool cam_setup(void) {
    TRY(camInit());
    TRY(CAMU_SetSize(SELECT_IN1, SIZE_QVGA, CONTEXT_A));
    TRY(CAMU_SetOutputFormat(SELECT_IN1, OUTPUT_RGB_565, CONTEXT_A));
    TRY(CAMU_SetFrameRate(SELECT_IN1, FRAME_RATE_30));
    TRY(CAMU_SetNoiseFilter(SELECT_IN1, true));
    TRY(CAMU_SetAutoExposure(SELECT_IN1, true));
    TRY(CAMU_SetAutoWhiteBalance(SELECT_IN1, true));
    TRY(CAMU_SetTrimming(PORT_CAM1, false));
    TRY(CAMU_GetMaxBytes(&transferBytes, CW, CH));
    TRY(CAMU_SetTransferBytes(PORT_CAM1, transferBytes, CW, CH));
    TRY(CAMU_GetBufferErrorInterruptEvent(&evError, PORT_CAM1));
    return true;
}

// Runs on its own thread so the camera never has to wait for drawing.
static void cam_thread(void *arg) {
    (void)arg;
    Handle evFrame = 0;
    bool needStart = true;
    int timeouts = 0;

    while (!camQuit) {
        if (!evFrame &&
            R_FAILED(CAMU_SetReceiving(&evFrame, bufs[backBuf], PORT_CAM1,
                                       FRAME_BYTES, (s16)transferBytes))) {
            evFrame = 0;
            svcSleepThread(10000000LL);
            continue;
        }
        if (needStart) {             // receive first, then start capture
            CAMU_StartCapture(PORT_CAM1);
            needStart = false;
        }
        Handle events[2] = { evError, evFrame };
        s32 which = -1;
        Result r = svcWaitSynchronizationN(&which, events, 2, false, 100000000LL);
        if (R_FAILED(r) || R_DESCRIPTION(r) == RD_TIMEOUT) {
            if (++timeouts < 10) continue;
            which = 0;               // stuck for 1 second: restart
            CAMU_StopCapture(PORT_CAM1);
        }
        timeouts = 0;
        svcCloseHandle(evFrame);
        evFrame = 0;
        if (which == 1) {            // full picture: pass it on
            LightLock_Lock(&frameLock);
            int t = readyBuf; readyBuf = backBuf; backBuf = t;
            freshFrame = true;
            LightLock_Unlock(&frameLock);
        } else {                     // broken picture: throw it away
            CAMU_ClearBuffer(PORT_CAM1);
            needStart = true;
        }
    }
    CAMU_StopCapture(PORT_CAM1);
    if (evFrame) svcCloseHandle(evFrame);
}

static void cam_start(void) {
    if (!camReady || camRunning) return;
    CAMU_Activate(SELECT_IN1);
    CAMU_ClearBuffer(PORT_CAM1);
    camQuit = false;
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    camThread = threadCreate(cam_thread, NULL, 8 * 1024, priority - 1, -2, false);
    if (!camThread) {
        CAMU_Activate(SELECT_NONE);
        return;
    }
    camRunning = true;
}

static void cam_stop(void) {
    if (!camRunning) return;
    camQuit = true;
    threadJoin(camThread, U64_MAX);
    threadFree(camThread);
    CAMU_Activate(SELECT_NONE);
    camRunning = false;
}

// Returns the newest picture, or NULL if there is no new one.
static const u16 *cam_poll(void) {
    const u16 *picture = NULL;
    LightLock_Lock(&frameLock);
    if (freshFrame) {
        int t = frontBuf; frontBuf = readyBuf; readyBuf = t;
        freshFrame = false;
        picture = bufs[frontBuf];
    }
    LightLock_Unlock(&frameLock);
    return picture;
}

// Stop the camera for HOME menu and lid close, restart afterwards.
static void apt_event(APT_HookType type, void *param) {
    (void)param;
    if (type == APTHOOK_ONSUSPEND || type == APTHOOK_ONSLEEP) cam_stop();
    if (type == APTHOOK_ONRESTORE || type == APTHOOK_ONWAKEUP) cam_start();
}

/* ---------------- screens ---------------- */

// 3DS framebuffers are rotated 90 degrees and use blue, green, red order.
static void blit(gfxScreen_t screen, const u16 *img) {
    u16 fbHeight, fbWidth;           // reported rotated: width=240
    u8 *fb = gfxGetFramebuffer(screen, GFX_LEFT, &fbHeight, &fbWidth);
    int left = (fbWidth - CW) / 2;   // top screen is 400 wide: black bars
    memset(fb, 0, (size_t)fbWidth * fbHeight * 3);
    for (int x = 0; x < CW; x++) {
        u8 *out = fb + (x + left) * CH * 3;
        for (int y = CH - 1; y >= 0; y--) {
            u16 p = img[y * CW + x];
            *out++ = (p & 31) << 3;
            *out++ = ((p >> 5) & 63) << 2;
            *out++ = (p >> 11) << 3;
        }
    }
}

/* The filter carousel. "scroll" counts in filters and has no limit, so the
   list goes round forever. The filter in the middle is the one in use.     */
#define CAR_Y 166
#define CAR_STEP 74

typedef struct {
    float scroll;        // what is on screen now
    int target;          // where it is heading
    bool touching, dragged;
    int startX, startY, lastX;
    float startScroll, speed;
} Carousel;

static int wrap(int i) { return ((i % F_COUNT) + F_COUNT) % F_COUNT; }

static int nearest(float v) { return (int)(v < 0 ? v - 0.5f : v + 0.5f); }

// Call once per frame. Returns true when the middle (shutter) button is tapped.
static bool carousel_update(Carousel *c, bool down, int x, int y) {
    bool shutter = false;
    if (down) {
        if (!c->touching) {
            c->touching = true;
            c->dragged = false;
            c->startX = c->lastX = x;
            c->startY = y;
            c->startScroll = c->scroll;
            c->speed = 0;
        }
        if (abs(x - c->startX) > 8) c->dragged = true;
        c->speed = c->speed * 0.5f + (c->lastX - x) * 0.5f;
        c->lastX = x;
        if (c->dragged) {            // circles follow the finger
            c->scroll = c->startScroll + (float)(c->startX - x) / CAR_STEP;
            c->target = nearest(c->scroll);
        }
    } else if (c->touching) {
        c->touching = false;
        if (c->dragged) {            // a quick flick carries further
            float fling = c->speed * 5.0f / CAR_STEP;
            if (fling > 2) fling = 2;
            if (fling < -2) fling = -2;
            c->target = nearest(c->scroll + fling);
        } else if (abs(c->startY - CAR_Y) <= 40) {
            int dx = c->startX - CW / 2;
            if (abs(dx) <= 36) {
                shutter = true;
            } else {                 // tapped a side circle: bring it over
                c->target = nearest(c->scroll + (float)dx / CAR_STEP);
            }
        }
    }
    if (!c->touching || !c->dragged) // glide into place
        c->scroll += (c->target - c->scroll) * 0.25f;
    return shutter;
}

static void draw_ui(const Carousel *car, int filter, bool autoTrack, bool found,
                    bool mirror, const char *message) {
    draw_rect(ui, 0, 0, CW, CH, 0x15151C);
    draw_rect(ui, 0, 0, CW, 34, 0xFFD60A);
    draw_text(ui, (CW - text_width(FILTER_NAMES[filter], 3)) / 2, 7, 3,
              FILTER_NAMES[filter], 0x15151C);

    const char *state = !autoTrack ? "LOCKED - MANUAL" :
                        found ? "AUTO: TRACKING" : "AUTO: SEARCHING";
    draw_text(ui, (CW - text_width(state, 1)) / 2, 44, 1, state,
              !autoTrack ? 0xFF9F0A : found ? 0x30D158 : 0xAAAAAA);

    draw_text(ui, 10, 62, 1, "SWIPE OR L/R: CHANGE FILTER", 0xFFFFFF);
    draw_text(ui, 10, 76, 1, "TAP MIDDLE CIRCLE OR X: PHOTO", 0xFFFFFF);
    draw_text(ui, 10, 90, 1, "A: ACTION    Y: AUTO / LOCK    B: RESET", 0xFFFFFF);
    draw_text(ui, 10, 104, 1, "CIRCLE PAD: MOVE   DPAD UP/DOWN: SIZE", 0xFFFFFF);
    draw_text(ui, 10, 118, 1, mirror ? "SELECT: MIRROR ON" : "SELECT: MIRROR OFF",
              0xFFFFFF);

    // white shutter ring in the middle
    draw_ellipse(ui, CW / 2, CAR_Y, 38, 38, 0, 0xFFFFFF, 255);
    draw_ellipse(ui, CW / 2, CAR_Y, 33, 33, 0, 0x15151C, 255);

    int centre = nearest(car->scroll);
    for (int i = centre - 3; i <= centre + 3; i++) {
        float x = CW / 2 + (i - car->scroll) * CAR_STEP;
        if (x < -30 || x > CW + 30) continue;
        float away = (x - CW / 2) / CAR_STEP;
        if (away < 0) away = -away;
        float radius = away < 1 ? 30 - 4 * away : 26; // bigger in the middle
        draw_ellipse(ui, x, CAR_Y, radius, radius, 0, 0x3A3A48, 255);
        filter_icon(ui, wrap(i), nearest(x), CAR_Y);
    }

    draw_text(ui, 10, 224, 1, message[0] ? message : "START: EXIT", 0xFFD60A);
}

/* ---------------- photos ---------------- */

static bool take_photo(char *msg, size_t len) {
    mkdir("sdmc:/3ds", 0777);        // fine if they already exist
    mkdir(PHOTO_DIR, 0777);

    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    char stamp[32] = "SNAP";
    if (tmv) strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", tmv);

    char name[48], path[128];
    for (int n = 0; n < 100; n++) {  // never overwrite an older photo
        snprintf(name, sizeof(name), "%s_%02d.bmp", stamp, n);
        snprintf(path, sizeof(path), "%s/%s", PHOTO_DIR, name);
        if (access(path, F_OK) != 0) {
            bool ok = save_bmp(path, photo);
            snprintf(msg, len, ok ? "SAVED %s" : "SAVE FAILED - CHECK SD CARD", name);
            return ok;
        }
    }
    snprintf(msg, len, "SAVE FAILED - TRY AGAIN");
    return false;
}

/* ---------------- main ---------------- */

int main(void) {
    gfxInitDefault();
    osSetSpeedupEnable(true);        // use the New 3DS faster CPU

    LightLock_Init(&frameLock);
    bool haveMemory = true;
    for (int i = 0; i < 3; i++) {
        bufs[i] = (u16 *)memalign(0x40, FRAME_BYTES);
        if (bufs[i]) memset(bufs[i], 0, FRAME_BYTES);
        else haveMemory = false;
    }
    if (haveMemory) camReady = cam_setup();
    aptHook(&hookCookie, apt_event, NULL);
    cam_start();

    Tracker tracker;
    tracker_reset(&tracker);
    int filter = F_DOG;
    bool autoTrack = true, mirror = true, havePicture = false;
    Carousel carousel = {0};
    carousel.scroll = carousel.target = F_DOG;
    float offX = 0, offY = 0, scale = 1.0f;
    Face face = tracker.face;
    char message[64] = "";
    u64 messageUntil = 0, bannerUntil = 0, flashUntil = 0;
    touchPosition touch = {0, 0};
    static u16 clean[CW * CH], shown[CW * CH];

    if (!camReady)
        snprintf(message, sizeof(message), "CAMERA ERROR %08lX", (unsigned long)camError);

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown(), held = hidKeysHeld();
        u64 now = osGetTime();
        int previous = filter;

        if (down & KEY_START) break;
        if (down & KEY_R) carousel.target++;
        if (down & KEY_L) carousel.target--;

        // touch: swipe the circles, tap one to pick it, tap the middle for a photo
        if (held & KEY_TOUCH) hidTouchRead(&touch);
        bool tapShutter = carousel_update(&carousel, (held & KEY_TOUCH) != 0,
                                          touch.px, touch.py);
        filter = wrap(carousel.target);
        if (filter != previous) bannerUntil = now + 1200;

        if (down & KEY_Y) {
            autoTrack = !autoTrack;  // locking keeps the filter where it is
        }
        if (down & KEY_B) {
            offX = offY = 0; scale = 1.0f; autoTrack = true;
            tracker_reset(&tracker);
            face = tracker.face;
        }
        if (down & KEY_SELECT) mirror = !mirror;

        circlePosition pad;
        hidCircleRead(&pad);
        if (abs(pad.dx) > 25) offX += pad.dx / 40.0f;
        if (abs(pad.dy) > 25) offY -= pad.dy / 40.0f;
        if (held & KEY_DUP) scale *= 1.02f;
        if (held & KEY_DDOWN) scale /= 1.02f;
        if (offX < -200) offX = -200;
        if (offX > 200) offX = 200;
        if (offY < -150) offY = -150;
        if (offY > 150) offY = 150;
        if (scale < 0.4f) scale = 0.4f;
        if (scale > 2.2f) scale = 2.2f;

        const u16 *picture = cam_poll();
        if (picture) {
            for (int y = 0; y < CH; y++)
                for (int x = 0; x < CW; x++)
                    clean[y * CW + x] = picture[y * CW + (mirror ? CW - 1 - x : x)];
            havePicture = true;
            if (autoTrack) {
                tracker_update(&tracker, clean);
                face = tracker.face;
            }
        }

        Face placed = { face.x + offX, face.y + offY, face.w * scale };
        memcpy(photo, clean, sizeof(photo));
        filter_draw(photo, filter, placed, (held & KEY_A) != 0, (now % 600000) / 1000.0f);

        if ((down & KEY_X) || tapShutter) {
            if (havePicture) {
                if (take_photo(message, sizeof(message))) {
                    CAMU_PlayShutterSound(SHUTTER_SOUND_TYPE_NORMAL);
                    flashUntil = osGetTime() + 120;
                }
            } else {
                snprintf(message, sizeof(message), "NO CAMERA PICTURE YET");
            }
            messageUntil = osGetTime() + 3000;
        }
        if (camReady && now > messageUntil && messageUntil) { message[0] = 0; messageUntil = 0; }

        // top screen: photo, plus extras that are not saved in the photo
        memcpy(shown, photo, sizeof(shown));
        if (now < bannerUntil) {
            int w = text_width(FILTER_NAMES[filter], 2);
            draw_rect(shown, (CW - w) / 2 - 8, 6, w + 16, 24, 0x000000);
            draw_text(shown, (CW - w) / 2, 11, 2, FILTER_NAMES[filter], 0xFFFFFF);
        }
        if (osGetTime() < flashUntil) draw_rect(shown, 0, 0, CW, CH, 0xFFFFFF);

        draw_ui(&carousel, filter, autoTrack, tracker.found, mirror, message);

        blit(GFX_TOP, shown);
        blit(GFX_BOTTOM, ui);
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    aptUnhook(&hookCookie);
    cam_stop();
    if (evError) svcCloseHandle(evError);
    camExit();
    for (int i = 0; i < 3; i++) free(bufs[i]);
    gfxExit();
    return 0;
}
