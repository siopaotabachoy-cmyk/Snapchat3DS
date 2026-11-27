// extra.c - the extra face filters for Snap3DS: sprite filters (pictures from sprites.h)
// and the BEAUTY looks, plus the circle pictures for the carousel.
#include <math.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "extra.h"
#include "sprites.h"        // only included here, so the picture data exists once
#include "faces.h"          // real face photos for the carousel pictures (made by gen_faces.py)

// Draws sprite `sp` so that its point (ax, ay) lands on screen at (cx, cy).
// s = screen pixels per sprite pixel. Everything is clipped to the picture.
// Each screen pixel averages 4 samples so shrinking stays smooth.
static void draw_sprite(uint16_t *c, const Sprite *sp, float cx, float cy,
                        float ax, float ay, float s) {
    if (!(s > 0.02f && s < 20.0f)) return;               // also rejects NaN
    float left = cx - ax * s, top = cy - ay * s;
    float right = left + sp->w * s, bottom = top + sp->h * s;
    int x0 = (int)floorf(left),  x1 = (int)ceilf(right);
    int y0 = (int)floorf(top),   y1 = (int)ceilf(bottom);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > CW) x1 = CW;
    if (y1 > CH) y1 = CH;
    static const float off[4][2] = { {0.25f, 0.25f}, {0.75f, 0.25f}, {0.25f, 0.75f}, {0.75f, 0.75f} };
    float inv = 1.0f / s;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            int ar = 0, ag = 0, ab = 0, aa = 0;           // sums over the 4 samples
            for (int k = 0; k < 4; k++) {
                int u = (int)floorf((x + off[k][0] - left) * inv);
                int v = (int)floorf((y + off[k][1] - top) * inv);
                if (u < 0 || v < 0 || u >= sp->w || v >= sp->h) continue;
                int a = sp->a[v * sp->w + u];
                if (!a) continue;
                uint16_t p = sp->rgb[v * sp->w + u];
                ar += (p >> 11) * a;
                ag += ((p >> 5) & 63) * a;
                ab += (p & 31) * a;
                aa += a;
            }
            if (!aa) continue;
            int sr = ar / aa, sg = ag / aa, sb = ab / aa;  // average colour
            int alpha = aa >> 2;                           // average opacity 0..255
            uint16_t *d = &c[y * CW + x];
            if (alpha >= 250) {
                *d = (uint16_t)((sr << 11) | (sg << 5) | sb);
            } else {
                int dr = *d >> 11, dg = (*d >> 5) & 63, db = *d & 31;
                dr += ((sr - dr) * alpha) / 255;
                dg += ((sg - dg) * alpha) / 255;
                db += ((sb - db) * alpha) / 255;
                *d = (uint16_t)((dr << 11) | (dg << 5) | db);
            }
        }
    }
}

// Put a sprite on the face: width = fraction of the face width, centre offset
// by (u, v) face widths from the face centre.
static void on_face(uint16_t *c, const Sprite *sp, Face f, float width, float u, float v) {
    float s = (width * f.w) / sp->w;
    draw_sprite(c, sp, f.x + u * f.w, f.y + v * f.w, sp->w * 0.5f, sp->h * 0.5f, s);
}

static void draw_kitty(uint16_t *c, Face f) {
    // the yellow nose of the picture sits on the nose; whiskers span the cheeks
    float s = (1.25f * f.w) / KITTY.w;
    draw_sprite(c, &KITTY, f.x, f.y + 0.14f * f.w, KITTY.cx, KITTY.cy, s);
}

static void draw_strawberry(uint16_t *c, Face f) {
    // The hat's see-through hole (the sprite's anchor) goes around the face. The hole
    // is about 70% of the hat's width, so the hat is ~1.45 face widths wide.
    float s = (1.45f * f.w) / STRAWBERRY.w;
    draw_sprite(c, &STRAWBERRY, f.x, f.y - 0.06f * f.w, STRAWBERRY.cx, STRAWBERRY.cy, s);
}

