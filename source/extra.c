// extra.c - extra face filters: CROWN, SHADES, CAT, DEVIL, GENTLEMAN, LOVE,
//                              HIPSTER, CLAY MASK, BEAR
#include <math.h>
#include <string.h>
#include "extra.h"

#define PI2 6.2831853f

static const char *const XNAMES[X_COUNT] = {
    "CROWN", "SHADES", "CAT", "DEVIL", "GENTLEMAN", "LOVE",
    "HIPSTER", "CLAY MASK", "BEAR"
};

/* ---------------- small helpers ---------------- */

static void ell(uint16_t *c, float x, float y, float rx, float ry, uint32_t rgb) {
    draw_ellipse(c, x, y, rx, ry, 0.0f, rgb, 255);
}
static void ella(uint16_t *c, float x, float y, float rx, float ry, uint32_t rgb, int a) {
    draw_ellipse(c, x, y, rx, ry, 0.0f, rgb, a);
}

// Filled triangle: sweep lines from the apex across the opposite edge.
static void tri(uint16_t *c, float ax, float ay, float bx, float by,
                float cx, float cy, uint32_t rgb) {
    float dx = cx - bx, dy = cy - by;
    int n = (int)(sqrtf(dx * dx + dy * dy) / 1.5f) + 2;
    if (n > 90) n = 90;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / (float)n;
        draw_line(c, ax, ay, bx + dx * t, by + dy * t, 2.5f, rgb);
    }
}

// Four-point sparkle.
static void star(uint16_t *c, float x, float y, float r) {
    if (r < 1.0f) return;
    draw_line(c, x - r, y, x + r, y, 1.8f, 0xFFFFFF);
    draw_line(c, x, y - r, x, y + r, 1.8f, 0xFFFFFF);
}

// Heart; s is half of its width.
static void heart(uint16_t *c, float x, float y, float s, uint32_t rgb) {
    ell(c, x - 0.5f * s, y - 0.25f * s, 0.5f * s, 0.5f * s, rgb);
    ell(c, x + 0.5f * s, y - 0.25f * s, 0.5f * s, 0.5f * s, rgb);
    tri(c, x, y + s, x - 0.95f * s, y - 0.05f * s, x + 0.95f * s, y - 0.05f * s, rgb);
}

/* ---------------- the filters (f.x, f.y = face centre, f.w = face width) ---------------- */

static void crown(uint16_t *c, Face f, bool action, float t) {
    float w = f.w, x = f.x, base = f.y - 0.60f * w;
    const uint32_t gold = 0xFFC83D, shade = 0xD79A1E;
    for (int i = 0; i < 5; i++) {
        float px = x + (i - 2) * 0.17f * w;
        float top = base - ((i & 1) ? 0.20f : 0.30f) * w;
        tri(c, px, top, px - 0.09f * w, base, px + 0.09f * w, base, gold);
        ell(c, px, top, 0.04f * w, 0.04f * w, (i & 1) ? 0x30D158 : 0xFF3B30);
    }
    draw_rect(c, (int)(x - 0.43f * w), (int)base, (int)(0.86f * w), (int)(0.12f * w), gold);
    draw_rect(c, (int)(x - 0.43f * w), (int)(base + 0.09f * w), (int)(0.86f * w),
              (int)(0.03f * w) + 1, shade);
    for (int i = 0; i < 3; i++)
        ell(c, x + (i - 1) * 0.24f * w, base + 0.06f * w, 0.025f * w, 0.025f * w, 0x0A84FF);

    int n = action ? 8 : 3;          // twinkles (more when A is held)
    for (int i = 0; i < n; i++) {
        float a = i * (PI2 / n) + t;
        float ph = t * 5.0f + i * 1.7f;
        star(c, x + cosf(a) * 0.55f * w, base - 0.12f * w + sinf(a) * 0.25f * w,
             0.06f * w * (0.5f + 0.5f * sinf(ph)));
    }
}

