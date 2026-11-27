// extra.c - extra face filters for Snap3DS.
// Built only on the drawing functions in art.h, so art.c needs no change.
#include <math.h>
#include "extra.h"

// If colours look washed out or wrong, art.c may use a different alpha scale
// (for example 0..100). Change this one number to match.
#define OPAQUE 255
#define PI_F 3.14159265f

enum { X_CROWN, X_SHADES, X_CAT, X_DEVIL, X_GENTLEMAN, X_LOVE, X_HIPSTER, X_CLAYMASK, X_BEAR };

static const char *const X_NAMES[X_COUNT] = {
    "CROWN", "SHADES", "CAT", "DEVIL", "GENTLEMAN", "LOVE", "HIPSTER", "CLAY MASK", "BEAR"
};

/* ---------------- small helpers ---------------- */

static void circle(uint16_t *c, float x, float y, float r, uint32_t rgb) {
    draw_ellipse(c, x, y, r, r, 0.0f, rgb, OPAQUE);
}

static void oval(uint16_t *c, float x, float y, float rx, float ry, float angle, uint32_t rgb) {
    draw_ellipse(c, x, y, rx, ry, angle, rgb, OPAQUE);
}

static float thin(float t) { return t < 1.2f ? 1.2f : t; }

// Outline of an ellipse, drawn as short line pieces.
static void ring(uint16_t *c, float x, float y, float rx, float ry, float thick, uint32_t rgb) {
    const int n = 16;
    for (int k = 0; k < n; k++) {
        float a0 = 2.0f * PI_F * k / n, a1 = 2.0f * PI_F * (k + 1) / n;
        draw_line(c, x + rx * cosf(a0), y + ry * sinf(a0),
                  x + rx * cosf(a1), y + ry * sinf(a1), thin(thick), rgb);
    }
}

static void heart(uint16_t *c, float x, float y, float s, uint32_t rgb) {
    circle(c, x - 0.25f * s, y - 0.15f * s, 0.3f * s, rgb);
    circle(c, x + 0.25f * s, y - 0.15f * s, 0.3f * s, rgb);
    oval(c, x, y, 0.45f * s, 0.3f * s, 0.0f, rgb);
    draw_line(c, x - 0.45f * s, y, x, y + 0.6f * s, thin(0.3f * s), rgb);
    draw_line(c, x + 0.45f * s, y, x, y + 0.6f * s, thin(0.3f * s), rgb);
}

/* ---------------- the filters ---------------- */

static void draw_crown(uint16_t *c, Face f, bool action) {
    float w = f.w, by = f.y - 0.72f * w;
    uint32_t gold = 0xF5C518, gem = action ? 0x4FD8FF : 0xE23B4E;
    draw_rect(c, (int)(f.x - 0.38f * w), (int)by, (int)(0.76f * w), (int)(0.14f * w), gold);
    for (int k = -1; k <= 1; k++) {
        float sx = f.x + k * 0.3f * w;
        draw_line(c, sx - 0.1f * w, by, sx, by - 0.22f * w, thin(0.07f * w), gold);
        draw_line(c, sx + 0.1f * w, by, sx, by - 0.22f * w, thin(0.07f * w), gold);
        circle(c, sx, by - 0.24f * w, 0.045f * w, gem);
    }
    circle(c, f.x, by + 0.07f * w, 0.035f * w, gem);
}

static void draw_shades(uint16_t *c, Face f) {
    float w = f.w, ey = f.y - 0.08f * w;
    uint32_t dark = 0x15151C;
    draw_line(c, f.x - 0.2f * w, ey, f.x + 0.2f * w, ey, thin(0.04f * w), dark);
    draw_line(c, f.x - 0.36f * w, ey, f.x - 0.5f * w, ey - 0.04f * w, thin(0.035f * w), dark);
    draw_line(c, f.x + 0.36f * w, ey, f.x + 0.5f * w, ey - 0.04f * w, thin(0.035f * w), dark);
    oval(c, f.x - 0.21f * w, ey, 0.17f * w, 0.115f * w, 0.0f, dark);
    oval(c, f.x + 0.21f * w, ey, 0.17f * w, 0.115f * w, 0.0f, dark);
    for (int k = -1; k <= 1; k += 2) {
        float lx = f.x + k * 0.21f * w;
        draw_line(c, lx - 0.09f * w, ey - 0.03f * w, lx - 0.03f * w, ey - 0.08f * w,
                  thin(0.02f * w), 0xFFFFFF);
    }
}