// Dog pieces: the nose goes on the nose, the tongue hangs under it (only while A is
// held), and the ears sit on the top corners of the head. All pieces share one scale,
// so the nose is about a third of the face width.
static void draw_dog(uint16_t *c, const SpriteDog *d, Face f, bool tongue) {
    float s = (0.34f * f.w) / d->nose.w;
    float nx = f.x, ny = f.y + 0.14f * f.w;
    if (tongue)
        draw_sprite(c, &d->tongue, nx, ny + 0.05f * f.w, d->tongue.cx, d->tongue.cy, s);
    draw_sprite(c, &d->nose, nx, ny, d->nose.cx, d->nose.cy, s);
    float ex = 0.46f * f.w, ey = f.y - 0.50f * f.w;      // ear centres
    draw_sprite(c, &d->ear_l, f.x - ex, ey, d->ear_l.w * 0.5f, d->ear_l.h * 0.5f, s);
    draw_sprite(c, &d->ear_r, f.x + ex, ey, d->ear_r.w * 0.5f, d->ear_r.h * 0.5f, s);
}

/* ---------------- BEAUTY looks ----------------
   Each look is just a row of numbers (0..100). draw_beauty() does the work:
   1. smooth the skin (blur, but only where the colour barely changes, so eyes,
      brows and lips stay sharp), 2. glow + warm/cool tint, 3. cheek highlight,
      blush, freckles, 4. lip colour + gloss, 5. brighter eyes + lash line.
   Everything is placed from the face centre and width, like the other filters. */

#define PIF 3.14159265f

typedef struct {
    uint8_t smooth, glow;        // skin smoothing, brightness
    int8_t  warm;                // +warm / -cool tint
    uint32_t blushRGB; uint8_t blush, noseBlush;
    uint32_t lipRGB;   uint8_t lip, gloss;
    uint8_t eyes, highlight, freckles;
} Look;

static const char *const LOOK_NAMES[X_COUNT] = { "NATURAL", "GLAM", "PEACHY", "DEWY", "SUNKISSED" };

static const Look LOOKS[X_COUNT] = {
    //  smooth glow warm  blush colour, amt, nose   lip colour, amt, gloss  eyes hilite freckles
    {   55,    30,   0,   0xE8707C,  25,  0,       0xD0707A,   25,   0,      0,   0,    0 },  // NATURAL
    {   80,    55,  10,   0xE85A6C,  55,  1,       0xC9606C,   60,  55,     70,   0,    0 },  // GLAM
    {   70,    45,  40,   0xFF8A6A,  55,  1,       0xF2806E,   45,  20,      0,   0,    0 },  // PEACHY
    {   85,    70, -30,   0xF07CA0,  30,  0,       0xE0708F,   40,  80,     40,  60,    0 },  // DEWY
    {   45,    35,  70,   0xE0794F,  40,  1,       0xB8606A,   35,  15,      0,   0,   70 },  // SUNKISSED
};
_Static_assert(sizeof(LOOK_NAMES) / sizeof(LOOK_NAMES[0]) == X_COUNT, "LOOK_NAMES must match X_COUNT");
_Static_assert(sizeof(LOOKS) / sizeof(LOOKS[0]) == X_COUNT, "LOOKS must match X_COUNT");

static inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static inline uint16_t rgb565(int r, int g, int b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
static inline void unpack565(uint16_t p, int *r, int *g, int *b) {
    int r5 = p >> 11, g6 = (p >> 5) & 63, b5 = p & 31;
    *r = (r5 << 3) | (r5 >> 2);
    *g = (g6 << 2) | (g6 >> 4);
    *b = (b5 << 3) | (b5 >> 2);
}
// a = 0..256
static inline void mix_px(uint16_t *d, uint32_t rgb, int a) {
    if (a <= 0) return;
    if (a > 256) a = 256;
    int r, g, b;
    unpack565(*d, &r, &g, &b);
    r += ((int)((rgb >> 16) & 255) - r) * a / 256;
    g += ((int)((rgb >> 8) & 255) - g) * a / 256;
    b += ((int)(rgb & 255) - b) * a / 256;
    *d = rgb565(r, g, b);
}

// Soft coloured ellipse. plateau=false: fades out smoothly (blush, glow).
// plateau=true: mostly solid with a soft edge (lips, eyes, dots).
static void spot(uint16_t *c, float cx, float cy, float rx, float ry,
                 uint32_t rgb, int a, bool plateau, int minLum) {
    if (a <= 0 || rx < 0.5f || ry < 0.5f) return;
    int x0 = (int)floorf(cx - rx), x1 = (int)ceilf(cx + rx);
    int y0 = (int)floorf(cy - ry), y1 = (int)ceilf(cy + ry);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > CW - 1) x1 = CW - 1;
    if (y1 > CH - 1) y1 = CH - 1;
    float ix = 1.0f / rx, iy = 1.0f / ry;
    for (int y = y0; y <= y1; y++) {
        float dy = (y - cy) * iy, dy2 = dy * dy;
        if (dy2 >= 1.0f) continue;
        for (int x = x0; x <= x1; x++) {
            float dx = (x - cx) * ix, d2 = dx * dx + dy2;
            if (d2 >= 1.0f) continue;
            float f = 1.0f - d2;
            f = plateau ? (f * 3.0f > 1.0f ? 1.0f : f * 3.0f) : f * f;
            if (minLum) {                              // only brighten light pixels (eye whites)
                int pr, pg, pb;
                unpack565(c[y * CW + x], &pr, &pg, &pb);
                if (((77 * pr + 150 * pg + 29 * pb) >> 8) < minLum) continue;
            }
            mix_px(&c[y * CW + x], rgb, (int)(a * f));
        }
    }
}

