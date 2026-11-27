// art.h - drawing, filters, face tracking, photo saving (no 3DS code in here)
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define CW 320
#define CH 240

enum { F_DOG, F_FLOWER, F_BUNNY, F_BEE, F_RAINBOW, F_COUNT };
extern const char *const FILTER_NAMES[F_COUNT];

typedef struct { float x, y, w; } Face; // face centre and face width, in pixels

typedef struct {
    Face face;
    bool found;  // seen in the latest frame
} Tracker;

void tracker_reset(Tracker *t);
void tracker_update(Tracker *t, const uint16_t *img); // img: CW*CH RGB565

// All colours are 0xRRGGBB. All images are CW*CH RGB565, row by row.
void draw_rect(uint16_t *c, int x, int y, int w, int h, uint32_t rgb);
void draw_ellipse(uint16_t *c, float cx, float cy, float rx, float ry,
                  float angle, uint32_t rgb, int alpha);
void draw_line(uint16_t *c, float x0, float y0, float x1, float y1,
               float thick, uint32_t rgb);
void draw_text(uint16_t *c, int x, int y, int scale, const char *s, uint32_t rgb);
int  text_width(const char *s, int scale);

void filter_draw(uint16_t *c, int filter, Face f, bool action, float time);
void filter_icon(uint16_t *c, int filter, int cx, int cy);

bool save_bmp(const char *path, const uint16_t *img);
