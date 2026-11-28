// snapmap.c - the Map screen.
//
// Bottom (touch) screen: the map. Drag or use the circle pad / D-pad to move,
// L / R or + / - to zoom, "I'M HERE" (X) puts you on the cross in the middle,
// "FIND ME" (Y) jumps back to you. Tap a place or a snap pin.
// Top screen: the whole town, or the snap you tapped.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snapmap.h"
#include "memories.h"                    // mem_load_bmp

#define WHITE  0xFFFFFF
#define DARK   0x17171B
#define YELLOW 0xFFFC00
#define BLUE   0x14ADFF
#define RED    0xF23C57
#define LAND   0xBFE3A8
#define WATER  0x7CC4EE
#define SAND   0xF2E2B0
#define PARK   0x84C872
#define ROAD   0xFFFFFF
#define BLOCK  0xD9D3C7

#define BAR_Y 210                        // button bar at the bottom
#define MID_X (CW / 2)
#define MID_Y (BAR_Y / 2)

static const float ZOOM[MAP_ZOOMS] = { 0.25f, 0.5f, 1.0f, 2.0f };

/* ---------------- the town ---------------- */

typedef struct { int x, y, w, h; uint32_t rgb; } Box;

static const Box AREAS[] = {             // painted in this order
    { 0, 780, 1280, 40, SAND }, { 0, 820, 1280, 140, WATER },     // beach and sea
    { 80, 80, 260, 200, PARK }, { 860, 520, 240, 170, PARK },     // parks
    { 520, 330, 90, 70, BLOCK }, { 640, 330, 110, 70, BLOCK },    // downtown
    { 520, 440, 90, 80, BLOCK }, { 640, 440, 110, 80, BLOCK },
    { 160, 460, 100, 70, BLOCK }, { 280, 460, 80, 70, BLOCK },
    { 1010, 170, 130, 90, BLOCK },
};

static const Box ROADS[] = {
    { 0, 300, 1280, 16, ROAD }, { 0, 410, 1280, 20, ROAD }, { 0, 720, 1280, 16, ROAD },
    { 60, 540, 780, 10, ROAD }, { 380, 180, 780, 10, ROAD },
    { 140, 300, 12, 436, ROAD }, { 490, 0, 20, 780, ROAD }, { 620, 190, 10, 530, ROAD },
    { 760, 0, 14, 780, ROAD }, { 1150, 180, 12, 556, ROAD },
};

static const struct { int x, y, rx, ry; } LAKE[] = { { 330, 660, 95, 45 }, { 395, 645, 60, 40 } };

typedef struct { const char *name; int x, y; uint32_t rgb; } Place;
static const Place PLACES[] = {
    { "GREEN PARK", 210, 180, 0x2F8F3A },   { "DOWNTOWN", 630, 415, 0x6A5ACD },
    { "SCHOOL", 210, 495, 0xE08A1E },       { "MALL", 1075, 215, 0xD6409F },
    { "LAKE", 340, 660, 0x1E78C8 },         { "SPORTS FIELD", 980, 605, 0x2E8B57 },
    { "SUNNY BEACH", 700, 800, 0xC99A1A },  { "CAFE", 890, 365, 0x8B5A2B },
    { "HILLTOP", 960, 90, 0x7A6A55 },
};
#define N_PLACES ((int)(sizeof(PLACES) / sizeof(PLACES[0])))

/* ---------------- saving and loading ---------------- */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void file_path(const char *dir, const char *name, char *out, size_t len) {
    snprintf(out, len, "%.95s/%.39s", dir, name);
}

// Fills in your spot and the pins. A missing file gives the starting spot and no pins.
static int load_file(const char *dir, int *meX, int *meY, MapPin *pin) {
    *meX = 630; *meY = 415;              // downtown
    char path[160], line[96];
    file_path(dir, "map.txt", path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        char name[40];
        int x, y;
        if (sscanf(line, "ME %d %d", &x, &y) == 2) {
            *meX = clampi(x, 0, MAP_W - 1);
            *meY = clampi(y, 0, MAP_H - 1);
        } else if (count < MAP_PINS && sscanf(line, "SNAP %39s %d %d", name, &x, &y) == 3) {
            memcpy(pin[count].name, name, sizeof(name));
            pin[count].x = clampi(x, 0, MAP_W - 1);
            pin[count].y = clampi(y, 0, MAP_H - 1);
            count++;
        }
    }
    fclose(f);
    return count;
}