static uint16_t bt_tmp[CW * CH], bt_blur[CW * CH];   // blur work pictures

// Skin smoothing + glow + tint, inside an oval around the face.
static void smooth_glow(uint16_t *c, Face f, const Look *L) {
    float cx = f.x, cy = f.y + 0.02f * f.w, rx = 0.52f * f.w, ry = 0.64f * f.w;
    int R = (int)(f.w / 36.0f);                       // blur size follows the face size
    if (R < 1) R = 1;
    if (R > 4) R = 4;
    int x0 = (int)floorf(cx - rx), x1 = (int)ceilf(cx + rx);
    int y0 = (int)floorf(cy - ry), y1 = (int)ceilf(cy + ry);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > CW - 1) x1 = CW - 1;
    if (y1 > CH - 1) y1 = CH - 1;
    if (x0 > x1 || y0 > y1) return;

    int K = 2 * R + 1, inv = (65536 + K - 1) / K;      // multiply instead of divide
    int ya = y0 - R < 0 ? 0 : y0 - R, yb = y1 + R > CH - 1 ? CH - 1 : y1 + R;

    if (L->smooth) {
        for (int y = ya; y <= yb; y++) {               // sideways blur
            const uint16_t *row = &c[y * CW];
            uint16_t *out = &bt_tmp[y * CW];
            int sr = 0, sg = 0, sb = 0;
            for (int k = -R; k <= R; k++) {
                int xx = x0 + k;
                xx = xx < 0 ? 0 : xx > CW - 1 ? CW - 1 : xx;
                uint16_t p = row[xx];
                sr += p >> 11; sg += (p >> 5) & 63; sb += p & 31;
            }
            for (int x = x0; x <= x1; x++) {
                out[x] = (uint16_t)((((sr * inv) >> 16) << 11) | (((sg * inv) >> 16) << 5) | ((sb * inv) >> 16));
                int a = x + R + 1, d = x - R;
                a = a > CW - 1 ? CW - 1 : a;
                d = d < 0 ? 0 : d;
                uint16_t pa = row[a], pd = row[d];
                sr += (pa >> 11) - (pd >> 11);
                sg += ((pa >> 5) & 63) - ((pd >> 5) & 63);
                sb += (pa & 31) - (pd & 31);
            }
        }
        for (int x = x0; x <= x1; x++) {               // up-down blur
            int sr = 0, sg = 0, sb = 0;
            for (int k = -R; k <= R; k++) {
                int yy = y0 + k;
                yy = yy < 0 ? 0 : yy > CH - 1 ? CH - 1 : yy;
                uint16_t p = bt_tmp[yy * CW + x];
                sr += p >> 11; sg += (p >> 5) & 63; sb += p & 31;
            }
            for (int y = y0; y <= y1; y++) {
                bt_blur[y * CW + x] = (uint16_t)((((sr * inv) >> 16) << 11) | (((sg * inv) >> 16) << 5) | ((sb * inv) >> 16));
                int a = y + R + 1, d = y - R;
                a = a > CH - 1 ? CH - 1 : a;
                d = d < 0 ? 0 : d;
                uint16_t pa = bt_tmp[a * CW + x], pd = bt_tmp[d * CW + x];
                sr += (pa >> 11) - (pd >> 11);
                sg += ((pa >> 5) & 63) - ((pd >> 5) & 63);
                sb += (pa & 31) - (pd & 31);
            }
        }
    }

    int sm = L->smooth * 256 / 100, gl = L->glow * 256 / 100;
    float ix = 1.0f / rx, iy = 1.0f / ry;
    for (int y = y0; y <= y1; y++) {
        float dy = (y - cy) * iy, dy2 = dy * dy;
        if (dy2 >= 1.0f) continue;
        for (int x = x0; x <= x1; x++) {
            float dx = (x - cx) * ix, d2 = dx * dx + dy2;
            if (d2 >= 1.0f) continue;
            int m = d2 <= 0.5f ? 256 : (int)((1.0f - d2) * 512.0f);   // soft oval edge
            int i = y * CW + x, pr, pg, pb;
            unpack565(c[i], &pr, &pg, &pb);
            if (sm) {
                int qr, qg, qb;
                unpack565(bt_blur[i], &qr, &qg, &qb);
                int lo = (77 * pr + 150 * pg + 29 * pb) >> 8;
                int lb = (77 * qr + 150 * qg + 29 * qb) >> 8;
                int diff = lo > lb ? lo - lb : lb - lo;
                if (diff < 55) {                       // edges (diff big) are kept
                    int k = ((55 - diff) * 298) >> 6;
                    int t = (((k * m) >> 8) * sm) >> 8;
                    pr += (qr - pr) * t / 256;
                    pg += (qg - pg) * t / 256;
                    pb += (qb - pb) * t / 256;
                }
            }
            int g = (m * gl) >> 8;                     // glow 0..256
            pr += g * 22 / 256; pg += g * 15 / 256; pb += g * 12 / 256;
            int tw = m * L->warm / 256;                // tint -100..100
            pr += tw * 12 / 100; pg += tw * 3 / 100; pb -= tw * 12 / 100;
            c[i] = rgb565(clamp255(pr), clamp255(pg), clamp255(pb));
        }
    }
}

