// extra.c - the three face filters for Snap3DS: pictures from sprites.h are
// scaled, blended onto the camera picture and placed on the tracked face.
#include <math.h>
#include <stddef.h>
#include "extra.h"
#include "sprites.h"        // only included here, so the picture data exists once

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

/* ---------------- public functions (see extra.h) ---------------- */

const char *filter_name_any(int i) {
    if (i < 0 || i >= F_COUNT) return "";
    return FILTER_NAMES[i] ? FILTER_NAMES[i] : "";
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
    default: break;
    }
}

// Small picture for the carousel circle (fits inside radius ~26).
void filter_icon_any(uint16_t *c, int i, int cx, int cy) {
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