static void draw_cat(uint16_t *c, Face f) {
    float w = f.w;
    for (int k = -1; k <= 1; k += 2) {
        oval(c, f.x + k * 0.3f * w, f.y - 0.62f * w, 0.11f * w, 0.2f * w, k * 0.45f, 0xF59E42);
        oval(c, f.x + k * 0.3f * w, f.y - 0.6f * w, 0.06f * w, 0.12f * w, k * 0.45f, 0xFFB3C1);
        for (int j = 0; j < 3; j++) {
            float y0 = f.y + 0.1f * w + j * 0.04f * w;
            float y1 = f.y + 0.02f * w + j * 0.1f * w;
            draw_line(c, f.x + k * 0.15f * w, y0, f.x + k * 0.5f * w, y1, thin(0.015f * w), 0xFFFFFF);
        }
    }
    circle(c, f.x, f.y + 0.08f * w, 0.035f * w, 0xFF7A9C);
}

static void draw_devil(uint16_t *c, Face f, bool action, float time) {
    float w = f.w;
    uint32_t red = 0xD7263D, dark = 0x1A1A1A;
    for (int k = -1; k <= 1; k += 2) {
        float hx = f.x + k * 0.28f * w, hy = f.y - 0.62f * w;
        oval(c, hx, hy, 0.06f * w, 0.2f * w, k * 0.4f, red);
        draw_line(c, f.x + k * 0.3f * w, f.y - 0.24f * w, f.x + k * 0.1f * w, f.y - 0.16f * w,
                  thin(0.04f * w), dark);
        if (action) circle(c, hx + k * 0.06f * w, hy - 0.2f * w,
                           0.05f * w * (1.0f + 0.4f * sinf(time * 12.0f + k)), 0xFF9F1C);
    }
    draw_line(c, f.x - 0.08f * w, f.y + 0.4f * w, f.x, f.y + 0.58f * w, thin(0.07f * w), red);
    draw_line(c, f.x + 0.08f * w, f.y + 0.4f * w, f.x, f.y + 0.58f * w, thin(0.07f * w), red);
}

static void draw_gentleman(uint16_t *c, Face f) {
    float w = f.w, brimY = f.y - 0.62f * w;
    uint32_t black = 0x1A1A1A;
    draw_rect(c, (int)(f.x - 0.3f * w), (int)(brimY - 0.42f * w), (int)(0.6f * w), (int)(0.42f * w), black);
    oval(c, f.x, brimY, 0.5f * w, 0.07f * w, 0.0f, black);
    draw_rect(c, (int)(f.x - 0.3f * w), (int)(brimY - 0.12f * w), (int)(0.6f * w), (int)(0.08f * w), 0xB3202A);
    for (int k = -1; k <= 1; k += 2)
        oval(c, f.x + k * 0.1f * w, f.y + 0.17f * w, 0.12f * w, 0.04f * w, k * 0.35f, black);
    ring(c, f.x + 0.21f * w, f.y - 0.08f * w, 0.13f * w, 0.13f * w, 0.025f * w, 0xF5C518);
    draw_line(c, f.x + 0.3f * w, f.y + 0.04f * w, f.x + 0.34f * w, f.y + 0.4f * w, thin(0.012f * w), 0xF5C518);
}

static void draw_love(uint16_t *c, Face f, bool action, float time) {
    float w = f.w;
    for (int k = 0; k < 3; k++) {
        float a = time * 2.0f + k * 2.0f * PI_F / 3.0f;
        float s = 0.15f * w * (1.0f + 0.2f * sinf(time * 6.0f + k));
        heart(c, f.x + 0.62f * w * cosf(a), f.y - 0.45f * w + 0.2f * w * sinf(a), s, 0xFF4D79);
    }
    for (int k = -1; k <= 1; k += 2) {
        circle(c, f.x + k * 0.28f * w, f.y + 0.1f * w, 0.07f * w, 0xFF8FA3);
        if (action) heart(c, f.x + k * 0.21f * w, f.y - 0.08f * w, 0.12f * w, 0xFF1F5A);
    }
}