static const float FRECKLES[][2] = {                   // (side, down) in face widths
    {-0.05f, 0.08f}, {0.04f, 0.10f}, {0.00f, 0.13f}, {-0.10f, 0.12f}, {0.10f, 0.13f},
    {-0.15f, 0.15f}, {0.15f, 0.16f}, {-0.20f, 0.12f}, {0.20f, 0.12f}, {-0.08f, 0.17f},
    {0.08f, 0.18f}, {-0.22f, 0.18f}, {0.22f, 0.17f}, {0.02f, 0.05f}, {-0.26f, 0.15f}, {0.26f, 0.14f}
};

static void draw_beauty(uint16_t *c, Face f, const Look *L) {
    float w = f.w;
    smooth_glow(c, f, L);

    if (L->highlight) {                                // shiny cheekbones + nose bridge
        spot(c, f.x - 0.25f * w, f.y + 0.06f * w, 0.09f * w, 0.06f * w, 0xFFFFFF, L->highlight * 2, false, 0);
        spot(c, f.x + 0.25f * w, f.y + 0.06f * w, 0.09f * w, 0.06f * w, 0xFFFFFF, L->highlight * 2, false, 0);
        spot(c, f.x, f.y + 0.03f * w, 0.035f * w, 0.08f * w, 0xFFFFFF, L->highlight * 3 / 2, false, 0);
    }
    if (L->blush) {                                    // pink cheeks (+ nose)
        spot(c, f.x - 0.27f * w, f.y + 0.15f * w, 0.20f * w, 0.17f * w, L->blushRGB, L->blush * 2, false, 0);
        spot(c, f.x + 0.27f * w, f.y + 0.15f * w, 0.20f * w, 0.17f * w, L->blushRGB, L->blush * 2, false, 0);
        if (L->noseBlush)
            spot(c, f.x, f.y + 0.13f * w, 0.10f * w, 0.06f * w, L->blushRGB, L->blush * 6 / 5, false, 0);
    }
    if (L->freckles) {
        float r = 0.011f * w < 0.9f ? 0.9f : 0.011f * w;
        for (unsigned i = 0; i < sizeof(FRECKLES) / sizeof(FRECKLES[0]); i++)
            spot(c, f.x + FRECKLES[i][0] * w, f.y + FRECKLES[i][1] * w, r, r, 0xA0623F, L->freckles * 2, true, 0);
    }
    if (L->lip)                                        // lip colour
        spot(c, f.x, f.y + 0.335f * w, 0.14f * w, 0.05f * w, L->lipRGB, L->lip * 2, true, 0);
    if (L->gloss)                                      // shine on the lower lip
        spot(c, f.x - 0.04f * w, f.y + 0.32f * w, 0.045f * w, 0.014f * w, 0xFFFFFF, L->gloss * 2, true, 0);

    if (L->eyes) {
        float ey = f.y - 0.06f * w, ex = 0.20f * w, er = 0.075f * w;
        float thick = 0.018f * w < 1.5f ? 1.5f : 0.018f * w;
        float cr = 0.014f * w < 1.0f ? 1.0f : 0.014f * w;
        for (int s = -1; s <= 1; s += 2) {
            float x = f.x + s * ex;
            spot(c, x, ey, er * 0.9f, 0.032f * w, 0xFFFFFF, L->eyes, true, 205);              // whiter eyes (light pixels only)
            spot(c, x - 0.012f * w, ey - 0.01f * w, cr, cr, 0xFFFFFF, L->eyes * 3, true, 0);      // sparkle
            if (L->eyes >= 50) {                                                               // lash line + wing
                float outer = x + s * er, inner = x - s * er, px = outer, py = ey - 0.006f * w;
                for (int k = 1; k <= 4; k++) {
                    float t = k / 4.0f;
                    float nx = outer + (inner - outer) * t;
                    float ny = ey - 0.006f * w - 0.032f * w * sinf(PIF * t);
                    draw_line(c, px, py, nx, ny, thick, 0x1A1210);
                    px = nx; py = ny;
                }
                draw_line(c, outer, ey - 0.006f * w, outer + s * 0.04f * w, ey - 0.035f * w, thick, 0x1A1210);
            }
        }
    }
}