static void shades(uint16_t *c, Face f, bool action, float t) {
    float w = f.w, x = f.x, y = f.y - 0.07f * w;
    float dx = 0.2f * w, rx = 0.17f * w, ry = 0.12f * w;
    static const uint32_t cols[4] = { 0xFF3B7A, 0x30D158, 0x0A84FF, 0xFFD60A };
    uint32_t lens = action ? cols[(int)(t * 6.0f) & 3] : 0x16161C;
    int alpha = action ? 215 : 240;

    for (int s = -1; s <= 1; s += 2) {   // arms
        draw_line(c, x + s * (dx + rx), y - 0.01f * w, x + s * (dx + rx + 0.18f * w),
                  y - 0.04f * w, 3.0f, 0x000000);
    }
    draw_line(c, x - dx + rx * 0.7f, y - 0.03f * w, x + dx - rx * 0.7f, y - 0.03f * w,
              3.5f, 0x000000);          // bridge
    for (int s = -1; s <= 1; s += 2) {
        ell(c, x + s * dx, y, rx + 2, ry + 2, 0x000000);
        ella(c, x + s * dx, y, rx, ry, lens, alpha);
        ella(c, x + s * dx - 0.06f * w, y - 0.05f * w, 0.06f * w, 0.018f * w, 0xFFFFFF, 140);
    }
}

static void cat(uint16_t *c, Face f, bool action, float t) {
    float w = f.w, x = f.x, y = f.y;
    float tw = action ? 0.025f * w * sinf(t * 25.0f) : 0.0f;   // ears twitch
    for (int s = -1; s <= 1; s += 2) {
        tri(c, x + s * 0.46f * w + tw * s, y - 0.88f * w,
               x + s * 0.52f * w, y - 0.40f * w, x + s * 0.16f * w, y - 0.62f * w, 0x6B5B53);
        tri(c, x + s * 0.44f * w + tw * s, y - 0.74f * w,
               x + s * 0.43f * w, y - 0.47f * w, x + s * 0.24f * w, y - 0.60f * w, 0xFF9FB5);
        for (int k = 0; k < 3; k++)
            draw_line(c, x + s * 0.20f * w, y + (0.12f + k * 0.035f) * w,
                         x + s * 0.62f * w, y + (0.04f + k * 0.09f) * w, 1.8f, 0xFFFFFF);
    }
    ell(c, x, y + 0.11f * w, 0.05f * w, 0.035f * w, 0xFF7A9A);
}

static void devil(uint16_t *c, Face f, bool action, float t) {
    float w = f.w, x = f.x, y = f.y;
    for (int s = -1; s <= 1; s += 2) {
        tri(c, x + s * 0.50f * w, y - 0.88f * w,
               x + s * 0.40f * w, y - 0.45f * w, x + s * 0.16f * w, y - 0.58f * w, 0xE0201B);
        draw_line(c, x + s * 0.32f * w, y - 0.18f * w, x + s * 0.08f * w, y - 0.10f * w,
                  4.0f, 0x2A0A0C);       // angry eyebrows
        if (action) {                    // flames from the horn tips
            for (int i = 0; i < 5; i++) {
                float ph = fmodf(t * 1.3f + i * 0.2f, 1.0f);
                float fx = x + s * (0.50f + 0.05f * sinf(t * 9.0f + i * 2.0f)) * w;
                float fy = y - 0.88f * w - ph * 0.35f * w;
                float r = (1.0f - ph) * 0.05f * w + 1.0f;
                ell(c, fx, fy, r, r * 1.3f, ph < 0.5f ? 0xFFD60A : 0xFF6A00);
            }
        }
    }
}

static void gentleman(uint16_t *c, Face f, bool action, float t) {
    float w = f.w, x = f.x, y = f.y;
    float lift = action ? 0.10f * w * fabsf(sinf(t * 12.0f)) : 0.0f;   // hat tip
    draw_rect(c, (int)(x - 0.27f * w), (int)(y - 1.08f * w - lift), (int)(0.54f * w),
              (int)(0.42f * w), 0x111111);
    draw_rect(c, (int)(x - 0.27f * w), (int)(y - 0.76f * w - lift), (int)(0.54f * w),
              (int)(0.07f * w) + 1, 0xC0262D);
    ell(c, x, y - 0.66f * w - lift, 0.44f * w, 0.055f * w, 0x111111);

    const uint32_t hair = 0x2B1B12;      // moustache
    for (int s = -1; s <= 1; s += 2) {
        ell(c, x + s * 0.115f * w, y + 0.18f * w, 0.125f * w, 0.04f * w, hair);
        ell(c, x + s * 0.27f * w, y + 0.145f * w, 0.05f * w, 0.05f * w, hair);
    }
    ell(c, x, y + 0.175f * w, 0.05f * w, 0.04f * w, hair);
}

