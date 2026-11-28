// snapmap.h - the Map screen: an offline map of a made-up town.
// The 3DS has no GPS, so YOU choose where you are ("I'M HERE"). Snaps you take
// are pinned to that spot. Nothing goes online.
// No 3DS code in here (like memories.c).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "art.h"

#define MAP_W 1280                       // size of the town, in map units
#define MAP_H 960
#define MAP_PINS 200                     // snaps remembered on the map (the newest ones)
#define MAP_ZOOMS 4

// Buttons, as bit flags. main.c turns KEY_B etc. into these.
enum { PK_A = 1, PK_B = 2, PK_X = 4, PK_Y = 8, PK_L = 16, PK_R = 32 };

typedef struct {
    unsigned keys;                       // PK_ flags pressed on this frame
    bool down, held;                     // touch: landed this frame / still touching
    int x, y;                            // touch position (keep the last one after lifting)
    int moveX, moveY;                    // circle pad / D-pad: -1..1 pushes the map
} MapInput;

typedef struct { char name[40]; int x, y; } MapPin;

typedef struct {
    char dir[96];                        // photo folder; the map is saved there as map.txt
    int meX, meY;                        // where you are
    MapPin pin[MAP_PINS];                // oldest first
    int count;
    float cx, cy;                        // map point in the middle of the bottom screen
    int zoom;                            // 0 = whole town ... MAP_ZOOMS - 1 = closest
    bool touching, dragged;
    int sx, sy, lastX, lastY;
    int shown;                           // snap on the top screen, -1 = show the whole town
    bool photoOk;
    int place;                           // chosen place, -1 = none
    unsigned long long noteUntil;
    char note[44];
    uint16_t photo[CW * CH];
} SnapMap;

// Reads the saved map and forgets snaps that have been deleted.
void map_open(SnapMap *m, const char *dir);
// Call once per frame. Returns true when the person wants to go back to the camera.
bool map_update(SnapMap *m, const MapInput *in);
// Draws both screens (each CW*CH RGB565). me draws your round avatar.
typedef void (*MapAvatarFn)(uint16_t *img, int x, int y, int size);
void map_draw(SnapMap *m, uint16_t *top, uint16_t *bottom, unsigned long long nowMs, MapAvatarFn me);
// Pins a newly saved snap (file name only) where you are now. Returns false if it could not be saved.
bool map_add_snap(const char *dir, const char *name);
