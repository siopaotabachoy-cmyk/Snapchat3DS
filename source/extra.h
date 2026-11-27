// extra.h - extra face filters. Built only on the drawing functions in art.h,
// so art.c does not need any change.
#pragma once
#include "art.h"

#define X_COUNT 5                        // NATURAL GLAM PEACHY DEWY SUNKISSED (the BEAUTY looks)
#define ALL_FILTERS (F_COUNT + X_COUNT)  // old filters first, new ones after

const char *filter_name_any(int i);
void filter_draw_any(uint16_t *c, int i, Face f, bool action, float time);
// Carousel circle: a face photo with the filter on it (falls back to a drawn icon).
void filter_icon_any(uint16_t *c, int i, int cx, int cy);