static void draw_hipster(uint16_t *c, Face f) {
    float w = f.w, ey = f.y - 0.08f * w;
    uint32_t brown = 0x6B4423, frame = 0x2B2B2B;
    oval(c, f.x, f.y + 0.5f * w, 0.34f * w, 0.22f * w, 0.0f, brown);
    for (int k = -1; k <= 1; k += 2) {
        oval(c, f.x + k * 0.09f * w, f.y + 0.2f * w, 0.12f * w, 0.04f * w, k * 0.3f, brown);
        ring(c, f.x + k * 0.21f * w, ey, 0.14f * w, 0.14f * w, 0.03f * w, frame);
    }
    draw_line(c, f.x - 0.07f * w, ey, f.x + 0.07f * w, ey, thin(0.03f * w), frame);
}

static void draw_claymask(uint16_t *c, Face f) {
    float w = f.w;
    uint32_t clay = 0xA3A78B;
    oval(c, f.x, f.y - 0.55f * w, 0.45f * w, 0.1f * w, 0.0f, 0xFFFFFF);       // towel
    oval(c, f.x, f.y - 0.38f * w, 0.3f * w, 0.1f * w, 0.0f, clay);            // forehead
    draw_line(c, f.x, f.y - 0.12f * w, f.x, f.y + 0.12f * w, thin(0.1f * w), clay); // nose
    oval(c, f.x, f.y + 0.5f * w, 0.17f * w, 0.08f * w, 0.0f, clay);           // chin
    for (int k = -1; k <= 1; k += 2) {
        oval(c, f.x + k * 0.27f * w, f.y + 0.12f * w, 0.14f * w, 0.1f * w, 0.0f, clay); // cheeks
        circle(c, f.x + k * 0.21f * w, f.y - 0.08f * w, 0.13f * w, 0x7CC47A);  // cucumber
        circle(c, f.x + k * 0.21f * w, f.y - 0.08f * w, 0.09f * w, 0xB8E6A0);
    }
}

static void draw_bear(uint16_t *c, Face f, bool action) {
    float w = f.w;
    for (int k = -1; k <= 1; k += 2) {
        circle(c, f.x + k * 0.4f * w, f.y - 0.5f * w, 0.14f * w, 0x8B5A2B);
        circle(c, f.x + k * 0.4f * w, f.y - 0.5f * w, 0.08f * w, 0xD9A66B);
    }
    oval(c, f.x, f.y + 0.12f * w, 0.17f * w, 0.13f * w, 0.0f, 0xE8C8A0);
    oval(c, f.x, f.y + 0.06f * w, 0.06f * w, 0.045f * w, 0.0f, 0x2B1B14);
    draw_line(c, f.x, f.y + 0.1f * w, f.x, f.y + 0.17f * w, thin(0.015f * w), 0x2B1B14);
    if (action) oval(c, f.x, f.y + 0.24f * w, 0.05f * w, 0.06f * w, 0.0f, 0xFF7A8A);
}

static void draw_extra(uint16_t *c, int j, Face f, bool action, float time) {
    switch (j) {
    case X_CROWN:     draw_crown(c, f, action); break;
    case X_SHADES:    draw_shades(c, f); break;
    case X_CAT:       draw_cat(c, f); break;
    case X_DEVIL:     draw_devil(c, f, action, time); break;
    case X_GENTLEMAN: draw_gentleman(c, f); break;
    case X_LOVE:      draw_love(c, f, action, time); break;
    case X_HIPSTER:   draw_hipster(c, f); break;
    case X_CLAYMASK:  draw_claymask(c, f); break;
    case X_BEAR:      draw_bear(c, f, action); break;
    }
}

/* ---------------- public functions (see extra.h) ---------------- */

const char *filter_name_any(int i) {
    if (i < 0 || i >= ALL_FILTERS) return "";
    if (i < F_COUNT) return FILTER_NAMES[i];
    return X_NAMES[i - F_COUNT];
}

void filter_draw_any(uint16_t *c, int i, Face f, bool action, float time) {
    if (i < 0 || i >= ALL_FILTERS) return;
    if (i < F_COUNT) { filter_draw(c, i, f, action, time); return; }
    draw_extra(c, i - F_COUNT, f, action, time);
}

void filter_icon_any(uint16_t *c, int i, int cx, int cy) {
    if (i < 0 || i >= ALL_FILTERS) return;
    if (i < F_COUNT) { filter_icon(c, i, cx, cy); return; }
    Face f;
    f.w = 34.0f;
    f.x = (float)cx;
    f.y = (float)cy + 0.3f * f.w;   // the filters sit above the face centre
    draw_extra(c, i - F_COUNT, f, false, 0.0f);
}