static bool save_file(const char *dir, int meX, int meY, const MapPin *pin, int count) {
    char path[160];
    file_path(dir, "map.txt", path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "ME %d %d\n", meX, meY);
    for (int i = 0; i < count; i++) fprintf(f, "SNAP %s %d %d\n", pin[i].name, pin[i].x, pin[i].y);
    bool ok = !ferror(f);
    if (fclose(f) != 0) ok = false;
    return ok;
}

bool map_add_snap(const char *dir, const char *name) {
    static MapPin pin[MAP_PINS];
    int meX, meY;
    int count = load_file(dir, &meX, &meY, pin);
    if (count == MAP_PINS) {             // full: forget the oldest
        memmove(&pin[0], &pin[1], sizeof(MapPin) * (MAP_PINS - 1));
        count--;
    }
    snprintf(pin[count].name, sizeof(pin[count].name), "%s", name);
    pin[count].x = meX;
    pin[count].y = meY;
    return save_file(dir, meX, meY, pin, count + 1);
}

/* ---------------- view ---------------- */

static void clamp_view(SnapMap *m) {     // the middle of the screen stays on the map
    if (m->cx < 0) m->cx = 0;
    if (m->cx > MAP_W) m->cx = MAP_W;
    if (m->cy < 0) m->cy = 0;
    if (m->cy > MAP_H) m->cy = MAP_H;
}

static int to_sx(const SnapMap *m, int x) { return MID_X + (int)((x - m->cx) * ZOOM[m->zoom]); }
static int to_sy(const SnapMap *m, int y) { return MID_Y + (int)((y - m->cy) * ZOOM[m->zoom]); }

static void find_me(SnapMap *m) {
    m->cx = (float)m->meX;
    m->cy = (float)m->meY;
}

static void say(SnapMap *m, const char *text) {
    snprintf(m->note, sizeof(m->note), "%s", text);
    m->noteUntil = (unsigned long long)-1;       // map_draw starts the clock
}

void map_open(SnapMap *m, const char *dir) {
    snprintf(m->dir, sizeof(m->dir), "%s", dir);
    int n = load_file(m->dir, &m->meX, &m->meY, m->pin);
    int kept = 0;
    for (int i = 0; i < n; i++) {        // drop snaps that are no longer on the card
        char path[160];
        file_path(m->dir, m->pin[i].name, path, sizeof(path));
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fclose(f);
        m->pin[kept++] = m->pin[i];
    }
    m->count = kept;
    if (kept != n) save_file(m->dir, m->meX, m->meY, m->pin, kept);
    m->zoom = 2;
    m->touching = m->dragged = false;
    m->shown = m->place = -1;
    m->note[0] = 0;
    find_me(m);
}

/* ---------------- input ---------------- */

static bool same_spot(const MapPin *a, const MapPin *b) { return a->x == b->x && a->y == b->y; }

static int snaps_at(const SnapMap *m, int i) {
    int n = 0;
    for (int k = 0; k < m->count; k++) n += same_spot(&m->pin[k], &m->pin[i]);
    return n;
}

static void show_snap(SnapMap *m, int i) {
    char path[160];
    file_path(m->dir, m->pin[i].name, path, sizeof(path));
    m->shown = i;
    m->place = -1;
    m->photoOk = mem_load_bmp(path, m->photo, CW, CH);
}

// Snap badges sit up and to the right of their spot, so your avatar does not hide them.
#define BADGE_DX 17
#define BADGE_DY (-27)

static bool near_point(int x, int y, int px, int py, int r) {
    return (x - px) * (x - px) + (y - py) * (y - py) <= r * r;
}

static void zoom_by(SnapMap *m, int step) {
    m->zoom = clampi(m->zoom + step, 0, MAP_ZOOMS - 1);
}

static void im_here(SnapMap *m) {
    m->meX = clampi((int)m->cx, 0, MAP_W - 1);
    m->meY = clampi((int)m->cy, 0, MAP_H - 1);
    say(m, save_file(m->dir, m->meX, m->meY, m->pin, m->count)
           ? "YOU ARE HERE NOW" : "SAVE FAILED - CHECK SD CARD");
}