static void love(uint16_t *c, Face f, bool action, float t) {
    float w = f.w, x = f.x, y = f.y;
    for (int s = -1; s <= 1; s += 2)     // blush
        ella(c, x + s * 0.30f * w, y + 0.14f * w, 0.10f * w, 0.06f * w, 0xFF6F91, 120);
    int n = action ? 10 : 6;             // hearts circling the head
    for (int i = 0; i < n; i++) {
        float a = t * 1.2f + i * (PI2 / n);
        float r = 0.72f * w + 0.08f * w * sinf(t * 2.0f + i);
        float hs = 0.08f * w * (0.8f + 0.3f * sinf(t * 3.0f + i));
        heart(c, x + cosf(a) * r, y - 0.10f * w + sinf(a) * r * 0.6f, hs,
              (i & 1) ? 0xFF6FA5 : 0xFF2D6F);
    }
}


// Same number every time for the same n: gives fixed "random" spots.
static float rnd(int n) {
    unsigned h = (unsigned)n * 2654435761u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    return (float)(h & 0xFFFF) / 65535.0f;
}

// Thick curved stroke through points, getting thinner towards the end.
static void stroke(uint16_t *c, const float *px, const float *py, int n,
                   float t0, float t1, uint32_t rgb) {
    for (int i = 0; i + 1 < n; i++) {
        float th = t0 + (t1 - t0) * (float)i / (float)(n - 1);
        draw_line(c, px[i], py[i], px[i + 1], py[i + 1], th, rgb);
        ell(c, px[i + 1], py[i + 1], th * 0.5f, th * 0.5f, rgb);
    }
}

// Put back the original picture inside an ellipse (cuts a hole in a mask).
static void restore(uint16_t *c, const uint16_t *src, float cx, float cy, float rx, float ry) {
    int x0 = (int)(cx - rx), x1 = (int)(cx + rx) + 1;
    int y0 = (int)(cy - ry), y1 = (int)(cy + ry) + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > CW) x1 = CW;
    if (y1 > CH) y1 = CH;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float u = (x - cx) / rx, v = (y - cy) / ry;
            if (u * u + v * v <= 1.0f) c[y * CW + x] = src[y * CW + x];
        }
}

static void plumeria(uint16_t *c, float x, float y, float r) {
    for (int k = 0; k < 5; k++) {
        float a = k * (PI2 / 5.0f) - 1.5708f;
        float px = x + cosf(a) * 0.55f * r, py = y + sinf(a) * 0.55f * r;
        ell(c, px, py, 0.50f * r, 0.50f * r, 0xFF8FB5);
        ell(c, px - cosf(a) * 0.1f * r, py - sinf(a) * 0.1f * r, 0.32f * r, 0.32f * r, 0xFFC0D4);
    }
    ell(c, x, y, 0.28f * r, 0.28f * r, 0xFFB347);
    ell(c, x, y, 0.14f * r, 0.14f * r, 0xFFE08A);
}

static const float MX[9] = { 0.00f, 0.10f, 0.20f, 0.29f, 0.36f, 0.40f, 0.39f, 0.35f, 0.33f };
static const float MY[9] = { 0.19f, 0.215f, 0.23f, 0.22f, 0.17f, 0.11f, 0.06f, 0.05f, 0.08f };