/* ---------------- lens pictures (the circle pictures in the carousel) ----------------
   Like Snapchat: each circle shows a face with that filter already on it.
   Real photos come from faces.h (made by gen_faces.py). With no photos, the
   BEAUTY filters show a drawn face and the old filters keep their old icons. */

#define LENS_PX 52                  // circle picture size on the bottom screen
#define LENS_SRC FACE_PX            // photo size we draw the filter on (then shrink 2x)
_Static_assert(LENS_SRC == LENS_PX * 2, "FACE_PX must be 2 x LENS_PX");

static uint16_t lens[ALL_FILTERS][LENS_PX * LENS_PX];
static bool lensOk[ALL_FILTERS], lensBuilt;
static uint16_t scratch[CW * CH];

#if FACE_PHOTO_COUNT == 0
// A drawn face for when there is no photo. Same proportions the tracker gives.
static Face cartoon_face(uint16_t *c) {
    draw_rect(c, 0, 0, LENS_SRC, LENS_SRC, 0xC9B3D1);
    draw_ellipse(c, 52, 118, 42, 24, 0, 0x4A5A78, 255);          // shoulders
    draw_rect(c, 44, 82, 16, 18, 0xC48E72);                      // neck
    draw_ellipse(c, 52, 46, 36, 42, 0, 0x2B1D16, 255);           // hair
    draw_ellipse(c, 23, 56, 4, 7, 0, 0xDBA98C, 255);
    draw_ellipse(c, 81, 56, 4, 7, 0, 0xDBA98C, 255);
    draw_ellipse(c, 52, 55, 29, 35, 0, 0xE0AC8C, 255);           // face
    draw_ellipse(c, 52, 24, 30, 9, 0, 0x2B1D16, 255);            // fringe
    for (int s = -1; s <= 1; s += 2) {
        float x = 52 + s * 11.6f;
        draw_ellipse(c, x, 50, 4.6f, 2.8f, 0, 0xF4F0EC, 255);
        draw_ellipse(c, x, 50, 2.4f, 2.4f, 0, 0x3B2A1F, 255);
        draw_ellipse(c, x, 50, 1.0f, 1.0f, 0, 0x0E0A08, 255);
        draw_line(c, x - 5, 43, x + 5, 42, 1.6f, 0x2B1D16);
    }
    draw_line(c, 52, 52, 51, 61, 1.2f, 0xB07860);
    draw_ellipse(c, 52, 73, 8, 3, 0, 0xB5545A, 255);
    return (Face){ 52.0f, 53.5f, 58.0f };
}
#endif