static void tap_map(SnapMap *m, int x, int y) {
    // a snap badge first; tapping it again goes to the next older snap on that spot
    for (int i = m->count - 1; i >= 0; i--) {
        if (!near_point(x, y, to_sx(m, m->pin[i].x) + BADGE_DX, to_sy(m, m->pin[i].y) + BADGE_DY, 14))
            continue;
        int pick = i;                    // i is the newest on this spot
        if (m->shown >= 0 && same_spot(&m->pin[m->shown], &m->pin[i]))
            for (int k = m->shown - 1; k >= 0; k--)
                if (same_spot(&m->pin[k], &m->pin[i])) { pick = k; break; }
        show_snap(m, pick);
        return;
    }
    for (int i = 0; i < N_PLACES; i++)
        if (near_point(x, y, to_sx(m, PLACES[i].x), to_sy(m, PLACES[i].y), 14)) {
            m->place = i;
            m->shown = -1;
            m->cx = (float)PLACES[i].x;  // centred, so "I'M HERE" moves you there
            m->cy = (float)PLACES[i].y;
            say(m, PLACES[i].name);
            return;
        }
    m->shown = m->place = -1;
}

bool map_update(SnapMap *m, const MapInput *in) {
    if (in->keys & PK_B) {
        if (m->shown >= 0) m->shown = -1;
        else return true;
    }
    if (in->keys & PK_L) zoom_by(m, -1);
    if (in->keys & PK_R) zoom_by(m, 1);
    if (in->keys & PK_X) im_here(m);
    if (in->keys & PK_Y) find_me(m);
    if (in->keys & PK_A) {               // open what is under the cross
        tap_map(m, MID_X, MID_Y);
        if (m->shown < 0 && m->place < 0)
            for (int i = m->count - 1; i >= 0; i--)
                if (near_point(MID_X, MID_Y, to_sx(m, m->pin[i].x), to_sy(m, m->pin[i].y), 16)) {
                    show_snap(m, i);
                    break;
                }
    }
    m->cx += in->moveX * 5.0f / ZOOM[m->zoom];
    m->cy += in->moveY * 5.0f / ZOOM[m->zoom];

    if (in->down && !m->touching) {
        m->touching = true;
        m->dragged = false;
        m->sx = m->lastX = in->x;
        m->sy = m->lastY = in->y;
    }
    if (m->touching && in->held) {
        if (abs(in->x - m->sx) > 5 || abs(in->y - m->sy) > 5) m->dragged = true;
        if (m->dragged && m->sy < BAR_Y) {       // the map follows the stylus
            m->cx -= (in->x - m->lastX) / ZOOM[m->zoom];
            m->cy -= (in->y - m->lastY) / ZOOM[m->zoom];
            m->lastX = in->x;
            m->lastY = in->y;
        }
    } else if (m->touching) {
        m->touching = false;
        if (!m->dragged) {
            int x = m->sx, y = m->sy;
            if (y >= BAR_Y) {                    // BACK | I'M HERE | FIND ME
                if (x < 80) return true;
                if (x < 200) im_here(m);
                else find_me(m);
            } else if (x >= CW - 36 && y < 38) zoom_by(m, 1);
            else if (x >= CW - 36 && y < 74) zoom_by(m, -1);
            else tap_map(m, x, y);
        }
    }
    clamp_view(m);
    return false;
}

/* ---------------- drawing ---------------- */

typedef struct { float cx, cy, k; int mx, my; } View;   // map point (cx,cy) is at screen (mx,my)

static void box(uint16_t *c, const View *v, const Box *b) {
    int x0 = v->mx + (int)((b->x - v->cx) * v->k), y0 = v->my + (int)((b->y - v->cy) * v->k);
    int x1 = v->mx + (int)((b->x + b->w - v->cx) * v->k), y1 = v->my + (int)((b->y + b->h - v->cy) * v->k);
    draw_rect(c, x0, y0, x1 - x0 > 0 ? x1 - x0 : 1, y1 - y0 > 0 ? y1 - y0 : 1, b->rgb);
}

static void draw_town(uint16_t *c, const View *v) {
    draw_rect(c, 0, 0, CW, CH, 0x9FBF8F);        // outside the town
    const Box land = { 0, 0, MAP_W, MAP_H, LAND };
    box(c, v, &land);
    for (unsigned i = 0; i < sizeof(AREAS) / sizeof(AREAS[0]); i++) box(c, v, &AREAS[i]);
    for (unsigned i = 0; i < sizeof(LAKE) / sizeof(LAKE[0]); i++)
        draw_ellipse(c, v->mx + (LAKE[i].x - v->cx) * v->k, v->my + (LAKE[i].y - v->cy) * v->k,
                     LAKE[i].rx * v->k, LAKE[i].ry * v->k, 0, WATER, 255);
    for (unsigned i = 0; i < sizeof(ROADS) / sizeof(ROADS[0]); i++) box(c, v, &ROADS[i]);
}

