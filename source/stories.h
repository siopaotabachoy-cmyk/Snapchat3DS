// stories.h - the Stories screen: a slideshow of the snaps taken today.
// Offline: only your own snaps. No 3DS code in here (like memories.c).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "art.h"

#define ST_MAX 50                        // snaps in one story (the newest ones)
#define ST_SHOW_MS 4000                  // time each snap stays up

// Buttons, as bit flags. main.c turns KEY_B etc. into these.
enum { SK_A = 1, SK_B = 2, SK_LEFT = 4, SK_RIGHT = 8 };

typedef struct {
    unsigned keys;                       // SK_ flags pressed on this frame
    bool down;                           // the screen was touched on this frame
    int x, y;                            // touch position
    unsigned long long ms;               // clock in milliseconds
} StoryInput;

typedef struct {
    char dir[96];
    char names[ST_MAX][40];              // oldest first
    int count;
    bool playing, paused;
    int index;                           // the snap being shown
    bool ok;                             // it loaded fine
    unsigned long long lastMs;           // clock at the last frame, 0 = just started
    unsigned elapsed;                    // how long this snap has been up
    uint16_t img[CW * CH];               // the snap being shown
    uint16_t cover[CW * CH];             // the newest snap
    bool coverOk;
} Stories;

// Collects the snaps in dir that were taken on the same day as now.
void stories_open(Stories *s, const char *dir, time_t now);
// Call once per frame. Returns true when the person wants to go back to the camera.
bool stories_update(Stories *s, const StoryInput *in);
// Draws both screens (each CW*CH RGB565).
void stories_draw(const Stories *s, uint16_t *top, uint16_t *bottom);
