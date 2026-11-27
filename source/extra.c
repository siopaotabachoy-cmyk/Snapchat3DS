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

// The looks were first drawn for a small cartoon face. Real faces are wider and their
// eyes, lips and cheeks sit further apart, so everything is stretched with these two knobs.
// Too small on your face -> raise them.  Too big -> lower them.  (1.0 = old size)
#define BEAUTY_W 1.30f   // sideways: spacing and width of eyes, lips, cheeks
#define BEAUTY_H 1.15f   // up-down: height of the features (their position stays the same)

typedef struct {
    uint8_t smooth, glow;        // skin smoothing, brightness
    int8_t  warm;                // +warm / -cool tint
    uint32_t blushRGB; uint8_t blush, noseBlush;
    uint32_t lipRGB;   uint8_t lip, gloss;
    uint8_t eyes, highlight, freckles;
    uint8_t lash;                // soft natural lashes (0 = none). GLAM uses the bold lash line from `eyes` instead
} Look;

static const char *const LOOK_NAMES[X_COUNT] = {
    "NATURAL", "GLAM", "PEACHY", "DEWY", "SUNKISSED",
    "SOFT", "GLASS", "ROSY", "BERRY", "MATTE", "GOLDEN", "FRESH", "DOLLY", "MOCHA", "FROST"
};

static const Look LOOKS[X_COUNT] = {
    //  smooth glow warm  blush colour, amt, nose   lip colour, amt, gloss  eyes hilite freckles lash
    {   75,    30,   0,   0xE8707C,  25,  0,       0xD0707A,   25,   0,      0,   0,    0 ,  55 },  // NATURAL
    {   80,    55,   6,   0xE85A6C,  50,  1,       0xC9606C,   60,  55,     70,   0,    0 ,   0 },  // GLAM
    {   70,    45,  22,   0xF08072,  40,  1,       0xF2806E,   45,  20,      0,   0,    0 ,  60 },  // PEACHY
    {   85,    70, -15,   0xF07CA0,  30,  0,       0xE0708F,   40,  80,     40,  60,    0 ,  50 },  // DEWY
    {   60,    35,  22,   0xD9788A,  28,  0,       0xB8606A,   35,  15,      0,   0,   45 ,  45 },  // SUNKISSED
    // ---- more looks (all use the soft natural lashes) ----
    {   70,    45,   0,   0xF08C9C,  35,  0,       0xE0808C,   35,  25,     25,  20,    0 ,  50 },  // SOFT      everyday soft pink
    {   90,    65, -10,   0xF2A0A8,  20,  0,       0xE8909A,   30,  90,     20,  55,    0 ,  45 },  // GLASS     glass skin: dewy shine, light colour
    {   65,    35,   5,   0xE86A82,  60,  1,       0xD05A72,   45,  20,      0,   0,    0 ,  55 },  // ROSY      strong rosy cheeks
    {   70,    30, -10,   0xD0607A,  30,  0,       0xA02848,   70,  30,      0,   0,    0 ,  60 },  // BERRY     berry lips
    {   80,    10,   0,   0xD88080,  25,  0,       0xB87078,   40,   0,      0,   0,    0 ,  50 },  // MATTE     no shine, nude lips
    {   60,    50,  30,   0xE08A7A,  30,  0,       0xC86E68,   35,  30,     15,  30,   25 ,  45 },  // GOLDEN    warm glow, few freckles
    {   55,    30,  -5,   0xF0909A,  20,  0,       0xDC8A92,   20,  40,     30,   0,    0 ,  40 },  // FRESH     barely-there makeup
    {   75,    55,   0,   0xF07088,  55,  1,       0xE06880,   50,  50,     45,  25,    0 ,  60 },  // DOLLY     round pink cheeks, bright eyes
    {   65,    35,  10,   0xB85A5A,  35,  0,       0x9A4A4E,   45,  30,     10,  35,    0 ,  50 },  // MOCHA     deeper tones
    {   80,    60, -40,   0xE88CA8,  25,  0,       0xD0708C,   35,  50,     35,  40,    0 ,  50 },  // FROST     cool, pale glow
};
_Static_assert(sizeof(LOOK_NAMES) / sizeof(LOOK_NAMES[0]) == X_COUNT, "LOOK_NAMES must match X_COUNT");
_Static_assert(sizeof(LOOKS) / sizeof(LOOKS[0]) == X_COUNT, "LOOKS must match X_COUNT");

/* ---------------- MIXED filters: a face effect + a beauty look together ----------------
   The beauty look is drawn first (smoothing, glow, blush, lips, lashes), then the
   sprite (dog ears, cat whiskers, hat...) goes on top so it stays sharp.
   To make your own mix, add a row here and raise C_COUNT in extra.h:
   { "NAME", effect (F_...), look (LK_...) } */
