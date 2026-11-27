// main.c - Snap3DS: 3DS camera, screens, buttons and touch
// Features: face filters, color looks, self-timer, captions, snap score/streak,
//           flip camera, memories gallery, boomerang (saved as animated GIF).
#include <3ds.h>
#include <ctype.h>
#include <dirent.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "art.h"
#include "extra.h"

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
static u32 camSelect = SELECT_IN1;   // which camera: inner (selfie) or outer

/* ---------------- camera ---------------- */

#define TRY(call) do { Result r_ = (call); if (R_FAILED(r_)) { camError = r_; return false; } } while (0)

// Settings that have to be repeated whenever the camera changes.
static bool cam_configure(u32 select) {
    TRY(CAMU_SetSize(select, SIZE_QVGA, CONTEXT_A));
    TRY(CAMU_SetOutputFormat(select, OUTPUT_RGB_565, CONTEXT_A));
    TRY(CAMU_SetFrameRate(select, FRAME_RATE_30));
    TRY(CAMU_SetNoiseFilter(select, true));
    TRY(CAMU_SetAutoExposure(select, true));
    TRY(CAMU_SetAutoWhiteBalance(select, true));
    TRY(CAMU_SetTrimming(PORT_CAM1, false));
    TRY(CAMU_GetMaxBytes(&transferBytes, CW, CH));
    TRY(CAMU_SetTransferBytes(PORT_CAM1, transferBytes, CW, CH));
    return true;
}

