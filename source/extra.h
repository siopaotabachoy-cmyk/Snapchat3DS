// extra.h - extra face filters. Built only on the drawing functions in art.h,
// so art.c does not need any change.
#pragma once
#include "art.h"

#define X_COUNT 15                       // the BEAUTY looks (names are in LOOK_NAMES in extra.c)
#define C_COUNT 17                       // face effects (dog, kitty...) mixed with a beauty look
#define S_COUNT 5                        // whole-picture looks: WEBCAM, FLASH, SUNSET, BANANA, FIRE
#define CUTE_COUNT 10                    // cute filters: NEKO MIMI, PINK CAT, RED BOW, KAWAII, TEDDY, BUNNY, BIG EYES, FLOWERS, LOVE HEARTS, SPARKLE
#define ALL_FILTERS (F_COUNT + X_COUNT + C_COUNT + S_COUNT + CUTE_COUNT)  // old filters, beauty looks, mixed ones, whole-picture looks, then the cute ones

const char *filter_name_any(int i);
void filter_draw_any(uint16_t *c, int i, Face f, bool action, float time);
// Carousel circle: a face photo with the filter on it (falls back to a drawn icon).
void filter_icon_any(uint16_t *c, int i, int cx, int cy);