static void pill_text(uint16_t *c, int x, int y, int w, int h, const char *s, uint32_t bg, uint32_t fg) {
    float r = h / 2.0f;
    draw_ellipse(c, x + r, y + r - 0.5f, r, r, 0, bg, 255);
    draw_ellipse(c, x + w - r - 1, y + r - 0.5f, r, r, 0, bg, 255);
    draw_rect(c, x + (int)r, y, w - 2 * (int)r, h, bg);
    draw_text(c, x + (w - text_width(s, 1)) / 2, y + (h - 7) / 2, 1, s, fg);
}

static void centred_pill(uint16_t *c, int y, const char *s, uint32_t bg, uint32_t fg) {
    int w = text_width(s, 1) + 20;
    pill_text(c, (CW - w) / 2, y, w, 18, s, bg, fg);
}

static bool off_screen(int x, int y) { return x < -60 || x > CW + 60 || y < -60 || y > CH + 60; }

static void snap_badge(uint16_t *c, int x, int y, int n, bool lit) {
    draw_ellipse(c, x, y, 11, 11, 0, lit ? YELLOW : WHITE, 255);
    draw_ellipse(c, x, y, 9, 9, 0, RED, 255);
    char t[8];
    snprintf(t, sizeof(t), "%d", n);
    int scale = n < 10 ? 2 : 1;
    draw_text(c, x - text_width(t, scale) / 2, y - (7 * scale) / 2, scale, t, WHITE);
}

static void draw_bottom(uint16_t *c, SnapMap *m, unsigned long long now, MapAvatarFn me) {
    View v = { m->cx, m->cy, ZOOM[m->zoom], MID_X, MID_Y };
    draw_town(c, &v);

    for (int i = 0; i < N_PLACES; i++) {
        int x = to_sx(m, PLACES[i].x), y = to_sy(m, PLACES[i].y);
        if (off_screen(x, y)) continue;
        draw_ellipse(c, x, y, 7, 7, 0, i == m->place ? YELLOW : WHITE, 255);
        draw_ellipse(c, x, y, 5, 5, 0, PLACES[i].rgb, 255);
        if (m->zoom > 0 || i == m->place) {      // names would overlap on the whole-town view
            int w = text_width(PLACES[i].name, 1) + 10;
            pill_text(c, x - w / 2, y + 10, w, 13, PLACES[i].name, DARK, WHITE);
        }
    }

    int mx = to_sx(m, m->meX), my = to_sy(m, m->meY);
    if (!off_screen(mx, my)) {                   // you
        draw_ellipse(c, mx, my, 5, 3, 0, 0x355530, 180);
        if (mx >= 16 && mx < CW - 16 && my >= 36 && my < CH) {
            me(c, mx - 12, my - 30, 24);         // the avatar needs to be fully on screen
            draw_line(c, mx, my - 5, mx, my - 1, 3.0f, WHITE);
        } else {
            draw_ellipse(c, mx, my, 7, 7, 0, BLUE, 255);
        }
    }

    for (int i = 0; i < m->count; i++) {         // one badge per spot, with how many snaps
        bool later = false;
        for (int k = i + 1; k < m->count; k++) later |= same_spot(&m->pin[i], &m->pin[k]);
        if (later) continue;
        int x = to_sx(m, m->pin[i].x), y = to_sy(m, m->pin[i].y);
        if (off_screen(x, y)) continue;
        bool lit = m->shown >= 0 && same_spot(&m->pin[m->shown], &m->pin[i]);
        snap_badge(c, x + BADGE_DX, y + BADGE_DY, snaps_at(m, i), lit);
    }

    // cross in the middle: where "I'M HERE" will put you
    draw_rect(c, MID_X - 7, MID_Y - 1, 15, 3, DARK);
    draw_rect(c, MID_X - 1, MID_Y - 7, 3, 15, DARK);
    draw_rect(c, MID_X - 6, MID_Y, 13, 1, YELLOW);
    draw_rect(c, MID_X, MID_Y - 6, 1, 13, YELLOW);

    // zoom buttons
    for (int i = 0; i < 2; i++) {
        int y = 20 + i * 36;
        draw_ellipse(c, CW - 19, y, 15, 15, 0, DARK, 255);
        draw_rect(c, CW - 26, y - 1, 15, 3, WHITE);
        if (i == 0) draw_rect(c, CW - 20, y - 7, 3, 15, WHITE);
    }

    draw_rect(c, 0, BAR_Y, CW, CH - BAR_Y, DARK);
    pill_text(c, 6, BAR_Y + 5, 68, 20, "B BACK", 0x38383D, WHITE);
    pill_text(c, 86, BAR_Y + 5, 108, 20, "X I'M HERE", YELLOW, DARK);
    pill_text(c, 206, BAR_Y + 5, 108, 20, "Y FIND ME", BLUE, WHITE);
    draw_rect(c, 86 + (108 - text_width("X I'M HERE", 1)) / 2 + 3 * 6 + 2, BAR_Y + 11, 2, 3, DARK);   // apostrophe

    if (m->note[0]) {
        if (m->noteUntil == (unsigned long long)-1) m->noteUntil = now + 2000;
        if (now >= m->noteUntil) m->note[0] = 0;
        else centred_pill(c, 186, m->note, DARK, WHITE);
    }
}