static void hipster(uint16_t *c, Face f, bool action, float t) {
    float w = f.w, x = f.x, y = f.y - 0.06f * w;
    float dx = 0.22f * w, rx = 0.20f * w, ry = 0.15f * w;
    for (int s = -1; s <= 1; s += 2)     // arms
        draw_line(c, x + s * (dx + rx), y - 0.02f * w, x + s * (dx + rx + 0.12f * w),
                  y - 0.06f * w, 4.0f, 0x000000);
    draw_line(c, x - dx + rx * 0.8f, y - 0.05f * w, x + dx - rx * 0.8f, y - 0.05f * w,
              5.0f, 0x000000);           // bridge
    for (int s = -1; s <= 1; s += 2) {
        ell(c, x + s * dx, y, rx + 0.03f * w, ry + 0.03f * w, 0x000000);   // thick frame
        ella(c, x + s * dx, y, rx, ry, 0x2B3A5C, 225);
        ella(c, x + s * dx, y - 0.05f * w, rx * 0.8f, ry * 0.45f, 0x7FA6E0, 110); // sky shine
        ella(c, x + s * dx - 0.08f * w, y - 0.08f * w, 0.05f * w, 0.02f * w, 0xFFFFFF, 150);
    }
    // big curly moustache
    float bob = action ? 0.015f * w * sinf(t * 14.0f) : 0.0f;
    for (int s = -1; s <= 1; s += 2) {
        float px[9], py[9], hy[9];
        for (int i = 0; i < 9; i++) {
            px[i] = x + s * MX[i] * w;
            py[i] = f.y + MY[i] * w + (i > 4 ? bob * (float)(i - 4) : 0.0f);
            hy[i] = py[i] - 0.015f * w;
        }
        stroke(c, px, py, 9, 0.085f * w, 0.025f * w, 0x4A2A18);
        stroke(c, px, hy, 9, 0.030f * w, 0.010f * w, 0x7A4A2C);   // shine
    }
}

static void clay(uint16_t *c, Face f, bool action, float t) {
    static uint16_t backup[CW * CH];
    static const uint32_t cols[4] = { 0x9FAEE6, 0xF2A5C0, 0x9FD8B5, 0xE8D08A };
    float w = f.w, x = f.x, y = f.y;
    uint32_t col = action ? cols[(int)(t * 2.0f) & 3] : cols[0];   // A: changes colour

    memcpy(backup, c, sizeof(backup));
    ella(c, x, y + 0.02f * w, 0.47f * w, 0.62f * w, col, 238);
    for (int i = 0; i < 45; i++) {       // clay texture
        float px = x + (rnd(i * 2) - 0.5f) * 0.9f * w;
        float py = y + 0.02f * w + (rnd(i * 2 + 1) - 0.5f) * 1.2f * w;
        float u = (px - x) / (0.44f * w), v = (py - y - 0.02f * w) / (0.60f * w);
        if (u * u + v * v > 1.0f) continue;
        float r = (0.010f + 0.015f * rnd(i + 99)) * w;
        if (r < 1.2f) r = 1.2f;
        if (i & 1) ella(c, px, py, r, r, 0xFFFFFF, 80);
        else       ella(c, px, py, r, r, 0x6F7FC0, 70);
    }
    for (int s = -1; s <= 1; s += 2)     // holes: eyes, nose, lips
        restore(c, backup, x + s * 0.20f * w, y - 0.07f * w, 0.15f * w, 0.085f * w);
    restore(c, backup, x, y + 0.13f * w, 0.09f * w, 0.065f * w);
    restore(c, backup, x, y + 0.31f * w, 0.13f * w, 0.065f * w);
    plumeria(c, x - 0.52f * w, y - 0.42f * w, 0.16f * w);
}

