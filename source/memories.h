// memories.h - the Memories screen, drawn to look like Snapchat's.
// No 3DS code in here (like art.c), so main.c only has to pass the buttons and
// touch in and show the two pictures that come out.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "art.h"

#define MEM_MAX 200                      // photos kept in the list (the newest ones)
#define MEM_TW 77                        // thumbnail size
#define MEM_TH 58

// Buttons, as bit flags. main.c turns KEY_B etc. into these.
enum { MK_A = 1, MK_B = 2, MK_X = 4, MK_L = 8, MK_R = 16,
       MK_LEFT = 32, MK_RIGHT = 64, MK_UP = 128, MK_DOWN = 256 };

typedef struct {
    unsigned keys;                       // MK_ flags pressed on this frame
    bool down, held, up;                 // touch: landed / still touching / lifted this frame
    int x, y;                            // touch position (keep the last one after lifting)
    unsigned long long ms;               // clock in milliseconds
} MemInput;

enum { MV_GRID, MV_PHOTO };

typedef struct {
    char dir[96];
    char names[MEM_MAX][40];             // sorted oldest to newest
    int count;
    uint8_t thumbState[MEM_MAX];         // 0 = not loaded yet, 1 = ready, 2 = could not read
    bool sel[MEM_MAX];                   // picked in select mode
    int nsel;
    bool select, confirm;                // select mode on / delete needs one more tap
    int view;                            // MV_GRID or MV_PHOTO
    int index;                           // the photo on the big screen
    bool ok;                             // that photo loaded fine
    float scroll, vel, startScroll;      // page scroll, its coasting speed, and where a drag began
    bool touching, dragged, longDone;
    int sx, sy, lastX, lastY;
    unsigned long long downMs, noteUntil;
    char note[32];                       // small message ("DELETED 2")
    uint16_t img[CW * CH];               // the big photo. main.c may borrow this as scratch space while Memories is closed.
    uint16_t thumbs[MEM_MAX][MEM_TW * MEM_TH];
} Memories;                              // about 2 MB: make it static, not a local variable

// Reads a BMP and shrinks or stretches it to ow x oh (also used by the Map).
bool mem_load_bmp(const char *path, uint16_t *out, int ow, int oh);

void mem_open(Memories *m, const char *dir);
// Call once per frame. Returns true when the person wants to go back to the camera.
bool mem_update(Memories *m, const MemInput *in);
// Draws both screens (each CW*CH RGB565).
void mem_draw(Memories *m, uint16_t *top, uint16_t *bottom, unsigned long long nowMs);