static void draw_top(uint16_t *c, const SnapMap *m) {
    if (m->shown >= 0) {
        if (m->photoOk) memcpy(c, m->photo, sizeof(m->photo));
        else {
            draw_rect(c, 0, 0, CW, CH, 0x000000);
            centred_pill(c, 110, "CANT OPEN THIS FILE", DARK, WHITE);
        }
        int older = 0;                           // position among the snaps on this spot
        for (int i = 0; i <= m->shown; i++) older += same_spot(&m->pin[i], &m->pin[m->shown]);
        char t[48];
        snprintf(t, sizeof(t), "SNAP %d / %d HERE", older, snaps_at(m, m->shown));
        centred_pill(c, 8, t, DARK, WHITE);
        centred_pill(c, 214, "TAP THE PIN: NEXT   B: CLOSE", DARK, WHITE);
        return;
    }
    // the whole town, with a frame round the part on the bottom screen
    View v = { 0, 0, 0.25f, 0, 0 };
    draw_town(c, &v);
    for (int i = 0; i < N_PLACES; i++)
        draw_ellipse(c, PLACES[i].x / 4.0f, PLACES[i].y / 4.0f, 3, 3, 0, PLACES[i].rgb, 255);
    for (int i = 0; i < m->count; i++)
        draw_ellipse(c, m->pin[i].x / 4.0f, m->pin[i].y / 4.0f, 4, 4, 0, RED, 255);
    draw_ellipse(c, m->meX / 4.0f, m->meY / 4.0f, 6, 6, 0, WHITE, 255);
    draw_ellipse(c, m->meX / 4.0f, m->meY / 4.0f, 4, 4, 0, BLUE, 255);

    float k = ZOOM[m->zoom];
    int x0 = (int)((m->cx - MID_X / k) / 4), x1 = (int)((m->cx + MID_X / k) / 4);
    int y0 = (int)((m->cy - MID_Y / k) / 4), y1 = (int)((m->cy + MID_Y / k) / 4);
    x0 = clampi(x0, 0, CW - 2); x1 = clampi(x1, 1, CW - 1);
    y0 = clampi(y0, 0, CH - 2); y1 = clampi(y1, 1, CH - 1);
    draw_rect(c, x0, y0, x1 - x0 + 1, 2, DARK);
    draw_rect(c, x0, y1 - 1, x1 - x0 + 1, 2, DARK);
    draw_rect(c, x0, y0, 2, y1 - y0 + 1, DARK);
    draw_rect(c, x1 - 1, y0, 2, y1 - y0 + 1, DARK);

    centred_pill(c, 6, "SNAP MAP - OFFLINE TOWN", DARK, WHITE);
    char t[48];
    snprintf(t, sizeof(t), "%d SNAP%s ON THE MAP", m->count, m->count == 1 ? "" : "S");
    centred_pill(c, 216, t, DARK, WHITE);
}

void map_draw(SnapMap *m, uint16_t *top, uint16_t *bottom, unsigned long long nowMs, MapAvatarFn me) {
    draw_top(top, m);
    draw_bottom(bottom, m, nowMs, me);
}