static void bear(uint16_t *c, Face f, bool action, float t) {
    float w = f.w, x = f.x, y = f.y;
    for (int s = -1; s <= 1; s += 2) {   // ears
        ell(c, x + s * 0.44f * w, y - 0.60f * w, 0.16f * w, 0.16f * w, 0x8A5A3C);
        ell(c, x + s * 0.44f * w, y - 0.60f * w, 0.095f * w, 0.095f * w, 0x4B2E1E);
    }
    for (int s = -1; s <= 1; s += 2)     // pink cheeks
        ella(c, x + s * 0.33f * w, y + 0.10f * w, 0.16f * w, 0.14f * w, 0xFF8FA0, 150);
    ella(c, x, y - 0.45f * w, 0.07f * w, 0.04f * w, 0xFFFFFF, 120);   // forehead shine
    for (int i = 0; i < 50; i++) {       // glitter that twinkles
        float px = x + (rnd(i * 2) - 0.5f) * 0.9f * w;
        float py = y - 0.05f * w + (rnd(i * 2 + 1) - 0.5f) * 1.1f * w;
        float u = (px - x) / (0.46f * w), v = (py - y) / (0.62f * w);
        float mu = (px - x) / (0.30f * w), mv = (py - y - 0.22f * w) / (0.20f * w);
        if (u * u + v * v > 1.0f || mu * mu + mv * mv < 1.0f) continue;
        float r = (0.008f + 0.012f * rnd(i + 500)) * w;
        if (r < 1.2f) r = 1.2f;
        int a = (int)(110.0f + 140.0f * (0.5f + 0.5f * sinf(t * 4.0f + i * 1.9f)));
        ella(c, px, py, r, r, (i % 3 == 0) ? 0xFFFFFF : 0xFFE2B0, a);
    }
    ell(c, x, y + 0.22f * w, 0.30f * w, 0.20f * w, 0x7A4A2C);          // muzzle
    ell(c, x, y + 0.12f * w, 0.11f * w, 0.075f * w, 0x3A2418);         // nose
    draw_line(c, x, y + 0.14f * w, x, y + 0.29f * w, 0.03f * w + 1.0f, 0x3A2418);
    if (action)                          // tongue
        ell(c, x, y + 0.34f * w, 0.06f * w, 0.05f * w, 0xFF7A9A);
}

/* ---------------- carousel icons (about 40 px wide) ---------------- */

static void icon_crown(uint16_t *c, int x, int y) {
    tri(c, x - 10, y - 12, x - 14, y + 4, x - 4, y + 4, 0xFFC83D);
    tri(c, x, y - 17, x - 6, y + 4, x + 6, y + 4, 0xFFC83D);
    tri(c, x + 10, y - 12, x + 4, y + 4, x + 14, y + 4, 0xFFC83D);
    draw_rect(c, x - 14, y + 3, 29, 7, 0xFFC83D);
    ell(c, x, y + 6, 2, 2, 0xFF3B30);
}
static void icon_shades(uint16_t *c, int x, int y) {
    draw_line(c, x - 6, y - 1, x + 6, y - 1, 2.5f, 0xFFFFFF);
    for (int s = -1; s <= 1; s += 2) {
        ell(c, x + s * 10, y + 1, 10, 8, 0xFFFFFF);
        ell(c, x + s * 10, y + 1, 8, 6, 0x111111);
    }
}
static void icon_cat(uint16_t *c, int x, int y) {
    for (int s = -1; s <= 1; s += 2) {
        tri(c, x + s * 13, y - 17, x + s * 15, y - 2, x + s * 3, y - 9, 0xE8C9A0);
        draw_line(c, x + s * 6, y + 7, x + s * 20, y + 4, 1.5f, 0xFFFFFF);
        draw_line(c, x + s * 6, y + 9, x + s * 20, y + 11, 1.5f, 0xFFFFFF);
    }
    ell(c, x, y + 3, 15, 13, 0xE8C9A0);
    ell(c, x, y + 6, 3, 2, 0xFF7A9A);
    ell(c, x - 6, y, 2, 2, 0x111111);
    ell(c, x + 6, y, 2, 2, 0x111111);
}
static void icon_devil(uint16_t *c, int x, int y) {
    for (int s = -1; s <= 1; s += 2)
        tri(c, x + s * 15, y - 18, x + s * 13, y - 6, x + s * 4, y - 10, 0xFF9F0A);
    ell(c, x, y + 3, 14, 14, 0xE0201B);
    for (int s = -1; s <= 1; s += 2) {
        draw_line(c, x + s * 9, y - 3, x + s * 3, y - 1, 2.0f, 0x2A0A0C);
        ell(c, x + s * 6, y + 1, 2, 2, 0xFFD60A);
    }
    draw_line(c, x - 5, y + 10, x + 5, y + 10, 2.0f, 0x2A0A0C);
}
static void icon_gentleman(uint16_t *c, int x, int y) {
    ell(c, x, y + 5, 13, 12, 0xE8C9A0);
    draw_rect(c, x - 8, y - 18, 16, 12, 0x111111);
    draw_rect(c, x - 8, y - 9, 16, 3, 0xC0262D);
    ell(c, x, y - 6, 15, 3, 0x111111);
    ell(c, x - 4, y + 8, 5, 2, 0x2B1B12);
    ell(c, x + 4, y + 8, 5, 2, 0x2B1B12);
}
static void icon_love(uint16_t *c, int x, int y) {
    heart(c, x, y - 2, 14, 0xFF2D6F);
}


