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

static u16 *camBuf;                  // filled by the camera
static u16 photo[CW * CH];           // mirrored picture + filter
static u16 ui[CW * CH];              // bottom screen
static Handle evError, evFrame;
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

static void cam_start(void) {
    if (!camReady || camRunning) return;
    CAMU_Activate(SELECT_IN1);
    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_StartCapture(PORT_CAM1);
    camRunning = true;
}

static void cam_stop(void) {
    if (!camRunning) return;
    CAMU_StopCapture(PORT_CAM1);
    if (evFrame) { svcCloseHandle(evFrame); evFrame = 0; }
    CAMU_Activate(SELECT_NONE);
    camRunning = false;
}

// Returns true when a new picture is in camBuf.
static bool cam_poll(void) {
    if (!camRunning) return false;
    if (!evFrame &&
        R_FAILED(CAMU_SetReceiving(&evFrame, camBuf, PORT_CAM1, FRAME_BYTES, (s16)transferBytes))) {
        evFrame = 0;
        return false;
    }
    Handle events[2] = { evError, evFrame };
    s32 which = -1;
    Result r = svcWaitSynchronizationN(&which, events, 2, false, 100000000LL); // 0.1 s
    if (R_FAILED(r) || R_DESCRIPTION(r) == RD_TIMEOUT) return false;
    svcCloseHandle(evFrame);
    evFrame = 0;
    if (which == 0) {                // buffer error: restart capture
        CAMU_ClearBuffer(PORT_CAM1);
        CAMU_StartCapture(PORT_CAM1);
        return false;
    }
    return which == 1;
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

#define BTN_Y 112
#define BTN_STEP 63
#define BTN_X(i) (34 + (i) * BTN_STEP)

static void draw_ui(int filter, float ringX, bool autoTrack, bool found,
                    bool mirror, const char *message) {
    draw_rect(ui, 0, 0, CW, CH, 0x15151C);
    draw_rect(ui, 0, 0, CW, 38, 0xFFD60A);
    draw_text(ui, (CW - text_width(FILTER_NAMES[filter], 3)) / 2, 9, 3,
              FILTER_NAMES[filter], 0x15151C);

    const char *state = !autoTrack ? "LOCKED - MANUAL" :
                        found ? "AUTO: TRACKING" : "AUTO: SEARCHING";
    draw_text(ui, (CW - text_width(state, 1)) / 2, 51, 1, state,
              !autoTrack ? 0xFF9F0A : found ? 0x30D158 : 0xAAAAAA);

    draw_ellipse(ui, ringX, BTN_Y, 31, 31, 0, 0xFFFFFF, 255);
    for (int i = 0; i < F_COUNT; i++) {
        draw_ellipse(ui, BTN_X(i), BTN_Y, 27, 27, 0, 0x3A3A48, 255);
        filter_icon(ui, i, BTN_X(i), BTN_Y);
    }

    draw_text(ui, 10, 158, 1, "L/R: SWIPE    TAP: PICK    A: ACTION", 0xFFFFFF);
    draw_text(ui, 10, 173, 1, "X: TAKE PHOTO    Y: AUTO / LOCK", 0xFFFFFF);
    draw_text(ui, 10, 188, 1, "CIRCLE PAD: MOVE   DPAD UP/DOWN: SIZE", 0xFFFFFF);
    draw_text(ui, 10, 203, 1, mirror ? "B: RESET   SELECT: MIRROR ON" :
                              "B: RESET   SELECT: MIRROR OFF", 0xFFFFFF);
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

    camBuf = (u16 *)memalign(0x40, FRAME_BYTES);
    if (camBuf) {
        memset(camBuf, 0, FRAME_BYTES);
        camReady = cam_setup();
    }
    aptHook(&hookCookie, apt_event, NULL);
    cam_start();

    Tracker tracker;
    tracker_reset(&tracker);
    int filter = F_DOG;
    bool autoTrack = true, mirror = true, havePicture = false;
    float offX = 0, offY = 0, scale = 1.0f, ringX = BTN_X(F_DOG);
    Face face = tracker.face;
    char message[64] = "";
    u64 messageUntil = 0, bannerUntil = 0, flashUntil = 0;
    touchPosition touchStart = {0, 0}, touchLast = {0, 0};
    bool touching = false;
    static u16 clean[CW * CH], shown[CW * CH];

    if (!camReady)
        snprintf(message, sizeof(message), "CAMERA ERROR %08lX", (unsigned long)camError);

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown(), held = hidKeysHeld(), up = hidKeysUp();
        u64 now = osGetTime();
        int previous = filter;

        if (down & KEY_START) break;
        if (down & KEY_R) filter = (filter + 1) % F_COUNT;
        if (down & KEY_L) filter = (filter + F_COUNT - 1) % F_COUNT;

        // touch: tap a circle to pick it, or drag sideways to swipe
        if (held & KEY_TOUCH) {
            hidTouchRead(&touchLast);
            if (!touching) { touchStart = touchLast; touching = true; }
        }
        if ((up & KEY_TOUCH) && touching) {
            int dx = touchLast.px - touchStart.px;
            if (dx > 40) filter = (filter + F_COUNT - 1) % F_COUNT;
            else if (dx < -40) filter = (filter + 1) % F_COUNT;
            else if (abs(touchStart.py - BTN_Y) <= 36)
                for (int i = 0; i < F_COUNT; i++)
                    if (abs(touchStart.px - BTN_X(i)) <= BTN_STEP / 2) filter = i;
            touching = false;
        }
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

        if (cam_poll()) {
            for (int y = 0; y < CH; y++)
                for (int x = 0; x < CW; x++)
                    clean[y * CW + x] = camBuf[y * CW + (mirror ? CW - 1 - x : x)];
            havePicture = true;
            if (autoTrack) {
                tracker_update(&tracker, clean);
                face = tracker.face;
            }
        }

        Face placed = { face.x + offX, face.y + offY, face.w * scale };
        memcpy(photo, clean, sizeof(photo));
        filter_draw(photo, filter, placed, (held & KEY_A) != 0, (now % 600000) / 1000.0f);

        if (down & KEY_X) {
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

        ringX += (BTN_X(filter) - ringX) * 0.35f;
        draw_ui(filter, ringX, autoTrack, tracker.found, mirror, message);

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
    free(camBuf);
    gfxExit();
    return 0;
}
