// spotlight.h - the Spotlight screen: a feed of random short videos.
// Offline: the 3DS app has no internet, so the "videos" are animations that are
// drawn on the spot. Most are FRUIT STORIES: two cartoon fruits (apple, banana,
// strawberry, orange, watermelon, lemon) act out a four-line story with typed
// subtitles. The rest are abstract clips (plasma, bouncing balls, waves, stars,
// a tunnel). Every video gets a random creator, caption, sound and like count.
// No 3DS code in here (like stories.c).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "art.h"

#define SP_HISTORY 32                    // videos you can go back to
#define SP_KINDS 9                       // abstract clips: plasma, balls, waves, stars, tunnel, fireworks, hearts, disco, bubbles

// Buttons, as bit flags. main.c turns KEY_B etc. into these.
enum { SP_KEY_A = 1, SP_KEY_B = 2, SP_KEY_X = 4, SP_KEY_LEFT = 8, SP_KEY_RIGHT = 16 };

typedef struct {
    unsigned keys;                       // SP_KEY_ flags pressed on this frame
    bool down;                           // the screen was touched on this frame
    int x, y;                            // touch position
    unsigned long long ms;               // clock in milliseconds
} SpotInput;

typedef struct {
    uint32_t seed[SP_HISTORY];           // each video is made from its seed
    bool liked[SP_HISTORY];
    int count;                           // videos in the history
    int pos;                             // the video being shown
    uint32_t rng;                        // makes the next seed
    unsigned long long startMs, lastMs;  // when this video began (it loops)
    unsigned long long heartUntil;       // big heart pop after a like
} Spotlight;

// Starts the feed at a random video. nowMs also seeds the random numbers.
void spotlight_open(Spotlight *s, unsigned long long nowMs);
// Call once per frame. Returns true when the person wants to go back to the camera.
bool spotlight_update(Spotlight *s, const SpotInput *in);
// Draws both screens (each CW*CH RGB565).
void spotlight_draw(const Spotlight *s, uint16_t *top, uint16_t *bottom, unsigned long long nowMs);
