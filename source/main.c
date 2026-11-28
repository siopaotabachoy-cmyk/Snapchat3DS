// main.c - Snap3DS: 3DS camera, screens, buttons and touch
// Features: face filters, color looks, self-timer, captions, snap score/streak,
//           flip camera, Snapchat-style memories, boomerang (saved as animated GIF).
#include <3ds.h>
#include <ctype.h>
#include <dirent.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "art.h"
#include "extra.h"
#include "memories.h"
#include "chat.h"
#include "snapmap.h"
#include "stories.h"

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
#define CAR_Y 177
#define CAR_STEP 62

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

/* ---------------- touch buttons (bottom row) ---------------- */

#define BTN_Y 205
#define BTN_H 16

enum { M_CAMERA, M_GALLERY, M_BOOM_REC, M_BOOM_PLAY, M_PROFILE, M_CHAT, M_MAP, M_STORIES };

static const char *const BTN_REC[]  = { "CANCEL" };
static const char *const BTN_PLAY[] = { "SAVE GIF", "DISCARD" };
static const char *const BTN_PROF[] = { "DONE" };

static int mode_buttons(int mode) {
    switch (mode) {
    case M_CAMERA: return 1;
    case M_BOOM_REC: return 1;
    case M_PROFILE: return 1;
    case M_BOOM_PLAY: return 2;
    default: return 1;               // M_GALLERY reads its own touches
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

static char lastPhoto[48];            // file name of the newest saved photo

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
            if (ok) snprintf(lastPhoto, sizeof(lastPhoto), "%s", name);
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
    "NORMAL", "B+W", "SEPIA", "WARM", "COOL", "VINTAGE", "POP"
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

// Keyboard for the Chat screen. Returns true if the person pressed OK with some text.
static bool ask_chat(const char *hint, char *out, size_t len) {
    SwkbdState sw;
    swkbdInit(&sw, SWKBD_TYPE_NORMAL, 2, 60);
    swkbdSetHintText(&sw, hint);
    char buf[96] = "";
    if (swkbdInputText(&sw, buf, sizeof(buf)) != SWKBD_BUTTON_CONFIRM) return false;
    size_t n = 0;
    for (size_t i = 0; buf[i] && n < len - 1 && n < 60; i++) {
        unsigned char c = (unsigned char)buf[i];
        out[n++] = (c >= 32 && c < 127) ? (char)toupper(c) : ' ';
    }
    out[n] = 0;
    return n > 0;
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

/* ---------------- memories ---------------- */

// The Memories screen lives in memories.c. mem.img is a spare CW x CH picture
// that the avatar code borrows while Memories is closed.
static Memories mem;             // about 2 MB, so it is static
static Chat chat;                // the fake Chat screen (chat.c)
static SnapMap snapMap;          // the offline Map screen (snapmap.c)
static Stories stories;          // slideshow of today's snaps (stories.c)

// The avatar and profile photo use this reader. Memories has its own, for thumbnails.
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

/* ---------------- profile and avatar ---------------- */

// Tap the round picture at the top left of the bottom screen to open the profile.
// The avatar is drawn by the app and can be edited there. A square BMP at
// AVATAR_FILE can be used instead (it is outside Snap3DS, so not in Memories).
#define AVATAR_FILE "sdmc:/3ds/Snap3DS_avatar.bmp"
#define PROFILE_FILE PHOTO_DIR "/profile.txt"
#define AV_SRC 96                    // the avatar is made at this size, then shrunk
#define PROF_NAME_MAX 14
#define USER_MAX 18
#define PHOTO_FILE "sdmc:/3ds/Snap3DS_photo.bmp"   // the round profile photo

enum { P_STYLE, P_HAIR, P_ENDS, P_SKIN, P_LIPS, P_TOP, P_PICTURE, P_COUNT };

static const char *const P_LABELS[P_COUNT] = {
    "HAIR STYLE", "HAIR COLOR", "HAIR ENDS", "SKIN", "LIPS", "TOP", "AVATAR"
};
static const int P_CHOICES[P_COUNT] = { 4, 6, 6, 5, 4, 6, 2 };

static const char *const STYLE_NAMES[4] = { "LONG", "BOB", "SHORT", "BUN" };
static const char *const HAIR_NAMES[6] = { "BLACK", "DARK PLUM", "BROWN", "BLONDE", "RED", "PINK" };
static const u32 HAIR_RGB[6] = { 0x1C1618, 0x4A2430, 0x6B4428, 0xD8B060, 0xA8402A, 0xE87CA8 };
static const char *const END_NAMES[6] = { "SAME", "LIGHT BROWN", "BLONDE", "RED", "PINK", "BLUE" };
static const u32 END_RGB[6] = { 0, 0x9A7050, 0xD8B060, 0xB8452C, 0xE87CA8, 0x4A7CD0 };
static const char *const SKIN_NAMES[5] = { "FAIR", "LIGHT", "TAN", "BROWN", "DEEP" };
static const u32 SKIN_RGB[5] = { 0xF6D3BC, 0xEBBB95, 0xD99A68, 0xA86C46, 0x70452E };
static const char *const LIP_NAMES[4] = { "NATURAL", "RED", "PINK", "BERRY" };
static const u32 LIP_RGB[4] = { 0xB8665E, 0xA82838, 0xE0607E, 0x80203C };
static const char *const TOP_NAMES[6] = { "NAVY", "BLACK", "WHITE", "PINK", "RED", "GREEN" };
static const u32 TOP_RGB[6] = { 0x1C3C70, 0x202024, 0xF0F0F0, 0xF088B0, 0xC83038, 0x2E8858 };

typedef struct {
    char name[PROF_NAME_MAX + 1];
    char user[USER_MAX + 1];
    int month, day;                  // birthday, 0 = not set
    int v[P_COUNT];
} SnapProfile;

static u16 avatarArt[AV_SRC * AV_SRC];
static u16 faceArt[AV_SRC * AV_SRC];   // round profile photo
static bool faceOk;
static bool pictureOk;               // AVATAR_FILE was loaded

static const char *profile_value(const SnapProfile *p, int row) {
    int v = p->v[row];
    switch (row) {
    case P_STYLE: return STYLE_NAMES[v];
    case P_HAIR:  return HAIR_NAMES[v];
    case P_ENDS:  return END_NAMES[v];
    case P_SKIN:  return SKIN_NAMES[v];
    case P_LIPS:  return LIP_NAMES[v];
    case P_TOP:   return TOP_NAMES[v];
    default:      return v ? "MY PICTURE" : "CARTOON";
    }
}

// Cartoon drawing helpers: coordinates are on a 96 x 96 grid, moved by (ox, oy)
// and made k times bigger.
static void toon_oval(u16 *c, float ox, float oy, float k, float x, float y,
                      float rx, float ry, float angle, u32 rgb, int alpha) {
    draw_ellipse(c, ox + x * k, oy + y * k, rx * k, ry * k, angle, rgb, alpha);
}
static void toon_line(u16 *c, float ox, float oy, float k, float x0, float y0,
                      float x1, float y1, float thick, u32 rgb) {
    draw_line(c, ox + x0 * k, oy + y0 * k, ox + x1 * k, oy + y1 * k, thick * k, rgb);
}
#define OVAL(...) toon_oval(c, ox, oy, k, __VA_ARGS__)
#define LINE(...) toon_line(c, ox, oy, k, __VA_ARGS__)

static void avatar_cartoon(u16 *c, const SnapProfile *p, float ox, float oy, float k) {
    int style = p->v[P_STYLE];
    u32 hair = HAIR_RGB[p->v[P_HAIR]], skin = SKIN_RGB[p->v[P_SKIN]];
    u32 ends = p->v[P_ENDS] ? END_RGB[p->v[P_ENDS]] : hair;

    // hair behind the head
    if (style == 0) {                                          // long
        for (int s = -1; s <= 1; s += 2)
            OVAL(48 + s * 20, 68, 15, 25, s * -0.12f, ends, 255);
        OVAL(48, 43, 31, 35, 0, hair, 255);
    } else if (style == 1) {                                   // bob
        for (int s = -1; s <= 1; s += 2)
            OVAL(48 + s * 21, 59, 13, 17, 0, ends, 255);
        OVAL(48, 42, 31, 31, 0, hair, 255);
    } else {
        if (style == 3) OVAL(48, 13, 14, 11, 0, ends, 255);   // bun
        OVAL(48, 39, 27, 28, 0, hair, 255);
    }
    OVAL(48, 99, 35, 22, 0, TOP_RGB[p->v[P_TOP]], 255);       // top
    draw_rect(c, (int)(ox + 41 * k), (int)(oy + 66 * k), (int)(14 * k), (int)(15 * k), skin);   // neck
    OVAL(48, 80, 7, 4, 0, skin, 255);
    OVAL(48, 47, 22, 28, 0, skin, 255);                       // face
    // fringe, swept to both sides
    OVAL(36, 27, 18, 8, -0.45f, hair, 255);
    OVAL(60, 27, 18, 8, 0.45f, hair, 255);

    for (int s = -1; s <= 1; s += 2) {
        float x = 48 + s * 10;
        OVAL(x, 47, 5.0f, 3.2f, 0, 0xFFFFFF, 255);
        OVAL(x, 47, 2.8f, 2.8f, 0, 0x3A2418, 255);
        OVAL(x + 1, 46, 0.9f, 0.9f, 0, 0xFFFFFF, 255);
        LINE(x - 5, 43.5f, x + 5, 43.5f, 1.6f, 0x241410);        // lid
        LINE(x - 5, 39, x + 5, 38.5f, 1.8f, 0x3A241C);           // brow
        OVAL(48 + s * 13, 56, 5, 3, 0, 0xF06878, 60);         // blush
    }
    OVAL(48, 56, 2.5f, 1.6f, 0, 0x804030, 70);                // nose
    OVAL(48, 64, 7.5f, 3.2f, 0, LIP_RGB[p->v[P_LIPS]], 255);  // lips
    LINE(42, 63.5f, 54, 63.5f, 1.0f, 0x602028);
}
#undef OVAL
#undef LINE

// Rebuilds the avatar. tmp is a CW x CH work picture.
static void avatar_build(const SnapProfile *p, u16 *tmp) {
    bool picture = p->v[P_PICTURE] && load_bmp(AVATAR_FILE, tmp);
    if (p->v[P_PICTURE]) pictureOk = picture;
    if (!picture) {
        draw_rect(tmp, 0, 0, AV_SRC, AV_SRC, 0xFFE27A);
        avatar_cartoon(tmp, p, 0, 0, 1.0f);
    }
    // load_bmp stretches to the full CW x CH; the cartoon only uses a square
    int w = picture ? CW : AV_SRC, h = picture ? CH : AV_SRC;
    for (int y = 0; y < AV_SRC; y++)
        for (int x = 0; x < AV_SRC; x++)
            avatarArt[y * AV_SRC + x] = tmp[y * h / AV_SRC * CW + x * w / AV_SRC];
}

static void face_load(u16 *tmp) {
    faceOk = load_bmp(PHOTO_FILE, tmp);
    if (!faceOk) return;
    for (int y = 0; y < AV_SRC; y++)
        for (int x = 0; x < AV_SRC; x++)
            faceArt[y * AV_SRC + x] = tmp[y * CH / AV_SRC * CW + x * CW / AV_SRC];
}

// Round picture, size x size pixels, top left corner at (x0, y0).
static void draw_round(u16 *img, const u16 *art, int x0, int y0, int size) {
    float r = size / 2.0f;
    draw_ellipse(img, x0 + r - 0.5f, y0 + r - 0.5f, r + 2, r + 2, 0, 0xFFFFFF, 255);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            int dx = 2 * x + 1 - size, dy = 2 * y + 1 - size;
            if (dx * dx + dy * dy > size * size) continue;         // circle shape
            int cr = 0, cg = 0, cb = 0;
            for (int k = 0; k < 4; k++) {                          // 4 samples = smooth
                int sx = (x * 4 + 1 + 2 * (k & 1)) * AV_SRC / (size * 4);
                int sy = (y * 4 + 1 + 2 * (k >> 1)) * AV_SRC / (size * 4);
                u16 q = art[sy * AV_SRC + sx];
                cr += q >> 11; cg += (q >> 5) & 63; cb += q & 31;
            }
            img[(y0 + y) * CW + x0 + x] = (u16)(((cr / 4) << 11) | ((cg / 4) << 5) | (cb / 4));
        }
}

static void draw_avatar(u16 *img, int x0, int y0, int size) {
    draw_round(img, avatarArt, x0, y0, size);
}

static bool valid_date(int m, int d) {
    static const int DAYS[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return m >= 1 && m <= 12 && d >= 1 && d <= DAYS[m - 1];
}

static void profile_load(SnapProfile *p) {
    static const int START_VALUES[P_COUNT] = { 0, 1, 1, 2, 1, 0, 0 };
    strcpy(p->name, "ME");
    strcpy(p->user, "USERNAME");
    p->month = p->day = 0;
    memcpy(p->v, START_VALUES, sizeof(p->v));
    FILE *f = fopen(PROFILE_FILE, "r");
    if (!f) return;
    for (int i = 0; i < P_COUNT; i++) {
        int v;
        if (fscanf(f, "%d", &v) == 1 && v >= 0 && v < P_CHOICES[i]) p->v[i] = v;
    }
    char line[64];
    if (fgets(line, sizeof(line), f) && fgets(line, sizeof(line), f)) {   // name is line 2
        line[strcspn(line, "\r\n")] = 0;
        if (line[0]) snprintf(p->name, sizeof(p->name), "%.*s", PROF_NAME_MAX, line);
        if (fgets(line, sizeof(line), f)) {                               // line 3: username
            line[strcspn(line, "\r\n")] = 0;
            if (line[0]) snprintf(p->user, sizeof(p->user), "%.*s", USER_MAX, line);
        }
        int m, d;
        if (fscanf(f, "%d %d", &m, &d) == 2 && valid_date(m, d)) { p->month = m; p->day = d; }
    }
    fclose(f);
}

static bool profile_save(const SnapProfile *p) {
    mkdir("sdmc:/3ds", 0777);
    mkdir(PHOTO_DIR, 0777);
    FILE *f = fopen(PROFILE_FILE, "w");
    if (!f) return false;
    for (int i = 0; i < P_COUNT; i++) fprintf(f, "%d ", p->v[i]);
    fprintf(f, "\n%s\n%s\n%d %d\n", p->name, p->user, p->month, p->day);
    bool ok = !ferror(f);
    if (fclose(f) != 0) ok = false;
    return ok;
}

// Counts saved photos and boomerangs for the profile card.
static void count_saved(int *photos, int *booms) {
    *photos = *booms = 0;
    DIR *d = opendir(PHOTO_DIR);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n < 5) continue;
        if (strcasecmp(e->d_name + n - 4, ".bmp") == 0) (*photos)++;
        else if (strcasecmp(e->d_name + n - 4, ".gif") == 0) (*booms)++;
    }
    closedir(d);
}

// A streak is over once a whole day has passed without a snap.
static unsigned long stats_streak(const Stats *s) {
    unsigned long day = (unsigned long)(time(NULL) / 86400);
    return day > s->lastDay + 1 ? 0 : s->streak;
}

// Keyboard for one line of text. An empty entry keeps the old text.
static void ask_text(const char *hint, char *out, size_t max) {
    SwkbdState sw;
    swkbdInit(&sw, SWKBD_TYPE_NORMAL, 2, (int)max);
    swkbdSetHintText(&sw, hint);
    char buf[64] = "";
    if (swkbdInputText(&sw, buf, sizeof(buf)) == SWKBD_BUTTON_CONFIRM) {
        size_t n = 0;
        for (size_t i = 0; buf[i] && n < max; i++) {
            unsigned char c = (unsigned char)buf[i];
            if (c >= 32 && c < 127) out[n++] = (char)toupper(c);
        }
        if (n) out[n] = 0;
    }
}

// Birthday is typed as 4 numbers, month first: 1105 = Nov 5.
static bool ask_birthday(SnapProfile *p) {
    SwkbdState sw;
    swkbdInit(&sw, SWKBD_TYPE_NUMPAD, 2, 4);
    swkbdSetHintText(&sw, "Month and day: MMDD");
    char buf[16] = "";
    if (swkbdInputText(&sw, buf, sizeof(buf)) != SWKBD_BUTTON_CONFIRM) return true;
    if (strlen(buf) != 4 || strspn(buf, "0123456789") != 4) return false;
    int v = atoi(buf);
    if (!valid_date(v / 100, v % 100)) return false;
    p->month = v / 100;
    p->day = v % 100;
    return true;
}

static const char *zodiac(int m, int d) {
    static const char *const SIGN[12] = { "CAPRICORN", "AQUARIUS", "PISCES", "ARIES", "TAURUS",
        "GEMINI", "CANCER", "LEO", "VIRGO", "LIBRA", "SCORPIO", "SAGITTARIUS" };
    static const int NEXT_FROM[12] = { 20, 19, 21, 20, 21, 21, 23, 23, 23, 23, 22, 22 };
    return SIGN[(m - 1 + (d >= NEXT_FROM[m - 1])) % 12];
}

/* ---------------- profile pages ---------------- */

enum { PG_HOME, PG_AVATAR, PG_ACCOUNT, PG_PHOTO };

// Rectangle with fully rounded ends.
static void pill(u16 *img, int x, int y, int w, int h, u32 rgb) {
    float r = h / 2.0f;
    draw_ellipse(img, x + r, y + r - 0.5f, r, r, 0, rgb, 255);
    draw_ellipse(img, x + w - r - 1, y + r - 0.5f, r, r, 0, rgb, 255);
    draw_rect(img, x + (int)r, y, w - 2 * (int)r, h, rgb);
}

static void pill_text(u16 *img, int x, int y, int w, int h, const char *text, u32 rgb) {
    pill(img, x, y, w, h, rgb);
    draw_text(img, x + (w - text_width(text, 1)) / 2, y + (h - 7) / 2, 1, text, 0xFFFFFF);
}

// Top screen: red banner, big avatar, round photo, name and info chips.
static void draw_profile_card(u16 *img, const SnapProfile *p, const Stats *st) {
    for (int y = 0; y < CH; y++) {          // red, fading to almost black
        int r, g, b;
        if (y < 80)       { r = 150 + y; g = 36 + y * 28 / 80; b = 40 + y * 28 / 80; }
        else if (y < 190) { int t = 190 - y; r = 14 + 216 * t / 110; g = 14 + 50 * t / 110; b = 16 + 52 * t / 110; }
        else              { r = g = 14; b = 16; }
        draw_rect(img, 0, y, CW, 1, ((u32)r << 16) | ((u32)g << 8) | (u32)b);
    }
    // big avatar with folded arms
    float k = 1.6f, ox = 208 - 48 * k, oy = 2;
    avatar_cartoon(img, p, ox, oy, k);
    u32 top = TOP_RGB[p->v[P_TOP]], skin = SKIN_RGB[p->v[P_SKIN]];
    toon_oval(img, ox, oy, k, 48, 104, 30, 18, 0, top, 255);                  // chest
    u32 edge = (top >> 1) & 0x7F7F7F;                                         // folded arms
    toon_line(img, ox, oy, k, 20, 95, 62, 89, 11, edge);
    toon_line(img, ox, oy, k, 20, 95, 62, 89, 8, top);
    toon_line(img, ox, oy, k, 76, 96, 34, 94, 11, edge);
    toon_line(img, ox, oy, k, 76, 96, 34, 94, 8, top);
    toon_oval(img, ox, oy, k, 63, 89, 6, 4.5f, -0.15f, skin, 255);            // hands
    toon_oval(img, ox, oy, k, 33, 94, 6, 4.5f, 0.1f, skin, 255);
    for (int y = 176; y < 200; y++)         // soft fade under the avatar
        for (int x = 0; x < CW; x++)
            draw_ellipse(img, x, y, 0.6f, 0.6f, 0, 0x0E0E10, (y - 176) * 255 / 23);
    draw_rect(img, 0, 200, CW, CH - 200, 0x0E0E10);

    draw_round(img, faceOk ? faceArt : avatarArt, 14, 122, 72);
    draw_text(img, 99, 160, 2, p->name, 0x000000);                             // shadow
    draw_text(img, 98, 159, 2, p->name, 0xFFFFFF);
    draw_text(img, 98, 180, 1, p->user, 0xC8C8CC);

    // chips: birthday, snap score, zodiac sign
    static const char *const MON[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    char chip[3][24];
    if (p->month) snprintf(chip[0], sizeof(chip[0]), "%s %d", MON[p->month - 1], p->day);
    else snprintf(chip[0], sizeof(chip[0]), "BIRTHDAY");
    snprintf(chip[1], sizeof(chip[1]), "SCORE %lu", st->score);
    snprintf(chip[2], sizeof(chip[2]), "%s", p->month ? zodiac(p->month, p->day) : "ZODIAC");
    int x = 12;
    for (int i = 0; i < 3; i++) {
        int w = text_width(chip[i], 1) + 18;
        pill_text(img, x, 212, w, 18, chip[i], i == 2 && p->month ? 0x5A2A90 : 0x2A2A30);
        x += w + 6;
    }
}

static void icon_hanger(u16 *img, int x, int y) {
    draw_line(img, x, y - 3, x - 9, y + 5, 1.8f, 0xFFFFFF);
    draw_line(img, x, y - 3, x + 9, y + 5, 1.8f, 0xFFFFFF);
    draw_line(img, x - 9, y + 5, x + 9, y + 5, 1.8f, 0xFFFFFF);
    draw_line(img, x, y - 3, x + 3, y - 6, 1.8f, 0xFFFFFF);
    draw_line(img, x + 3, y - 6, x, y - 8, 1.8f, 0xFFFFFF);
}

static void icon_gear(u16 *img, int x, int y, u32 back) {
    for (int i = 0; i < 4; i++) {
        float dx = (i == 0 ? 9.0f : i == 1 ? 0.0f : 6.4f);
        float dy = (i == 0 ? 0.0f : i == 1 ? 9.0f : i == 2 ? 6.4f : -6.4f);
        draw_line(img, x - dx, y - dy, x + dx, y + dy, 3.5f, 0xFFFFFF);
    }
    draw_ellipse(img, x, y, 6.5f, 6.5f, 0, 0xFFFFFF, 255);
    draw_ellipse(img, x, y, 2.8f, 2.8f, 0, back, 255);
}

// Bottom screen of the profile. The touch areas are in the main loop.
#define CARD_Y 90
#define CARD_H 36

static void draw_profile_home(const Stats *st, int photos, int booms) {
    draw_rect(ui, 0, 0, CW, CH, 0x0E0E10);
    draw_rect(ui, 0, 0, CW, 38, 0x962428);
    draw_ellipse(ui, 20, 19, 15, 15, 0, 0x5C161C, 255);                 // back
    draw_text(ui, 15, 12, 2, "<", 0xFFFFFF);
    pill_text(ui, 100, 4, 120, 30, "SAVE CARD", 0x5C161C);
    draw_ellipse(ui, 254, 19, 16, 16, 0, 0x5C161C, 255);                // outfit
    icon_hanger(ui, 254, 20);
    draw_ellipse(ui, 296, 19, 16, 16, 0, 0x5C161C, 255);                // account
    icon_gear(ui, 296, 19, 0x5C161C);

    pill(ui, 8, 46, 150, 28, 0xFFFFFF);
    pill_text(ui, 10, 48, 146, 24, "MY ACCOUNT", 0x3A3A40);
    pill_text(ui, 164, 46, 148, 28, "PROFILE PHOTO", 0x3A3A40);

    draw_text(ui, 10, 80, 1, "SNAP TO...", 0xFFFFFF);
    char sub[3][44];
    snprintf(sub[0], sizeof(sub[0]), "STREAK: %lu", stats_streak(st));
    snprintf(sub[1], sizeof(sub[1]), "PHOTOS SAVED: %d", photos);
    snprintf(sub[2], sizeof(sub[2]), "BOOMERANGS SAVED: %d", booms);
    static const char *const TITLE[3] = { "NEW SNAP", "MEMORIES", "NEW BOOMERANG" };
    static const u32 COLOR[3] = { 0x1EA7F0, 0xFFB020, 0xF0405C };
    for (int i = 0; i < 3; i++) {
        int y = CARD_Y + i * (CARD_H + 4);
        draw_rect(ui, 8, y, CW - 16, CARD_H, 0x222226);
        draw_ellipse(ui, 30, y + CARD_H / 2, 12, 12, 0, COLOR[i], 255);
        draw_ellipse(ui, 30, y + CARD_H / 2, 5, 5, 0, 0xFFFFFF, 255);
        draw_text(ui, 52, y + 8, 1, TITLE[i], 0xFFFFFF);
        draw_text(ui, 52, y + 21, 1, sub[i], 0x9A9AA0);
        draw_text(ui, CW - 24, y + 15, 1, ">", 0x9A9AA0);
    }
}

// Rows used by the avatar and account pages.
#define ROW_Y 40
#define ROW_H 20
#define ACC_ROWS 4

static void draw_profile_rows(const SnapProfile *p, int page, int sel) {
    const char *title = page == PG_AVATAR ? "MY AVATAR" : "MY ACCOUNT";
    draw_rect(ui, 0, 0, CW, CH, 0x15151C);
    draw_rect(ui, 0, 0, CW, 34, 0xFFD60A);
    draw_text(ui, (CW - text_width(title, 3)) / 2, 7, 3, title, 0x15151C);
    int rows = page == PG_AVATAR ? P_COUNT : ACC_ROWS;
    for (int i = 0; i < rows; i++) {
        int y = ROW_Y + i * ROW_H;
        draw_rect(ui, 4, y, CW - 8, ROW_H - 2, i == sel ? 0x5A5A70 : 0x2A2A36);
        char line[48];
        if (page == PG_AVATAR) snprintf(line, sizeof(line), "%s: %s", P_LABELS[i], profile_value(p, i));
        else if (i == 0) snprintf(line, sizeof(line), "NAME: %s", p->name);
        else if (i == 1) snprintf(line, sizeof(line), "USERNAME: %s", p->user);
        else if (i == 2) {
            if (p->month) snprintf(line, sizeof(line), "BIRTHDAY: %02d/%02d", p->month, p->day);
            else snprintf(line, sizeof(line), "BIRTHDAY: NOT SET");
        } else snprintf(line, sizeof(line), "TAKE PROFILE PHOTO");
        draw_text(ui, (CW - text_width(line, 1)) / 2, y + 6, 1, line, 0xFFFFFF);
        if (page == PG_AVATAR) {
            draw_text(ui, 14, y + 6, 1, "<", 0xFFD60A);
            draw_text(ui, CW - 19, y + 6, 1, ">", 0xFFD60A);
        }
    }
}

/* ---------------- camera screen (bottom, touch) ---------------- */

#define HOLD_MS 600                  // hold the shutter this long for a boomerang
#define NAV_Y 214

enum { A_NONE, A_AVATAR, A_SEARCH, A_BELL, A_ADD, A_FLIP, A_FLASH, A_MUSIC, A_MORE,
       A_TIMER, A_TEXT, A_LOOK, A_MIRROR, A_AI, A_MEMORIES,
       A_MAP, A_CHAT, A_SMILE, A_FRIENDS, A_PLAY };      // the last 5 are the bottom bar, in order

static int camera_hit(int x, int y) {
    if (y >= NAV_Y) return A_MAP + (x * 5 / CW > 4 ? 4 : x * 5 / CW);
    if (y >= 148 && x < 58) return A_AI;
    if (y >= 148 && x < 122) return A_MEMORIES;
    return A_NONE;
}

// The top screen cannot be touched: SELECT turns on a yellow highlight, the D-pad
// moves it and A presses the button. This is the order it moves in.
typedef struct { int act, x, y, w, h; } TopButton;
static const TopButton TOP_BUTTONS[] = {
    { A_AVATAR, 4, 3, 32, 32 }, { A_SEARCH, 42, 4, 30, 30 }, { A_BELL, 205, 4, 30, 30 },
    { A_ADD, 241, 4, 34, 30 }, { A_FLIP, 285, 4, 30, 30 }, { A_FLASH, 285, 37, 30, 30 },
    { A_MUSIC, 285, 68, 30, 29 }, { A_MORE, 285, 98, 30, 28 },
    // the next ones only show when the arrow is open
    { A_TIMER, 168, 44, 108, 22 }, { A_TEXT, 168, 72, 108, 22 },
    { A_LOOK, 168, 100, 108, 22 }, { A_MIRROR, 168, 128, 108, 22 }
};
#define TOP_ALWAYS 8
#define TOP_ALL ((int)(sizeof(TOP_BUTTONS) / sizeof(TOP_BUTTONS[0])))

static u16 *pen;                     // the picture the icons are drawn on

// White line with a dark shadow, so icons show on any picture.
static void icon_line(float x0, float y0, float x1, float y1) {
    draw_line(pen, x0 + 1, y0 + 1, x1 + 1, y1 + 1, 2.0f, 0x202020);
    draw_line(pen, x0, y0, x1, y1, 2.0f, 0xFFFFFF);
}

// pts is a list of x,y pairs measured from (cx, cy).
static void icon_path(float cx, float cy, const signed char *pts, int n) {
    for (int i = 0; i + 1 < n; i++)
        icon_line(cx + pts[i * 2], cy + pts[i * 2 + 1], cx + pts[i * 2 + 2], cy + pts[i * 2 + 3]);
}
#define PATH(cx, cy, ...) do { static const signed char q_[] = { __VA_ARGS__ }; \
    icon_path(cx, cy, q_, (int)sizeof(q_) / 2); } while (0)

static void icon_ring(float cx, float cy, float r) {
    float px = cx + r, py = cy;
    for (int i = 1; i <= 16; i++) {
        float a = i * 6.2831853f / 16.0f;
        float x = cx + r * cosf(a), y = cy + r * sinf(a);
        icon_line(px, py, x, y);
        px = x; py = y;
    }
}

static void dim(int x, int y, int w, int h) {         // half dark box
    for (int j = y < 0 ? 0 : y; j < y + h && j < CH; j++)
        for (int i = x < 0 ? 0 : x; i < x + w && i < CW; i++)
            ui[j * CW + i] = (ui[j * CW + i] >> 1) & 0x7BEF;
}

static void red_dot(int x, int y) { draw_ellipse(pen, x, y, 4, 4, 0, 0xFF2D46, 255); }

typedef struct {
    bool flash, more, newMemory, remind, mirror;
    int timerSecs, cursor;
    const char *look;
} CamUi;

// Top screen: profile, search and the buttons on the right (not saved in photos).
static void draw_top_buttons(u16 *img, const CamUi *u) {
    pen = img;
    // top left: profile and search
    draw_avatar(img, 6, 5, 28);
    icon_ring(54, 17, 7);
    icon_line(59, 22, 65, 28);

    // top right: bell
    PATH(220, 19, -8,6, -6,3, -6,-3, -3,-7, 0,-8, 3,-7, 6,-3, 6,3, 8,6, -8,6);
    PATH(220, 19, -2,9, 2,9);
    if (u->remind) red_dot(229, 10);
    // add friend
    icon_ring(256, 13, 5);
    PATH(256, 19, -9,9, -8,4, -4,2, 2,2);
    PATH(266, 24, -4,0, 4,0);
    PATH(266, 24, 0,-4, 0,4);
    // flip camera: two arrows going round
    PATH(300, 19, -9,1, -9,-6, 6,-6);
    PATH(300, 19, 2,-10, 6,-6, 2,-2);
    PATH(300, 19, 9,-1, 9,6, -6,6);
    PATH(300, 19, -2,10, -6,6, -2,2);

    // right column: flash (crossed out when off), music, more
    PATH(300, 52, 2,-11, -6,2, 0,2, -2,11, 6,-2, 0,-2, 2,-11);
    if (!u->flash) icon_line(290, 42, 310, 62);
    else draw_ellipse(img, 300, 52, 3, 4, 0, 0xFFD60A, 255);
    PATH(300, 82, -4,7, -4,-6, 8,-9, 8,4);
    icon_ring(293, 90, 3);
    icon_ring(305, 87, 3);
    if (u->more) PATH(300, 112, -8,4, 0,-4, 8,4);
    else PATH(300, 112, -8,-4, 0,4, 8,-4);

    if (u->more) {                    // extra tools
        char row[4][24];
        if (u->timerSecs) snprintf(row[0], sizeof(row[0]), "TIMER: %dS", u->timerSecs);
        else snprintf(row[0], sizeof(row[0]), "TIMER: OFF");
        snprintf(row[1], sizeof(row[1]), "CAPTION");
        snprintf(row[2], sizeof(row[2]), "%s", u->look);
        snprintf(row[3], sizeof(row[3]), "MIRROR: %s", u->mirror ? "ON" : "OFF");
        for (int i = 0; i < 4; i++)
            pill_text(img, 168, 44 + i * 28, 108, 22, row[i], 0x25252C);
    }

    if (u->cursor >= 0) {             // yellow box round the chosen button
        const TopButton *t = &TOP_BUTTONS[u->cursor];
        draw_rect(img, t->x - 2, t->y - 2, t->w + 4, 2, 0xFFD60A);
        draw_rect(img, t->x - 2, t->y + t->h, t->w + 4, 2, 0xFFD60A);
        draw_rect(img, t->x - 2, t->y, 2, t->h, 0xFFD60A);
        draw_rect(img, t->x + t->w, t->y, 2, t->h, 0xFFD60A);
    }
}

// Bottom screen: AI, Memories, filters, shutter and the bottom bar.
static void draw_camera_ui(const Carousel *car, const CamUi *u) {
    pen = ui;
    const char *hint = u->cursor >= 0 ? "DPAD: MOVE   A: PRESS   B: CLOSE"
                                      : "SELECT: TOP BUTTONS";
    pill_text(ui, (CW - text_width(hint, 1) - 16) / 2, 6, text_width(hint, 1) + 16, 15, hint, 0x101012);

    // bottom left: AI and Memories
    draw_text(ui, 18, 169, 2, "AI", 0x202020);
    draw_text(ui, 17, 168, 2, "AI", 0xFFFFFF);
    PATH(44, 163, -4,0, 4,0);
    PATH(44, 163, 0,-4, 0,4);
    draw_text(ui, 25, 197, 1, "AI", 0xFFFFFF);
    PATH(92, 174, -7,-8, 8,-8, 8,9, -7,9, -7,-8);
    PATH(92, 174, -7,-5, -11,-4, -9,10, -7,10);
    PATH(92, 174, -5,6, 0,0, 3,4, 5,2, 7,5);
    icon_ring(95, 170, 1.5f);
    draw_text(ui, 90 - text_width("MEMORIES", 1) / 2, 197, 1, "MEMORIES", 0xFFFFFF);
    if (u->newMemory) red_dot(104, 164);

    // filter circles: the one in the shutter ring is in use, the next ones sit to the right
    int centre = nearest(car->scroll);
    for (int i = centre - 1; i <= centre + 4; i++) {
        float x = CW / 2 + (i - car->scroll) * CAR_STEP;
        if (x < CW / 2 - 8 || x > CW + 28) continue;
        if (x > CW / 2 + 20) draw_ellipse(ui, x, CAR_Y, 27, 27, 0, 0x3A3A48, 255);
        else draw_ellipse(ui, x, CAR_Y, 27, 27, 0, 0x3A3A48, 150);
        filter_icon_any(ui, wrap(i), nearest(x), CAR_Y);
    }
    // white shutter ring
    for (int y = CAR_Y - 35; y <= CAR_Y + 35; y++)
        for (int x = CW / 2 - 35; x <= CW / 2 + 35; x++) {
            int d = (x - CW / 2) * (x - CW / 2) + (y - CAR_Y) * (y - CAR_Y);
            if (d >= 29 * 29 && d <= 34 * 34) ui[y * CW + x] = 0xFFFF;
        }

    // bottom bar: map, chat, avatar, friends, play
    draw_rect(ui, 0, NAV_Y, CW, CH - NAV_Y, 0x101012);
    icon_ring(32, 224, 6);
    PATH(32, 224, -5,4, 0,11, 5,4);
    icon_ring(32, 224, 1.5f);
    PATH(96, 227, -8,-8, 8,-8, 8,5, 3,5, 8,9, -8,5, -8,-8);
    icon_ring(160, 227, 9);
    PATH(160, 227, -4,2, -2,4, 2,4, 4,2);
    PATH(160, 227, -3,-3, -3,-2);
    PATH(160, 227, 3,-3, 3,-2);
    icon_ring(219, 222, 4);
    icon_ring(231, 222, 4);
    PATH(219, 234, -8,0, -6,-4, 0,-6, 6,-4, 8,0);
    PATH(231, 234, 2,-6, 6,-4, 8,0);
    PATH(288, 227, -6,-9, 9,0, -6,9, -6,-9);
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

    chat_load(&chat, PHOTO_DIR);     // bring back the saved chats

    Tracker tracker;
    tracker_reset(&tracker);
    int filter = F_GLASSES;
    bool autoTrack = true, mirror = true, havePicture = false;
    Carousel carousel = {0};
    carousel.scroll = carousel.target = F_GLASSES;
    float offX = 0, offY = 0, scale = 1.0f;
    Face face = tracker.face;
    char message[64] = "";
    u64 messageUntil = 0, bannerUntil = 0, flashUntil = 0;
    touchPosition touch = {0, 0};
    static u16 clean[CW * CH], shown[CW * CH];

    int grade = G_NONE, timerIdx = 0;
    char caption[24] = "";
    u64 timerAt = 0;
    Stats stats;
    stats_load(&stats);
    srand((unsigned)time(NULL));
    SnapProfile profile;
    profile_load(&profile);
    avatar_build(&profile, mem.img);  // mem.img is free while Memories is closed
    face_load(mem.img);
    int profRow = 0, profPhotos = 0, profBooms = 0, profPage = PG_HOME;
    bool profSeen = false;           // a camera picture arrived on the photo page
    bool flashOn = false, moreTools = false, newMemory = false;
    int cursor = -1;                 // chosen top screen button, -1 = off
    u64 flashShotAt = 0, holdFrom = 0;

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
        if (mode == M_CAMERA && (down & KEY_TOUCH) &&
            camera_hit(touch.px, touch.py) != A_NONE) btnHeld = true;

        if (mode == M_PROFILE) {
            /* ---------- profile ---------- */
            bool tap = (down & KEY_TOUCH) != 0;
            int tx = touch.px, ty = touch.py;

            if (profPage == PG_HOME) {
                bool leave = (down & KEY_B) || (tap && ty < 38 && tx < 40);
                int card = -1;
                if (tap && ty >= CARD_Y && ty < CARD_Y + 3 * (CARD_H + 4))
                    card = (ty - CARD_Y) / (CARD_H + 4);
                if (tap && ty < 38 && tx >= 100 && tx < 220) {            // save card
                    draw_profile_card(photo, &profile, &stats);
                    if (take_photo(message, sizeof(message))) count_saved(&profPhotos, &profBooms);
                } else if (tap && ty < 38 && tx >= 236 && tx < 274) {     // outfit
                    profPage = PG_AVATAR; profRow = 0; message[0] = 0;
                } else if ((tap && ty < 38 && tx >= 276) ||               // gear
                           (tap && ty >= 46 && ty < 74 && tx < 160)) {    // my account
                    profPage = PG_ACCOUNT; profRow = 0; message[0] = 0;
                } else if (tap && ty >= 46 && ty < 74) {                  // profile photo
                    profPage = PG_PHOTO; profSeen = false; message[0] = 0;
                }
                if (card == 0) leave = true;                              // new snap
                if (card == 1) {
                    mem_open(&mem, PHOTO_DIR);
                    mode = M_GALLERY;
                    message[0] = 0;
                }
                if (card == 2) {
                    if (boom_alloc(boom)) {
                        mode = M_BOOM_REC;
                        boomCount = boomSkip = 0;
                        message[0] = 0;
                        btnHeld = true;
                    } else {
                        snprintf(message, sizeof(message), "NOT ENOUGH MEMORY");
                    }
                }
                if (leave) {
                    mode = M_CAMERA;
                    message[0] = 0;
                    btnHeld = true;          // this touch must not move the carousel
                }
                draw_profile_card(shown, &profile, &stats);
                draw_profile_home(&stats, profPhotos, profBooms);
                draw_text(ui, 10, 222, 1, message, 0xFFD60A);

            } else if (profPage == PG_PHOTO) {
                const u16 *picture = cam_poll();
                if (picture) {               // middle square, stretched to fill the BMP
                    for (int y = 0; y < CH; y++)
                        for (int x = 0; x < CW; x++)
                            clean[y * CW + x] = picture[y * CW + (mirror ? CW - 1 - x : x)];
                    havePicture = profSeen = true;
                }
                bool take = (down & (KEY_A | KEY_X)) || (tap && ty >= BTN_Y && tx < CW / 2);
                bool cancel = (down & KEY_B) || (tap && ty >= BTN_Y && tx >= CW / 2);
                if (take && !profSeen) {
                    snprintf(message, sizeof(message), "NO CAMERA PICTURE YET");
                } else if (take) {
                    for (int y = 0; y < CH; y++)
                        for (int x = 0; x < CW; x++)
                            photo[y * CW + x] = clean[y * CW + (CW - CH) / 2 + x * CH / CW];
                    mkdir("sdmc:/3ds", 0777);
                    bool ok = save_bmp(PHOTO_FILE, photo);
                    if (ok) face_load(mem.img);
                    snprintf(message, sizeof(message), ok ? "PHOTO SAVED" : "SAVE FAILED - CHECK SD CARD");
                    profPage = PG_HOME;
                } else if (cancel) {
                    profPage = PG_HOME;
                    message[0] = 0;
                }
                // top screen: live camera with the circle area marked
                memcpy(shown, clean, sizeof(shown));
                for (int y = 0; y < CH; y++)
                    for (int x = 0; x < CW; x++) {
                        int dx = x - CW / 2, dy = y - CH / 2;
                        if (dx * dx + dy * dy > 120 * 120)
                            shown[y * CW + x] = (shown[y * CW + x] >> 1) & 0x7BEF;
                    }
                draw_rect(ui, 0, 0, CW, CH, 0x15151C);
                draw_rect(ui, 0, 0, CW, 34, 0xFFD60A);
                draw_text(ui, (CW - text_width("PROFILE PHOTO", 3)) / 2, 7, 3, "PROFILE PHOTO", 0x15151C);
                draw_text(ui, 10, 80, 1, "PUT YOUR FACE INSIDE THE CIRCLE.", 0xFFFFFF);
                draw_text(ui, 10, 96, 1, "A OR TAKE: USE THIS PHOTO", 0xFFFFFF);
                draw_text(ui, 10, 108, 1, "B OR CANCEL: GO BACK", 0xFFFFFF);
                static const char *const BTN_FACE[] = { "TAKE", "CANCEL" };
                draw_buttons(BTN_FACE, 2);
                draw_text(ui, 10, 226, 1, message, 0xFFD60A);

            } else {                          // avatar page or account page
                int rows = profPage == PG_AVATAR ? P_COUNT : ACC_ROWS;
                int change = 0;
                bool open = false, changed = false;
                if (down & KEY_DUP) profRow = (profRow + rows - 1) % rows;
                if (down & KEY_DDOWN) profRow = (profRow + 1) % rows;
                if (down & KEY_DLEFT) change = -1;
                if (down & (KEY_DRIGHT | KEY_A)) change = 1;
                if (down & KEY_A) open = true;
                if (tap && ty >= ROW_Y && ty < ROW_Y + rows * ROW_H) {
                    profRow = (ty - ROW_Y) / ROW_H;
                    change = tx < CW / 2 ? -1 : 1;        // left half: previous
                    open = true;
                }
                if (profPage == PG_AVATAR && change) {
                    profile.v[profRow] = (profile.v[profRow] + change + P_CHOICES[profRow]) % P_CHOICES[profRow];
                    avatar_build(&profile, mem.img);
                    changed = true;
                    message[0] = 0;
                } else if (profPage == PG_ACCOUNT && open) {
                    message[0] = 0;
                    if (profRow == 0) { ask_text("Your name", profile.name, PROF_NAME_MAX); changed = true; }
                    if (profRow == 1) { ask_text("Username", profile.user, USER_MAX); changed = true; }
                    if (profRow == 2) {
                        if (ask_birthday(&profile)) changed = true;
                        else snprintf(message, sizeof(message), "BAD DATE - TYPE MMDD LIKE 1105");
                    }
                    if (profRow == 3) { profPage = PG_PHOTO; profSeen = false; }
                }
                if (changed)
                    snprintf(message, sizeof(message),
                             profile_save(&profile) ? "SAVED" : "SAVE FAILED - CHECK SD CARD");
                if (profile.v[P_PICTURE] && !pictureOk)
                    snprintf(message, sizeof(message), "NO PICTURE FOUND - SHOWING CARTOON");

                draw_profile_card(shown, &profile, &stats);
                if (profPage != PG_PHOTO) {
                    draw_profile_rows(&profile, profPage, profRow);
                    draw_buttons(BTN_PROF, 1);
                    draw_text(ui, 10, 226, 1, message, 0xFFD60A);
                    if ((down & KEY_B) || (tap && ty >= BTN_Y && ty < BTN_Y + BTN_H)) {
                        profPage = PG_HOME;
                        message[0] = 0;
                    }
                }
            }

        } else if (mode == M_GALLERY) {
            /* ---------- memories (see memories.c) ---------- */
            MemInput mi = { 0 };
            if (down & KEY_A) mi.keys |= MK_A;
            if (down & KEY_B) mi.keys |= MK_B;
            if (down & KEY_X) mi.keys |= MK_X;
            if (down & KEY_L) mi.keys |= MK_L;
            if (down & KEY_R) mi.keys |= MK_R;
            if (down & KEY_DLEFT) mi.keys |= MK_LEFT;
            if (down & KEY_DRIGHT) mi.keys |= MK_RIGHT;
            if (down & KEY_DUP) mi.keys |= MK_UP;
            if (down & KEY_DDOWN) mi.keys |= MK_DOWN;
            mi.down = (down & KEY_TOUCH) != 0;
            mi.held = (held & KEY_TOUCH) != 0;
            mi.up = (hidKeysUp() & KEY_TOUCH) != 0;
            mi.x = touch.px;
            mi.y = touch.py;
            mi.ms = now;
            if (mem_update(&mem, &mi)) {
                mode = M_CAMERA;
                btnHeld = true;          // this touch must not move the carousel
            }
            mem_draw(&mem, shown, ui, now);

        } else if (mode == M_STORIES) {
            /* ---------- stories (see stories.c) ---------- */
            StoryInput si = { 0 };
            if (down & KEY_A) si.keys |= SK_A;
            if (down & KEY_B) si.keys |= SK_B;
            if (down & KEY_DLEFT) si.keys |= SK_LEFT;
            if (down & KEY_DRIGHT) si.keys |= SK_RIGHT;
            si.down = (down & KEY_TOUCH) != 0;
            si.x = touch.px;
            si.y = touch.py;
            si.ms = now;
            if (stories_update(&stories, &si)) {
                mode = M_CAMERA;
                btnHeld = true;          // this touch must not move the carousel
            }
            stories_draw(&stories, shown, ui);

        } else if (mode == M_MAP) {
            /* ---------- map (see snapmap.c) ---------- */
            MapInput pi = { 0 };
            if (down & KEY_A) pi.keys |= PK_A;
            if (down & KEY_B) pi.keys |= PK_B;
            if (down & KEY_X) pi.keys |= PK_X;
            if (down & KEY_Y) pi.keys |= PK_Y;
            if (down & KEY_L) pi.keys |= PK_L;
            if (down & KEY_R) pi.keys |= PK_R;
            pi.down = (down & KEY_TOUCH) != 0;
            pi.held = (held & KEY_TOUCH) != 0;
            pi.x = touch.px;
            pi.y = touch.py;
            // KEY_LEFT etc. cover both the D-pad and the circle pad
            pi.moveX = ((held & KEY_RIGHT) != 0) - ((held & KEY_LEFT) != 0);
            pi.moveY = ((held & KEY_DOWN) != 0) - ((held & KEY_UP) != 0);
            if (map_update(&snapMap, &pi)) {
                mode = M_CAMERA;
                btnHeld = true;          // this touch must not move the carousel
            }
            map_draw(&snapMap, shown, ui, now, draw_avatar);

        } else if (mode == M_CHAT) {
            /* ---------- chat (see chat.c) ---------- */
            ChatInput ci = { 0 };
            if (down & KEY_A) ci.keys |= CK_A;
            if (down & KEY_B) ci.keys |= CK_B;
            if (down & KEY_DUP) ci.keys |= CK_UP;
            if (down & KEY_DDOWN) ci.keys |= CK_DOWN;
            if (down & KEY_DLEFT) ci.keys |= CK_LEFT;
            if (down & KEY_DRIGHT) ci.keys |= CK_RIGHT;
            if (down & KEY_X) ci.keys |= CK_X;
            ci.down = (down & KEY_TOUCH) != 0;
            ci.x = touch.px;
            ci.y = touch.py;
            ci.ms = now;
            int cr = chat_update(&chat, &ci);
            if (cr == CR_KEYBOARD) {
                char text[CHAT_TEXT];
                if (ask_chat("Send a chat", text, sizeof(text))) chat_send(&chat, text, osGetTime());
            } else if (cr == CR_ASK_NAME || cr == CR_ASK_FOOD) {     // friend maker
                char text[FRIEND_FOOD + 1];
                int max = cr == CR_ASK_NAME ? FRIEND_NAME : FRIEND_FOOD;
                if (ask_chat(cr == CR_ASK_NAME ? "Friend's name (up to 10)" : "Favorite food (up to 20)",
                             text, (size_t)max + 1))
                    chat_set_text(&chat, cr, text);
            } else if (cr == CR_STORIES) {
                stories_open(&stories, PHOTO_DIR, time(NULL));
                mode = M_STORIES;
            } else if (cr == CR_MAP) {
                map_open(&snapMap, PHOTO_DIR);
                mode = M_MAP;
            } else if (cr == CR_EXIT) {
                mode = M_CAMERA;
                btnHeld = true;          // this touch must not move the carousel
            }
            chat_draw(&chat, shown, ui, now);

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

            // buttons on the camera screen
            int act = A_NONE;
            if (mode == M_CAMERA && (down & KEY_TOUCH)) {
                act = camera_hit(touch.px, touch.py);
                if (act != A_NONE) { btnHeld = true; tapShutter = false; }
            } else if (mode == M_BOOM_REC && btn == 0) {          // cancel recording
                boom_free(boom);
                mode = M_CAMERA;
            }

            // top screen buttons: SELECT, D-pad and A
            bool menuWasOn = cursor >= 0;
            if (mode != M_CAMERA) cursor = -1;
            else if (down & KEY_SELECT) cursor = menuWasOn ? -1 : 0;
            else if (cursor >= 0) {
                int n = moreTools ? TOP_ALL : TOP_ALWAYS;
                if (down & (KEY_DRIGHT | KEY_DDOWN)) cursor = (cursor + 1) % n;
                if (down & (KEY_DLEFT | KEY_DUP)) cursor = (cursor + n - 1) % n;
                if (down & KEY_B) cursor = -1;
                else if (down & KEY_A) act = TOP_BUTTONS[cursor].act;
            }
            bool menu = menuWasOn || cursor >= 0;   // these keys belong to the menu now
            const char *note = NULL;     // short message to show
            bool startBoom = false;
            switch (act) {
            case A_SEARCH: {             // find a filter by name
                char want[20] = "";
                ask_text("Search filters", want, sizeof(want) - 1);
                if (want[0]) {
                    int found = -1;
                    for (int n = 1; n <= ALL_FILTERS && found < 0; n++)
                        if (strstr(filter_name_any(wrap(filter + n)), want)) found = wrap(filter + n);
                    if (found >= 0) {
                        carousel.target += wrap(found - filter);
                        carousel.scroll = carousel.target;
                        filter = found;
                        bannerUntil = now + 1200;
                    } else note = "NO FILTER WITH THAT NAME";
                }
                break;
            }
            case A_BELL:
                if (!stats.score) note = "TAKE YOUR FIRST SNAP!";
                else if (stats.lastDay == (unsigned long)(time(NULL) / 86400)) note = "YOU ALREADY SNAPPED TODAY!";
                else note = stats_streak(&stats) ? "SNAP TODAY TO KEEP YOUR STREAK!" : "SNAP TO START A NEW STREAK!";
                break;
            case A_FLIP:
                timerAt = flashShotAt = 0;
                if (cam_flip()) {
                    mirror = (camSelect == SELECT_IN1);   // selfie camera mirrors
                    tracker_reset(&tracker);
                    face = tracker.face;
                    note = camSelect == SELECT_IN1 ? "SELFIE CAMERA" : "OUTER CAMERA";
                } else note = "FLIP FAILED";
                break;
            case A_FLASH:
                flashOn = !flashOn;
                note = flashOn ? "SCREEN FLASH ON" : "SCREEN FLASH OFF";
                break;
            case A_MORE:  moreTools = !moreTools; break;
            case A_TIMER: timerIdx = (timerIdx + 1) % 4; timerAt = 0; break;
            case A_TEXT:  ask_caption(caption, sizeof(caption)); break;
            case A_LOOK:  grade = (grade + 1) % G_COUNT; break;
            case A_MIRROR: mirror = !mirror; break;
            case A_AVATAR:               // my profile
                timerAt = flashShotAt = 0;
                count_saved(&profPhotos, &profBooms);
                profPage = PG_HOME;
                profRow = 0;
                message[0] = 0;
                messageUntil = 0;
                mode = M_PROFILE;
                break;
            case A_AI:                   // no real AI on the 3DS: picks a random filter
                carousel.target += 1 + rand() % (ALL_FILTERS - 1);
                carousel.scroll = carousel.target;
                filter = wrap(carousel.target);
                bannerUntil = now + 1200;
                note = "SURPRISE FILTER!";
                break;
            case A_MEMORIES:
                timerAt = flashShotAt = 0;
                newMemory = false;
                mem_open(&mem, PHOTO_DIR);
                mode = M_GALLERY;
                break;
            case A_SMILE:                // avatar editor
                timerAt = flashShotAt = 0;
                count_saved(&profPhotos, &profBooms);
                profPage = PG_AVATAR;
                profRow = 0;
                message[0] = 0;
                messageUntil = 0;
                mode = M_PROFILE;
                break;
            case A_PLAY:  startBoom = true; break;
            case A_ADD:     note = "ADD FRIENDS NEEDS ONLINE - NOT IN APP"; break;
            case A_MUSIC:   note = "MUSIC IS NOT IN THIS APP YET"; break;
            case A_MAP:
                timerAt = flashShotAt = 0;
                map_open(&snapMap, PHOTO_DIR);
                mode = M_MAP;
                break;
            case A_CHAT:                 // fake chat screen
                timerAt = flashShotAt = 0;
                chat_open(&chat);
                mode = M_CHAT;
                break;
            case A_FRIENDS:              // AI friends (part of the chat screen)
                timerAt = flashShotAt = 0;
                chat_open_friends(&chat);
                mode = M_CHAT;
                break;
            }

            if (mode != M_CAMERA) cursor = -1;   // leaving the camera closes the menu

            // hold the shutter ring to record a boomerang
            if (mode == M_CAMERA && carousel.touching && !carousel.dragged &&
                abs(carousel.startX - CW / 2) <= 36 && abs(carousel.startY - CAR_Y) <= 36) {
                if (!holdFrom) holdFrom = now;
                else if (now - holdFrom >= HOLD_MS) {
                    startBoom = true;
                    carousel.touching = false;   // letting go must not also take a photo
                    btnHeld = true;
                }
            } else holdFrom = 0;

            if (startBoom && mode == M_CAMERA) {
                timerAt = flashShotAt = 0;
                holdFrom = 0;
                if (boom_alloc(boom)) {
                    mode = M_BOOM_REC;
                    boomCount = 0;
                    boomSkip = 0;
                } else note = "NOT ENOUGH MEMORY";
            }
            if (note) {
                snprintf(message, sizeof(message), "%s", note);
                messageUntil = now + 2500;
            }

            if (down & KEY_Y) {
                autoTrack = !autoTrack;  // locking keeps the filter where it is
            }
            if ((down & KEY_B) && !menu) {
                if (mode == M_BOOM_REC) {
                    boom_free(boom);
                    mode = M_CAMERA;
                } else {
                    offX = offY = 0; scale = 1.0f; autoTrack = true;
                    tracker_reset(&tracker);
                    face = tracker.face;
                }
            }

            // looks, timer, caption
            if (!menu && (down & KEY_DRIGHT)) { grade = (grade + 1) % G_COUNT; }
            if (!menu && (down & KEY_DLEFT))  { grade = (grade + G_COUNT - 1) % G_COUNT; }
            if (down & KEY_ZL) timerIdx = (timerIdx + 1) % 4;
            if (mode == M_CAMERA && (down & KEY_ZR)) ask_caption(caption, sizeof(caption));

            circlePosition pad;
            hidCircleRead(&pad);
            if (abs(pad.dx) > 25) offX += pad.dx / 40.0f;
            if (abs(pad.dy) > 25) offY -= pad.dy / 40.0f;
            if (!menu && (held & KEY_DUP)) scale *= 1.02f;
            if (!menu && (held & KEY_DDOWN)) scale /= 1.02f;
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
            filter_draw_any(photo, filter, placed, !menu && (held & KEY_A) != 0, (now % 600000) / 1000.0f);
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
                if (fire && flashOn && !flashShotAt) {   // light up the screens first
                    flashShotAt = now + 700;
                    fire = false;
                }
                if (flashShotAt && now >= flashShotAt) { flashShotAt = 0; fire = true; }

                if (fire) {
                    if (havePicture) {
                        if (take_photo(message, sizeof(message))) {
                            map_add_snap(PHOTO_DIR, lastPhoto);   // pin it where you are
                            stats_snap(&stats);
                            newMemory = true;
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

            // bottom screen: live camera with the buttons on top
            memcpy(ui, photo, sizeof(ui));
            if (mode == M_CAMERA) {
                CamUi cu = { flashOn, moreTools, newMemory,
                             stats.lastDay != (unsigned long)(time(NULL) / 86400), mirror,
                             TIMER_SECS[timerIdx], cursor,
                             grade ? GRADE_NAMES[grade] : "LOOK: NORMAL" };
                draw_top_buttons(shown, &cu);
                draw_camera_ui(&carousel, &cu);
                const char *label = message[0] ? message :    // one label at a time
                                    now < bannerUntil ? filter_name_any(filter) : NULL;
                if (label) {
                    int w = text_width(label, 1) + 16;
                    pill_text(ui, (CW - w) / 2, 127, w, 15, label, 0x101012);
                }
            } else if (mode == M_BOOM_REC) {
                dim(0, 0, CW, CH);
                draw_text(ui, (CW - text_width("RECORDING BOOMERANG", 2)) / 2, 100, 2,
                          "RECORDING BOOMERANG", 0xFFFFFF);
                draw_buttons(BTN_REC, 1);
            }
            if (flashShotAt) {           // white screens light up your face
                draw_rect(shown, 0, 0, CW, CH, 0xFFFFFF);
                draw_rect(ui, 0, 0, CW, CH, 0xFFFFFF);
            }
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