enum { LK_NATURAL, LK_GLAM, LK_PEACHY, LK_DEWY, LK_SUNKISSED, LK_SOFT, LK_GLASS, LK_ROSY,
       LK_BERRY, LK_MATTE, LK_GOLDEN, LK_FRESH, LK_DOLLY, LK_MOCHA, LK_FROST };

typedef struct { const char *name; uint8_t effect, look; } Combo;

static const Combo COMBOS[C_COUNT] = {
    { "SOFT PUPPY",      F_PUPPY,      LK_SOFT   },
    { "DOLLY PUPPY",     F_PUPPY,      LK_DOLLY  },
    { "SNOW PUPPY",      F_PUPPY,      LK_FROST  },
    { "GOLDEN PUPPY",    F_PUPPY,      LK_GOLDEN },
    { "SOFT DALMATIAN",  F_DALMATIAN,  LK_SOFT   },
    { "ROSY DALMATIAN",  F_DALMATIAN,  LK_ROSY   },
    { "GLASS DALMATIAN", F_DALMATIAN,  LK_GLASS  },
    { "MOCHA DALMATIAN", F_DALMATIAN,  LK_MOCHA  },
    { "ROSY KITTY",      F_KITTY,      LK_ROSY   },
    { "GLAM KITTY",      F_KITTY,      LK_GLAM   },
    { "SNOW KITTY",      F_KITTY,      LK_FROST  },
    { "DEWY KITTY",      F_KITTY,      LK_DEWY   },
    { "SWEET BERRY",     F_STRAWBERRY, LK_SOFT   },
    { "BERRY GIRL",      F_STRAWBERRY, LK_ROSY   },
    { "COOL GLOW",       F_GLASSES,    LK_GOLDEN },
    { "COOL MATTE",      F_GLASSES,    LK_MATTE  },
    { "MR FRESH",        F_MUSTACHE,   LK_FRESH  },
};
_Static_assert(sizeof(COMBOS) / sizeof(COMBOS[0]) == C_COUNT, "COMBOS must match C_COUNT");

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

static inline int skin_weight(int r, int g, int b);
static bool g_skin_only;   // when true, spot() only colours skin (cheeks must not spill onto hair or the wall)

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
            int aa = (int)(a * f);
            if (g_skin_only) {
                int pr, pg, pb;
                unpack565(c[y * CW + x], &pr, &pg, &pb);
                aa = aa * skin_weight(pr, pg, pb) >> 8;
            }
            mix_px(&c[y * CW + x], rgb, aa);
        }
    }
}

static uint16_t bt_tmp[CW * CH], bt_blur[CW * CH];   // blur work pictures

// 0..256: how much a pixel is part of eyes, brows, nose tip or lips (these stay sharp)
static inline int keep_sharp(float x, float y, Face f) {
    static const float P[4][4] = {                     // x offset, y offset, half width, half height (face widths)
        { 0.20f, -0.07f, 0.12f, 0.060f }, { -0.20f, -0.07f, 0.12f, 0.060f },
        { 0.00f,  0.335f, 0.17f, 0.065f }, { 0.00f, 0.14f, 0.07f, 0.040f } };
    float w = f.w;
    if (y < f.y - 0.19f * w || (y > f.y + 0.20f * w && y < f.y + 0.25f * w) || y > f.y + 0.43f * w) return 0;
    int best = 0;
    for (int i = 0; i < 4; i++) {
        float dx = (x - f.x - P[i][0] * w * BEAUTY_W) / (P[i][2] * w * BEAUTY_W);
        float dy = (y - f.y - P[i][1] * w) / (P[i][3] * w * BEAUTY_H);
        float d2 = dx * dx + dy * dy;
        int k = d2 <= 0.6f ? 256 : d2 >= 1.0f ? 0 : (int)((1.0f - d2) * 640.0f);
        if (k > best) best = k;
    }
    return best;
}

// 0..256: how much a pixel looks like skin (soft edges). Hair, background and dark areas
// get little or none, so the smoothing can cover the whole face without blurring them.
static inline int skin_weight(int r, int g, int b) {
    int lum = (77 * r + 150 * g + 29 * b) >> 8;
    int wl = lum <= 45 ? 0 : lum >= 80 ? 256 : (lum - 45) * 256 / 35;
    int cb = 128 + ((-43 * r - 85 * g + 128 * b) >> 8);
    int cr = 128 + ((128 * r - 107 * g - 21 * b) >> 8);
    int dcb = cb < 75 ? 75 - cb : cb > 132 ? cb - 132 : 0;
    int dcr = cr < 132 ? 132 - cr : cr > 180 ? cr - 180 : 0;
    int d = dcb > dcr ? dcb : dcr;
    int wc = d >= 12 ? 0 : 256 - d * 256 / 12;
    return wl * wc >> 8;
}