static void icon_hipster(uint16_t *c, int x, int y) {
    for (int s = -1; s <= 1; s += 2) {
        ell(c, x + s * 10, y - 6, 10, 8, 0xFFFFFF);
        ell(c, x + s * 10, y - 6, 8, 6, 0x2B3A5C);
        draw_line(c, x + s * 3, y + 9, x + s * 14, y + 11, 5.0f, 0x7A4A2C);
        ell(c, x + s * 16, y + 7, 3, 3, 0x7A4A2C);
    }
    draw_line(c, x - 4, y - 7, x + 4, y - 7, 2.0f, 0xFFFFFF);
}
static void icon_clay(uint16_t *c, int x, int y) {
    ell(c, x, y + 1, 14, 17, 0x9FAEE6);
    for (int s = -1; s <= 1; s += 2) ell(c, x + s * 6, y - 4, 4, 3, 0xE8B890);
    ell(c, x, y + 3, 3, 2, 0xE8B890);
    ell(c, x, y + 10, 5, 2, 0xD08070);
    ell(c, x - 13, y - 12, 5, 5, 0xFF8FB5);
    ell(c, x - 13, y - 12, 2, 2, 0xFFB347);
}
static void icon_bear(uint16_t *c, int x, int y) {
    for (int s = -1; s <= 1; s += 2) {
        ell(c, x + s * 13, y - 13, 7, 7, 0x8A5A3C);
        ell(c, x + s * 13, y - 13, 4, 4, 0x4B2E1E);
    }
    ell(c, x, y + 2, 15, 14, 0xE8B890);
    for (int s = -1; s <= 1; s += 2) ell(c, x + s * 10, y + 3, 4, 3, 0xFF8FA0);
    ell(c, x, y + 8, 9, 6, 0x7A4A2C);
    ell(c, x, y + 4, 4, 3, 0x3A2418);
}

/* ---------------- the three entry points main.c uses ---------------- */

const char *filter_name_any(int i) {
    return i < F_COUNT ? FILTER_NAMES[i] : XNAMES[i - F_COUNT];
}

void filter_draw_any(uint16_t *c, int i, Face f, bool action, float time) {
    if (i < F_COUNT) { filter_draw(c, i, f, action, time); return; }
    switch (i - F_COUNT) {
    case 0: crown(c, f, action, time); break;
    case 1: shades(c, f, action, time); break;
    case 2: cat(c, f, action, time); break;
    case 3: devil(c, f, action, time); break;
    case 4: gentleman(c, f, action, time); break;
    case 5: love(c, f, action, time); break;
    case 6: hipster(c, f, action, time); break;
    case 7: clay(c, f, action, time); break;
    case 8: bear(c, f, action, time); break;
    }
}

void filter_icon_any(uint16_t *c, int i, int cx, int cy) {
    if (i < F_COUNT) { filter_icon(c, i, cx, cy); return; }
    switch (i - F_COUNT) {
    case 0: icon_crown(c, cx, cy); break;
    case 1: icon_shades(c, cx, cy); break;
    case 2: icon_cat(c, cx, cy); break;
    case 3: icon_devil(c, cx, cy); break;
    case 4: icon_gentleman(c, cx, cy); break;
    case 5: icon_love(c, cx, cy); break;
    case 6: icon_hipster(c, cx, cy); break;
    case 7: icon_clay(c, cx, cy); break;
    case 8: icon_bear(c, cx, cy); break;
    }
}