static bool cam_setup(void) {
    TRY(camInit());
    if (!cam_configure(camSelect)) return false;
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
    CAMU_Activate(camSelect);
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

// Switch between the inner (selfie) and outer camera.
static bool cam_flip(void) {
    if (!camReady) return false;
    cam_stop();
    u32 next = (camSelect == SELECT_IN1) ? SELECT_OUT1 : SELECT_IN1;
    bool ok = cam_configure(next);
    if (ok) camSelect = next;
    else cam_configure(camSelect);   // go back to the working camera
    LightLock_Lock(&frameLock);
    freshFrame = false;
    LightLock_Unlock(&frameLock);
    cam_start();
    return ok;
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

static int wrap(int i) { return ((i % ALL_FILTERS) + ALL_FILTERS) % ALL_FILTERS; }

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
    draw_text(ui, (CW - text_width(filter_name_any(filter), 3)) / 2, 7, 3,
              filter_name_any(filter), 0x15151C);

    const char *state = !autoTrack ? "LOCKED - MANUAL" :
                        found ? "AUTO: TRACKING" : "AUTO: SEARCHING";
    draw_text(ui, (CW - text_width(state, 1)) / 2, 44, 1, state,
              !autoTrack ? 0xFF9F0A : found ? 0x30D158 : 0xAAAAAA);

    // help lines (10px apart so everything fits above the carousel)
    draw_text(ui, 10, 56, 1, "SWIPE OR L/R: CHANGE FILTER", 0xFFFFFF);
    draw_text(ui, 10, 66, 1, "TAP MIDDLE CIRCLE OR X: PHOTO", 0xFFFFFF);
    draw_text(ui, 10, 76, 1, "A: ACTION    Y: AUTO / LOCK    B: RESET", 0xFFFFFF);
    draw_text(ui, 10, 86, 1, "CIRCLE PAD: MOVE   DPAD UP/DOWN: SIZE", 0xFFFFFF);
    draw_text(ui, 10, 96, 1, mirror ? "SELECT: MIRROR ON" : "SELECT: MIRROR OFF",
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
        filter_icon_any(ui, wrap(i), nearest(x), CAR_Y);
    }

    draw_text(ui, 10, 224, 1, message[0] ? message : "START: EXIT", 0xFFD60A);
}

/* ---------------- touch buttons (bottom row) ---------------- */

#define BTN_Y 205
#define BTN_H 16

enum { M_CAMERA, M_GALLERY, M_BOOM_REC, M_BOOM_PLAY };

static const char *const BTN_CAM[]  = { "FLIP", "MEMORIES", "BOOMERANG" };
static const char *const BTN_REC[]  = { "CANCEL" };
static const char *const BTN_PLAY[] = { "SAVE GIF", "DISCARD" };
static const char *const BTN_GAL[]  = { "<", ">", "DELETE", "BACK" };

static int mode_buttons(int mode) {
    switch (mode) {
    case M_CAMERA: return 3;
    case M_BOOM_REC: return 1;
    case M_BOOM_PLAY: return 2;
    default: return 4;
    }
}

static void draw_buttons(const char *const *labels, int n) {
    int w = CW / n;
    for (int i = 0; i < n; i++) {
        draw_rect(ui, i * w + 2, BTN_Y, w - 4, BTN_H, 0x3A3A48);
        draw_text(ui, i * w + (w - text_width(labels[i], 1)) / 2, BTN_Y + 4, 1,
                  labels[i], 0xFFFFFF);
    }
}

static int button_hit(int x, int n) {
    int i = x * n / CW;
    return i < 0 ? 0 : i >= n ? n - 1 : i;
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

/* ---------------- looks, caption, stats ---------------- */

enum { G_NONE, G_BW, G_SEPIA, G_WARM, G_COOL, G_VINTAGE, G_POP, G_COUNT };
static const char *GRADE_NAMES[G_COUNT] = {
    "NORMAL", "B&W", "SEPIA", "WARM", "COOL", "VINTAGE", "POP"
};
static const int TIMER_SECS[4] = {0, 3, 5, 10};

static int clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static void apply_grade(u16 *img, int g) {
    if (g == G_NONE) return;
    for (int i = 0; i < CW * CH; i++) {
        u16 p = img[i];
        int r = (p >> 11) << 3, gr = ((p >> 5) & 63) << 2, b = (p & 31) << 3;
        int lum = (r * 77 + gr * 150 + b * 29) >> 8;
        switch (g) {
        case G_BW:      r = gr = b = lum; break;
        case G_SEPIA:   r = (lum * 281 >> 8) + 20; gr = lum; b = lum * 205 >> 8; break;
        case G_WARM:    r += 22; b -= 22; break;
        case G_COOL:    r -= 22; b += 22; break;
        case G_VINTAGE: r = (r * 200 >> 8) + 35; gr = (gr * 200 >> 8) + 28;
                        b = (b * 200 >> 8) + 12; break;
        case G_POP:     r = lum + (r - lum) * 2; gr = lum + (gr - lum) * 2;
                        b = lum + (b - lum) * 2; break;
        }
        r = clamp8(r); gr = clamp8(gr); b = clamp8(b);
        img[i] = (u16)(((r >> 3) << 11) | ((gr >> 2) << 5) | (b >> 3));
    }
}

// Snapchat-style caption bar: darkened band with centred text.
static void draw_caption(u16 *img, const char *text) {
    int y0 = 150, h = 28;
    for (int y = y0; y < y0 + h; y++)
        for (int x = 0; x < CW; x++) {
            u16 *p = &img[y * CW + x];
            *p = (*p >> 1) & 0x7BEF;             // half brightness
        }
    draw_text(img, (CW - text_width(text, 2)) / 2, y0 + 7, 2, text, 0xFFFFFF);
}

static void ask_caption(char *cap, size_t len) {
    SwkbdState sw;
    swkbdInit(&sw, SWKBD_TYPE_NORMAL, 2, 20);
    swkbdSetHintText(&sw, "Caption (empty to clear)");
    char buf[64] = "";
    if (swkbdInputText(&sw, buf, sizeof(buf)) == SWKBD_BUTTON_CONFIRM) {
        size_t n = 0;
        for (size_t i = 0; buf[i] && n < len - 1 && n < 20; i++) {
            unsigned char c = (unsigned char)buf[i];
            cap[n++] = (c >= 32 && c < 127) ? (char)toupper(c) : ' ';
        }
        cap[n] = 0;
    }
}

typedef struct { unsigned long score, streak, lastDay; } Stats;
#define STATS_FILE PHOTO_DIR "/stats.txt"

static void stats_load(Stats *s) {
    s->score = s->streak = s->lastDay = 0;
    FILE *f = fopen(STATS_FILE, "r");
    if (f) {
        if (fscanf(f, "%lu %lu %lu", &s->score, &s->streak, &s->lastDay) != 3)
            s->score = s->streak = s->lastDay = 0;
        fclose(f);
    }
}

static void stats_snap(Stats *s) {
    unsigned long day = (unsigned long)(time(NULL) / 86400);
    if (day != s->lastDay) {
        s->streak = (s->lastDay && day == s->lastDay + 1) ? s->streak + 1 : 1;
        s->lastDay = day;
    }
    s->score++;
    FILE *f = fopen(STATS_FILE, "w");
    if (f) { fprintf(f, "%lu %lu %lu\n", s->score, s->streak, s->lastDay); fclose(f); }
}

/* ---------------- memories gallery ---------------- */

#define MAX_PICS 200

typedef struct {
    char names[MAX_PICS][40];        // sorted oldest to newest
    int count, index;
    bool confirm;                    // delete needs a second press
    bool ok;                         // current picture loaded fine
    u16 img[CW * CH];
} Gallery;

static Gallery gal;

static u32 le32(const u8 *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
}

// Reads a BMP (16, 24 or 32 bit, no compression) and fits it to CW x CH.
static bool load_bmp(const char *path, u16 *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    u8 h[54];
    if (fread(h, 1, 54, f) != 54 || h[0] != 'B' || h[1] != 'M') { fclose(f); return false; }
    u32 off = le32(h + 10);
    int w = (int)le32(h + 18), ht = (int)le32(h + 22);
    int bpp = h[28] | (h[29] << 8);
    u32 comp = le32(h + 30);
    bool topDown = ht < 0;
    if (topDown) ht = -ht;
    if (w <= 0 || ht <= 0 || w > 4096 || ht > 4096 || (comp != 0 && comp != 3) ||
        (bpp != 16 && bpp != 24 && bpp != 32)) {
        fclose(f);
        return false;
    }
    size_t bytes = (size_t)bpp / 8;
    size_t stride = ((size_t)w * bpp + 31) / 32 * 4;
    u8 *row = (u8 *)malloc(stride);
    if (!row) { fclose(f); return false; }

    bool ok = true;
    int lastRow = -1;
    for (int oy = 0; oy < CH && ok; oy++) {
        int sy = oy * ht / CH;
        int fileRow = topDown ? sy : ht - 1 - sy;
        if (fileRow != lastRow) {
            if (fseek(f, (long)(off + (size_t)fileRow * stride), SEEK_SET) != 0 ||
                fread(row, 1, stride, f) < (size_t)w * bytes) {
                ok = false;
                break;
            }
            lastRow = fileRow;
        }
        for (int ox = 0; ox < CW; ox++) {
            const u8 *p = row + (size_t)(ox * w / CW) * bytes;
            int r, g, b;
            if (bpp == 16) {
                u16 v = (u16)(p[0] | (p[1] << 8));
                if (comp == 3) { out[oy * CW + ox] = v; continue; }  // already 565
                r = ((v >> 10) & 31) << 3; g = ((v >> 5) & 31) << 3; b = (v & 31) << 3;
            } else {
                b = p[0]; g = p[1]; r = p[2];
            }
            out[oy * CW + ox] = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
    free(row);
    fclose(f);
    return ok;
}

static int cmp_name(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

static void gallery_load(Gallery *g) {
    g->ok = false;
    if (g->count <= 0) return;
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", PHOTO_DIR, g->names[g->index]);
    g->ok = load_bmp(path, g->img);
}

static void gallery_open(Gallery *g) {
    g->count = 0;
    g->confirm = false;
    DIR *d = opendir(PHOTO_DIR);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL && g->count < MAX_PICS) {
            size_t n = strlen(e->d_name);
            if (n < 5 || n >= sizeof(g->names[0])) continue;
            if (strcasecmp(e->d_name + n - 4, ".bmp") != 0) continue;
            memcpy(g->names[g->count++], e->d_name, n + 1);
        }
        closedir(d);
    }
    qsort(g->names, (size_t)g->count, sizeof(g->names[0]), cmp_name);
    g->index = g->count ? g->count - 1 : 0;   // start at the newest
    gallery_load(g);
}

/* ---------------- boomerang ---------------- */

#define BOOM_N 12                    // frames recorded (about 0.8 second)
#define BOOM_MS 70                   // time per frame when playing/saving

static void boom_free(u16 **b) {
    for (int i = 0; i < BOOM_N; i++) { free(b[i]); b[i] = NULL; }
}

static bool boom_alloc(u16 **b) {
    for (int i = 0; i < BOOM_N; i++) {
        b[i] = (u16 *)malloc(FRAME_BYTES);
        if (!b[i]) { boom_free(b); return false; }
    }
    return true;
}

// Bit writer for GIF data: every code is 9 bits, packed into 255-byte blocks.
typedef struct { FILE *f; u8 buf[255]; int len; u32 acc; int nbits; } BitOut;

static void bo_block(BitOut *o) {
    if (o->len) { fputc(o->len, o->f); fwrite(o->buf, 1, (size_t)o->len, o->f); o->len = 0; }
}
static void bo_byte(BitOut *o, u8 b) {
    o->buf[o->len++] = b;
    if (o->len == 255) bo_block(o);
}
static void bo_code(BitOut *o, u32 code) {
    o->acc |= code << o->nbits;
    o->nbits += 9;
    while (o->nbits >= 8) { bo_byte(o, (u8)(o->acc & 255)); o->acc >>= 8; o->nbits -= 8; }
}

// One frame, 256 colours (3-3-2) with light dithering. A "clear" code is sent
// often so the code size never grows: simple, valid GIF without a big table.
static void gif_frame(FILE *f, const u16 *img) {
    static const u8 bayer[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };
    const u8 head[] = { 0x21, 0xF9, 0x04, 0x00, BOOM_MS / 10, 0, 0, 0,
                        0x2C, 0, 0, 0, 0, CW & 255, CW >> 8, CH & 255, CH >> 8, 0, 8 };
    fwrite(head, 1, sizeof(head), f);

    BitOut o = { f, {0}, 0, 0, 0 };
    bo_code(&o, 256);                // clear
    int count = 0;
    for (int y = 0; y < CH; y++) {
        for (int x = 0; x < CW; x++) {
            u16 p = img[y * CW + x];
            int d = bayer[y & 3][x & 3];
            int r = p >> 11;  r = (r << 3) | (r >> 2);
            int g = (p >> 5) & 63;  g = (g << 2) | (g >> 4);
            int b = p & 31;  b = (b << 3) | (b >> 2);
            r = clamp8(r + d * 2 - 15) >> 5;
            g = clamp8(g + d * 2 - 15) >> 5;
            b = clamp8(b + d * 4 - 30) >> 6;
            bo_code(&o, (u32)((r << 5) | (g << 2) | b));
            if (++count == 250) { bo_code(&o, 256); count = 0; }
        }
    }
    bo_code(&o, 257);                // end of picture
    if (o.nbits) bo_byte(&o, (u8)(o.acc & 255));
    bo_block(&o);
    fputc(0, f);
}

// Plays forward then backward, and loops forever.
static bool save_boom_gif(const char *path, u16 *const *frames) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    const u8 hdr[13] = { 'G', 'I', 'F', '8', '9', 'a', CW & 255, CW >> 8, CH & 255, CH >> 8,
                         0xF7, 0, 0 };
    fwrite(hdr, 1, sizeof(hdr), f);
    for (int i = 0; i < 256; i++) {
        u8 rgb[3] = { (u8)(((i >> 5) & 7) * 255 / 7), (u8)(((i >> 2) & 7) * 255 / 7),
                      (u8)((i & 3) * 85) };
        fwrite(rgb, 1, 3, f);
    }
    const u8 loop[] = { 0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0',
                        3, 1, 0, 0, 0 };
    fwrite(loop, 1, sizeof(loop), f);

    for (int i = 0; i < BOOM_N; i++) gif_frame(f, frames[i]);
    for (int i = BOOM_N - 2; i >= 1; i--) gif_frame(f, frames[i]);
    fputc(0x3B, f);
    bool ok = !ferror(f);
    if (fclose(f) != 0) ok = false;
    return ok;
}

static bool boom_path(char *path, size_t plen, char *name, size_t nlen) {
    mkdir("sdmc:/3ds", 0777);
    mkdir(PHOTO_DIR, 0777);
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    char stamp[32] = "X";
    if (tmv) strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", tmv);
    for (int n = 0; n < 100; n++) {
        snprintf(name, nlen, "BOOM_%s_%02d.gif", stamp, n);
        snprintf(path, plen, "%s/%s", PHOTO_DIR, name);
        if (access(path, F_OK) != 0) return true;
    }
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

    int grade = G_NONE, timerIdx = 0;
    char caption[24] = "";
    u64 timerAt = 0, gradeBannerUntil = 0;
    Stats stats;
    stats_load(&stats);

    int mode = M_CAMERA;
    u16 *boom[BOOM_N] = {0};
    int boomCount = 0, boomSkip = 0, boomStep = 0;
    u64 boomNext = 0;
    bool savePending = false, btnHeld = false;

    if (!camReady)
        snprintf(message, sizeof(message), "CAMERA ERROR %08lX", (unsigned long)camError);

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown(), held = hidKeysHeld();
        u64 now = osGetTime();

        if (down & KEY_START) break;

        // touch: bottom row is buttons, the rest belongs to the carousel
        int btn = -1;
        if (held & KEY_TOUCH) hidTouchRead(&touch);
        if (down & KEY_TOUCH) {
            btnHeld = touch.py >= BTN_Y;
            if (btnHeld) btn = button_hit(touch.px, mode_buttons(mode));
        }
        if (!(held & KEY_TOUCH)) btnHeld = false;

        if (mode == M_GALLERY) {
            /* ---------- memories ---------- */
            int step = 0;
            if ((down & (KEY_DLEFT | KEY_L)) || btn == 0) step = -1;
            if ((down & (KEY_DRIGHT | KEY_R)) || btn == 1) step = 1;
            bool del = (down & KEY_X) || btn == 2;
            bool back = (down & KEY_B) || btn == 3;

            if (back) {
                mode = M_CAMERA;
            } else if (gal.count > 0) {
                if (step) {
                    gal.index = (gal.index + step + gal.count) % gal.count;
                    gal.confirm = false;
                    gallery_load(&gal);
                } else if (del) {
                    if (!gal.confirm) {
                        gal.confirm = true;
                    } else {
                        char path[128];
                        snprintf(path, sizeof(path), "%s/%s", PHOTO_DIR, gal.names[gal.index]);
                        remove(path);
                        memmove(gal.names[gal.index], gal.names[gal.index + 1],
                                (size_t)(gal.count - gal.index - 1) * sizeof(gal.names[0]));
                        gal.count--;
                        if (gal.index >= gal.count) gal.index = gal.count - 1;
                        if (gal.index < 0) gal.index = 0;
                        gal.confirm = false;
                        gallery_load(&gal);
                    }
                }
            }

            if (gal.count > 0 && gal.ok) {
                memcpy(shown, gal.img, sizeof(shown));
            } else {
                draw_rect(shown, 0, 0, CW, CH, 0x000000);
                const char *t = gal.count ? "CANT OPEN THIS FILE" : "NO PHOTOS YET";
                draw_text(shown, (CW - text_width(t, 2)) / 2, 108, 2, t, 0xFFFFFF);
            }

            draw_rect(ui, 0, 0, CW, CH, 0x15151C);
            draw_rect(ui, 0, 0, CW, 34, 0xFFD60A);
            draw_text(ui, (CW - text_width("MEMORIES", 3)) / 2, 7, 3, "MEMORIES", 0x15151C);
            if (gal.count > 0) {
                char up[48], line[40];
                size_t i = 0;
                for (; gal.names[gal.index][i] && i < sizeof(up) - 1; i++)
                    up[i] = (char)toupper((unsigned char)gal.names[gal.index][i]);
                up[i] = 0;
                draw_text(ui, (CW - text_width(up, 1)) / 2, 50, 1, up, 0xFFFFFF);
                snprintf(line, sizeof(line), "%d / %d", gal.index + 1, gal.count);
                draw_text(ui, (CW - text_width(line, 2)) / 2, 66, 2, line, 0xFFD60A);
                if (gal.confirm)
                    draw_text(ui, (CW - text_width("PRESS DELETE AGAIN TO CONFIRM", 1)) / 2, 92, 1,
                              "PRESS DELETE AGAIN TO CONFIRM", 0xFF9F0A);
            }
            draw_text(ui, 10, 130, 1, "L/R OR DPAD OR < >: BROWSE", 0xAAAAAA);
            draw_text(ui, 10, 142, 1, "X OR DELETE: DELETE (ASKS TWICE)", 0xAAAAAA);
            draw_text(ui, 10, 154, 1, "B OR BACK: GO BACK TO CAMERA", 0xAAAAAA);
            draw_buttons(BTN_GAL, 4);

        } else if (mode == M_BOOM_PLAY) {
            /* ---------- boomerang preview ---------- */
            if (savePending) {
                savePending = false;
                char path[128], name[48];
                bool ok = boom_path(path, sizeof(path), name, sizeof(name)) &&
                          save_boom_gif(path, boom);
                if (ok) {
                    stats_snap(&stats);
                    snprintf(message, sizeof(message), "SAVED %s", name);
                } else {
                    snprintf(message, sizeof(message), "SAVE FAILED - CHECK SD CARD");
                }
                boom_free(boom);
                mode = M_CAMERA;
                messageUntil = osGetTime() + 3000;
            } else if ((down & KEY_A) || btn == 0) {
                savePending = true;
                snprintf(message, sizeof(message), "SAVING GIF...");
            } else if ((down & KEY_B) || btn == 1) {
                boom_free(boom);
                mode = M_CAMERA;
                snprintf(message, sizeof(message), "BOOMERANG DISCARDED");
                messageUntil = now + 2000;
            }

            if (now >= boomNext) { boomStep++; boomNext = now + BOOM_MS; }
            if (mode == M_BOOM_PLAY) {
                int seq = boomStep % (2 * BOOM_N - 2);
                int fi = seq < BOOM_N ? seq : 2 * BOOM_N - 2 - seq;
                memcpy(shown, boom[fi], sizeof(shown));
            }

            draw_rect(ui, 0, 0, CW, CH, 0x15151C);
            draw_rect(ui, 0, 0, CW, 34, 0xFFD60A);
            draw_text(ui, (CW - text_width("BOOMERANG", 3)) / 2, 7, 3, "BOOMERANG", 0x15151C);
            draw_text(ui, 10, 56, 1, "PREVIEW (PLAYS FORWARD AND BACK)", 0xFFFFFF);
            draw_text(ui, 10, 72, 1, "A OR SAVE GIF: SAVE IT", 0xFFFFFF);
            draw_text(ui, 10, 84, 1, "B OR DISCARD: THROW IT AWAY", 0xFFFFFF);
            draw_text(ui, 10, 108, 1, "THE GIF IS SAVED IN THE SNAP3DS FOLDER.", 0xAAAAAA);
            draw_text(ui, 10, 120, 1, "OPEN IT ON YOUR PC OR PHONE.", 0xAAAAAA);
            draw_buttons(BTN_PLAY, 2);
            if (message[0]) draw_text(ui, 10, 224, 1, message, 0xFFD60A);

        } else {
            /* ---------- camera (and boomerang recording) ---------- */
            int previous = filter;
            if (down & KEY_R) carousel.target++;
            if (down & KEY_L) carousel.target--;

            bool tapShutter = carousel_update(&carousel, (held & KEY_TOUCH) && !btnHeld,
                                              touch.px, touch.py);
            if (mode != M_CAMERA) tapShutter = false;
            filter = wrap(carousel.target);
            if (filter != previous) bannerUntil = now + 1200;

            // bottom buttons
            if (mode == M_CAMERA) {
                if (btn == 0) {
                    if (cam_flip()) {
                        mirror = (camSelect == SELECT_IN1);   // selfie camera mirrors
                        tracker_reset(&tracker);
                        face = tracker.face;
                        snprintf(message, sizeof(message),
                                 camSelect == SELECT_IN1 ? "SELFIE CAMERA" : "OUTER CAMERA");
                    } else {
                        snprintf(message, sizeof(message), "FLIP FAILED");
                    }
                    messageUntil = now + 2000;
                } else if (btn == 1) {
                    timerAt = 0;
                    gallery_open(&gal);
                    mode = M_GALLERY;
                } else if (btn == 2) {
                    timerAt = 0;
                    if (boom_alloc(boom)) {
                        mode = M_BOOM_REC;
                        boomCount = 0;
                        boomSkip = 0;
                    } else {
                        snprintf(message, sizeof(message), "NOT ENOUGH MEMORY");
                        messageUntil = now + 3000;
                    }
                }
            } else if (btn == 0) {                       // cancel recording
                boom_free(boom);
                mode = M_CAMERA;
            }

            if (down & KEY_Y) {
                autoTrack = !autoTrack;  // locking keeps the filter where it is
            }
            if (down & KEY_B) {
                if (mode == M_BOOM_REC) {
                    boom_free(boom);
                    mode = M_CAMERA;
                } else {
                    offX = offY = 0; scale = 1.0f; autoTrack = true;
                    tracker_reset(&tracker);
                    face = tracker.face;
                }
            }
            if (down & KEY_SELECT) mirror = !mirror;

            // looks, timer, caption
            if (down & KEY_DRIGHT) { grade = (grade + 1) % G_COUNT; gradeBannerUntil = now + 1200; }
            if (down & KEY_DLEFT)  { grade = (grade + G_COUNT - 1) % G_COUNT; gradeBannerUntil = now + 1200; }
            if (down & KEY_ZL) timerIdx = (timerIdx + 1) % 4;
            if (mode == M_CAMERA && (down & KEY_ZR)) ask_caption(caption, sizeof(caption));

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
            filter_draw_any(photo, filter, placed, (held & KEY_A) != 0, (now % 600000) / 1000.0f);
            apply_grade(photo, grade);
            if (caption[0]) draw_caption(photo, caption);

            // boomerang: keep every second camera picture
            if (mode == M_BOOM_REC && picture && havePicture) {
                if ((boomSkip++ & 1) == 0) memcpy(boom[boomCount++], photo, FRAME_BYTES);
                if (boomCount >= BOOM_N) {
                    mode = M_BOOM_PLAY;
                    boomStep = 0;
                    boomNext = now + BOOM_MS;
                    savePending = false;
                    message[0] = 0;
                    messageUntil = 0;
                }
            }

            // shutter, with optional self-timer
            if (mode == M_CAMERA) {
                bool fire = false;
                if ((down & KEY_X) || tapShutter) {
                    if (timerAt) {
                        timerAt = 0;
                        snprintf(message, sizeof(message), "TIMER CANCELLED");
                        messageUntil = now + 2000;
                    } else if (TIMER_SECS[timerIdx]) {
                        timerAt = now + TIMER_SECS[timerIdx] * 1000ULL;
                    } else {
                        fire = true;
                    }
                }
                if (timerAt && now >= timerAt) { timerAt = 0; fire = true; }

                if (fire) {
                    if (havePicture) {
                        if (take_photo(message, sizeof(message))) {
                            stats_snap(&stats);
                            CAMU_PlayShutterSound(SHUTTER_SOUND_TYPE_NORMAL);
                            flashUntil = osGetTime() + 120;
                        }
                    } else {
                        snprintf(message, sizeof(message), "NO CAMERA PICTURE YET");
                    }
                    messageUntil = osGetTime() + 3000;
                }
            }
            if (camReady && now > messageUntil && messageUntil) { message[0] = 0; messageUntil = 0; }

            // top screen: photo, plus extras that are not saved in the photo
            memcpy(shown, photo, sizeof(shown));
            if (now < bannerUntil) {
                int w = text_width(filter_name_any(filter), 2);
                draw_rect(shown, (CW - w) / 2 - 8, 6, w + 16, 24, 0x000000);
                draw_text(shown, (CW - w) / 2, 11, 2, filter_name_any(filter), 0xFFFFFF);
            }
            if (now < gradeBannerUntil) {
                int w = text_width(GRADE_NAMES[grade], 2);
                draw_rect(shown, (CW - w) / 2 - 8, 34, w + 16, 24, 0x000000);
                draw_text(shown, (CW - w) / 2, 39, 2, GRADE_NAMES[grade], 0xFFFFFF);
            }
            if (timerAt) {               // big countdown, not saved in the photo
                char n[8];
                snprintf(n, sizeof(n), "%d", (int)((timerAt - now + 999) / 1000));
                draw_text(shown, (CW - text_width(n, 10)) / 2, 80, 10, n, 0xFFFFFF);
            }
            if (mode == M_BOOM_REC) {
                char rec[24];
                snprintf(rec, sizeof(rec), "REC %d/%d", boomCount, BOOM_N);
                int w = text_width(rec, 2);
                draw_rect(shown, 8, 8, w + 16, 24, 0x000000);
                draw_text(shown, 16, 13, 2, rec, 0xFF3B30);
            }
            if (osGetTime() < flashUntil) draw_rect(shown, 0, 0, CW, CH, 0xFFFFFF);

            draw_ui(&carousel, filter, autoTrack, tracker.found, mirror, message);

            // extra bottom-screen info
            char line[40], tl[8] = "OFF";
            snprintf(line, sizeof(line), "SCORE %lu", stats.score);
            draw_text(ui, 10, 44, 1, line, 0xFFD60A);
            snprintf(line, sizeof(line), "STREAK %lu", stats.streak);
            draw_text(ui, CW - 10 - text_width(line, 1), 44, 1, line, 0xFF9F0A);
            if (TIMER_SECS[timerIdx]) snprintf(tl, sizeof(tl), "%dS", TIMER_SECS[timerIdx]);
            snprintf(line, sizeof(line), "LOOK: %s   TIMER: %s", GRADE_NAMES[grade], tl);
            draw_text(ui, 10, 106, 1, line, 0xFFFFFF);
            draw_text(ui, 10, 116, 1, "DPAD L/R: LOOK  ZL: TIMER  ZR: CAPTION", 0xAAAAAA);
            if (mode == M_CAMERA) draw_buttons(BTN_CAM, 3);
            else draw_buttons(BTN_REC, 1);
        }

        blit(GFX_TOP, shown);
        blit(GFX_BOTTOM, ui);
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    boom_free(boom);
    aptUnhook(&hookCookie);
    cam_stop();
    if (evError) svcCloseHandle(evError);
    camExit();
    for (int i = 0; i < 3; i++) free(bufs[i]);
    gfxExit();
    return 0;
}