// Skin smoothing + glow + tint, inside an oval around the face.
static void smooth_glow(uint16_t *c, Face f, const Look *L) {
    float cx = f.x, cy = f.y + 0.02f * f.w, rx = 0.62f * f.w, ry = 0.74f * f.w;
    int R = (int)(f.w / 28.0f);                       // blur size follows the face size
    if (R < 2) R = 2;
    if (R > 5) R = 5;
    int x0 = (int)floorf(cx - rx), x1 = (int)ceilf(cx + rx);
    int y0 = (int)floorf(cy - ry), y1 = (int)ceilf(cy + ry);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > CW - 1) x1 = CW - 1;
    if (y1 > CH - 1) y1 = CH - 1;
    if (x0 > x1 || y0 > y1) return;

    int K = 2 * R + 1, inv = (65536 + K - 1) / K;      // multiply instead of divide
#define AVG(sum) (((sum) * inv + 32768) >> 16)       // rounded, so the blur does not tint the skin
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
                out[x] = (uint16_t)((AVG(sr) << 11) | (AVG(sg) << 5) | AVG(sb));
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
                bt_blur[y * CW + x] = (uint16_t)((AVG(sr) << 11) | (AVG(sg) << 5) | AVG(sb));
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
    const int EDGE = 45;                               // luminance jump that counts as a real edge
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
            m = m * skin_weight(pr, pg, pb) >> 8;      // only skin gets smoothed, glowed and tinted
            if (!m) continue;
            if (sm) {
                int qr, qg, qb;
                unpack565(bt_blur[i], &qr, &qg, &qb);
                int lo = (77 * pr + 150 * pg + 29 * pb) >> 8;
                int lb = (77 * qr + 150 * qg + 29 * qb) >> 8;
                int diff = lo > lb ? lo - lb : lb - lo;
                if (diff < EDGE) {                     // real edges (brows, lips, hair, nostrils) are kept
                    int k = (EDGE - diff) * 256 / EDGE;  // flat skin 256 ... edge 0
                    int keep = keep_sharp((float)x, (float)y, f);
                    int t = (((k * m) >> 8) * sm) >> 8;
                    if (t > 205) t = 205;              // never 100%: keeps a little skin texture (not plastic)
                    t = t * (256 - keep) >> 8;
                    pr += (qr - pr) * t / 256;
                    pg += (qg - pg) * t / 256;
                    pb += (qb - pb) * t / 256;
                }
            }
            int g = (m * gl) >> 8;                     // glow 0..256: neutral brightening, no colour cast
            pr += (255 - pr) * g * 10 / (256 * 100);   // 10% of the way to white at most,
            pg += (255 - pg) * g * 10 / (256 * 100);   // and less on already-bright skin
            pb += (255 - pb) * g * 10 / (256 * 100);
            int tw = m * L->warm / 256;                // tint -100..100 (kept small: skin turns orange fast)
            pr += tw * 5 / 100; pg += tw * 1 / 100; pb -= tw * 5 / 100;
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
    float w = f.w, wx = w * BEAUTY_W, wy = w * BEAUTY_H;   // wx: sideways sizes, wy: up-down sizes
    smooth_glow(c, f, L);

    g_skin_only = true;                                // highlights, blush and freckles stay on the skin
    if (L->highlight) {                                // shiny cheekbones + nose bridge
        spot(c, f.x - 0.25f * wx, f.y + 0.06f * w, 0.09f * wx, 0.06f * wy, 0xFFFFFF, L->highlight, false, 0);
        spot(c, f.x + 0.25f * wx, f.y + 0.06f * w, 0.09f * wx, 0.06f * wy, 0xFFFFFF, L->highlight, false, 0);
        spot(c, f.x, f.y + 0.03f * w, 0.035f * wx, 0.08f * wy, 0xFFFFFF, L->highlight * 2 / 3, false, 0);
    }
    if (L->blush) {                                    // pink cheeks (+ nose)
        spot(c, f.x - 0.22f * wx, f.y + 0.15f * w, 0.16f * wx, 0.17f * wy, L->blushRGB, L->blush * 3 / 2, false, 0);
        spot(c, f.x + 0.22f * wx, f.y + 0.15f * w, 0.16f * wx, 0.17f * wy, L->blushRGB, L->blush * 3 / 2, false, 0);
        if (L->noseBlush)
            spot(c, f.x, f.y + 0.13f * w, 0.10f * wx, 0.06f * wy, L->blushRGB, L->blush * 6 / 5, false, 0);
    }
    if (L->freckles) {
        float r = 0.011f * wx < 0.9f ? 0.9f : 0.011f * wx;
        for (unsigned i = 0; i < sizeof(FRECKLES) / sizeof(FRECKLES[0]); i++)
            spot(c, f.x + FRECKLES[i][0] * wx, f.y + FRECKLES[i][1] * w, r, r, 0xB07455, L->freckles * 2, true, 0);
    }
    g_skin_only = false;
    if (L->lip)                                        // lip colour
        spot(c, f.x, f.y + 0.335f * w, 0.14f * wx, 0.05f * wy, L->lipRGB, L->lip * 2, true, 0);
    if (L->gloss)                                      // shine on the lower lip
        spot(c, f.x - 0.04f * wx, f.y + 0.32f * w, 0.045f * wx, 0.014f * wy, 0xFFFFFF, L->gloss * 2, true, 0);

    if (L->eyes || L->lash) {
        float ey = f.y - 0.06f * w, ex = 0.20f * wx, er = 0.075f * wx;
        float thick = 0.018f * wx < 1.5f ? 1.5f : 0.018f * wx;
        float cr = 0.014f * wx < 1.0f ? 1.0f : 0.014f * wx;
        for (int s = -1; s <= 1; s += 2) {
            float x = f.x + s * ex;
            if (L->eyes) {
                spot(c, x, ey, er * 0.9f, 0.032f * wy, 0xFFFFFF, L->eyes, true, 205);             // whiter eyes (light pixels only)
                spot(c, x - 0.012f * wx, ey - 0.01f * wy, cr, cr, 0xFFFFFF, L->eyes * 3, true, 0);    // sparkle
            }
            if (L->eyes >= 50) {                                                               // bold lash line + wing (GLAM)
                float outer = x + s * er, inner = x - s * er, px = outer, py = ey - 0.006f * wy;
                for (int k = 1; k <= 4; k++) {
                    float t = k / 4.0f;
                    float nx = outer + (inner - outer) * t;
                    float ny = ey - 0.006f * wy - 0.032f * wy * sinf(PIF * t);
                    draw_line(c, px, py, nx, ny, thick, 0x1A1210);
                    px = nx; py = ny;
                }
                draw_line(c, outer, ey - 0.006f * wy, outer + s * 0.04f * wx, ey - 0.035f * wy, thick, 0x1A1210);
            } else if (L->lash) {                                                              // natural lashes: thin, soft
                float inner = x - s * er, outer = x + s * er;
                float r0 = 0.0055f * wx < 0.85f ? 0.85f : 0.0055f * wx;
                int a0 = L->lash * 4 > 255 ? 255 : L->lash * 4;                                // opacity (about 70-85% at the defaults)
                for (int k = 0; k <= 20; k++) {                                                // thin line along the upper lid,
                    float u = k / 20.0f;                                                        // a bit darker toward the outer corner
                    float lx = inner + (outer - inner) * u;
                    float ly = ey - 0.006f * wy - 0.030f * wy * sinf(PIF * u);
                    draw_ellipse(c, lx, ly, r0, r0, 0, 0x2B1D18, a0 * (60 + 40 * k / 20) / 100);           // dots overlap, so the line ends up fairly solid but thin
                }
                for (int k = 0; k < 3; k++) {                                                  // 3 tiny lashes at the outer corner
                    float u = 0.62f + 0.17f * k;
                    float lx = inner + (outer - inner) * u;
                    float ly = ey - 0.006f * wy - 0.030f * wy * sinf(PIF * u);
                    float len = (0.026f - 0.003f * k) * wy;
                    for (int j = 1; j <= 3; j++) {
                        float t = j / 3.0f;
                        draw_ellipse(c, lx + s * len * 0.45f * t, ly - len * t, r0 * (1.0f - 0.3f * t), r0 * (1.0f - 0.3f * t),
                                     0, 0x2B1D18, a0 * (100 - 35 * j) / 140);
                    }
                }
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
static bool lensOk[ALL_FILTERS], lensTried[ALL_FILTERS];
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
    if (i >= F_COUNT && i < F_COUNT + X_COUNT) return LOOK_NAMES[i - F_COUNT];
    if (i >= F_COUNT + X_COUNT && i < ALL_FILTERS) return COMBOS[i - F_COUNT - X_COUNT].name;
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
        if (i >= F_COUNT && i < F_COUNT + X_COUNT) {
            draw_beauty(c, f, &LOOKS[i - F_COUNT]);
        } else if (i >= F_COUNT + X_COUNT && i < ALL_FILTERS) {      // mixed: beauty first, effect on top
            const Combo *m = &COMBOS[i - F_COUNT - X_COUNT];
            draw_beauty(c, f, &LOOKS[m->look]);
            filter_draw_any(c, m->effect, f, action, time);
        }
        break;
    }
}

// Picture for the carousel circle (fits inside radius ~26).
void filter_icon_any(uint16_t *c, int i, int cx, int cy) {
    if (i >= 0 && i < ALL_FILTERS && !lensTried[i]) { lensTried[i] = true; build_lens(i); }   // each picture is made once, when first shown
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