static void build_lens(int i) {
    Face f;
#if FACE_PHOTO_COUNT > 0
    const FacePhoto *ph = &FACE_PHOTOS[i % FACE_PHOTO_COUNT];
    for (int y = 0; y < LENS_SRC; y++)
        for (int x = 0; x < LENS_SRC; x++)
            scratch[y * CW + x] = ph->rgb[y * LENS_SRC + x];
    f = (Face){ ph->fx, ph->fy, ph->fw };
#else
    if (i < F_COUNT) return;                                     // keep the old icon
    f = cartoon_face(scratch);
#endif
    filter_draw_any(scratch, i, f, true, 0.0f);
    for (int ty = 0; ty < LENS_PX; ty++)
        for (int tx = 0; tx < LENS_PX; tx++) {
            int r = 0, g = 0, b = 0;
            for (int k = 0; k < 4; k++) {
                int pr, pg, pb;
                unpack565(scratch[(ty * 2 + (k >> 1)) * CW + tx * 2 + (k & 1)], &pr, &pg, &pb);
                r += pr; g += pg; b += pb;
            }
            lens[i][ty * LENS_PX + tx] = rgb565(r >> 2, g >> 2, b >> 2);
        }
    lensOk[i] = true;
}

static void lens_build_all(void) {
    lensBuilt = true;
    for (int i = 0; i < ALL_FILTERS; i++) build_lens(i);
}

static void blit_lens(uint16_t *c, int i, int cx, int cy) {
    const uint16_t *t = lens[i];
    int half = LENS_PX / 2;
    for (int ty = 0; ty < LENS_PX; ty++) {
        int y = cy - half + ty;
        if (y < 0 || y >= CH) continue;
        int dy = 2 * ty + 1 - LENS_PX;
        for (int tx = 0; tx < LENS_PX; tx++) {
            int x = cx - half + tx;
            if (x < 0 || x >= CW) continue;
            int dx = 2 * tx + 1 - LENS_PX;
            if (dx * dx + dy * dy <= LENS_PX * LENS_PX)             // circle shape
                c[y * CW + x] = t[ty * LENS_PX + tx];
        }
    }
}

/* ---------------- public functions (see extra.h) ---------------- */

const char *filter_name_any(int i) {
    if (i >= 0 && i < F_COUNT) return FILTER_NAMES[i] ? FILTER_NAMES[i] : "";
    if (i >= F_COUNT && i < ALL_FILTERS) return LOOK_NAMES[i - F_COUNT];
    return "";
}

void filter_draw_any(uint16_t *c, int i, Face f, bool action, float time) {
    (void)time;
    switch (i) {
    case F_GLASSES:    on_face(c, &GLASSES, f, 1.00f, 0.0f, -0.06f); break;
    case F_KITTY:      draw_kitty(c, f); break;
    case F_MUSTACHE:   on_face(c, &MUSTACHE, f, 0.62f, 0.0f, 0.24f); break;
    case F_STRAWBERRY: draw_strawberry(c, f); break;
    case F_DALMATIAN:  draw_dog(c, &DALMATIAN, f, action); break;
    case F_PUPPY:      draw_dog(c, &PUPPY, f, action); break;
    default:
        if (i >= F_COUNT && i < ALL_FILTERS) draw_beauty(c, f, &LOOKS[i - F_COUNT]);
        break;
    }
}

// Picture for the carousel circle (fits inside radius ~26).
void filter_icon_any(uint16_t *c, int i, int cx, int cy) {
    if (!lensBuilt) lens_build_all();                // first time only
    if (i >= 0 && i < ALL_FILTERS && lensOk[i]) { blit_lens(c, i, cx, cy); return; }

    const Sprite *sp = NULL;
    float width = 46.0f;
    switch (i) {
    case F_GLASSES:  sp = &GLASSES; break;
    case F_KITTY:    sp = &KITTY; width = 50.0f; break;
    case F_MUSTACHE: sp = &MUSTACHE; width = 42.0f; break;
    case F_STRAWBERRY: sp = &STRAWBERRY; width = 40.0f; break;
    case F_DALMATIAN:
    case F_PUPPY: {
        Face f = { (float)cx, cy - 0.05f * 32.0f, 32.0f };
        draw_dog(c, i == F_DALMATIAN ? &DALMATIAN : &PUPPY, f, true);
        return;
    }
    default: return;
    }
    draw_sprite(c, sp, (float)cx, (float)cy, sp->w * 0.5f, sp->h * 0.5f, width / sp->w);
}
