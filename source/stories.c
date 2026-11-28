// stories.c - the Stories screen. See stories.h.
//
// First page: "My Story" with how many snaps it has; tap it (or press A) to play.
// Playing: the snap is on the top screen. Bottom screen: previous, pause, next,
// close. D-pad left / right also move, A pauses, B closes.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "stories.h"
#include "memories.h"                    // mem_load_bmp

#define BG     0x111113
#define CARD   0x232327
#define GREY   0x9A9AA0
#define WHITE  0xFFFFFF
#define BLACK  0x000000
#define YELLOW 0xFFFC00
#define PURPLE 0xA259FF

/* ---------------- finding and loading ---------------- */

static int cmp_name(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

static bool load(const Stories *s, int n, uint16_t *out) {
    char path[160];
    snprintf(path, sizeof(path), "%.95s/%.39s", s->dir, s->names[n]);
    return mem_load_bmp(path, out, CW, CH);
}

void stories_open(Stories *s, const char *dir, time_t now) {
    snprintf(s->dir, sizeof(s->dir), "%s", dir);
    s->count = s->index = 0;
    s->playing = s->paused = s->ok = s->coverOk = false;
    s->lastMs = 0;
    s->elapsed = 0;

    char today[16] = "";
    struct tm *lt = localtime(&now);
    if (!lt || !strftime(today, sizeof(today), "%Y%m%d_", lt)) return;

    DIR *d = opendir(s->dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {   // snap names start with the date they were taken
        size_t n = strlen(e->d_name);
        if (n < 13 || n >= 40 || strncmp(e->d_name, today, 9) != 0 ||
            strcasecmp(e->d_name + n - 4, ".bmp") != 0) continue;
        if (s->count < ST_MAX) { memcpy(s->names[s->count++], e->d_name, n + 1); continue; }
        int oldest = 0;
        for (int i = 1; i < s->count; i++)
            if (strcmp(s->names[i], s->names[oldest]) < 0) oldest = i;
        if (strcmp(e->d_name, s->names[oldest]) > 0) memcpy(s->names[oldest], e->d_name, n + 1);
    }
    closedir(d);
    qsort(s->names, (size_t)s->count, sizeof(s->names[0]), cmp_name);
    if (s->count) s->coverOk = load(s, s->count - 1, s->cover);
}

/* ---------------- input ---------------- */

static void show(Stories *s, int n) {
    s->index = n;
    s->elapsed = 0;
    s->ok = load(s, n, s->img);
    s->lastMs = 0;                       // loading time does not count
}

static void play(Stories *s) {
    if (!s->count) return;
    s->playing = true;
    s->paused = false;
    show(s, 0);
}

static void step(Stories *s, int by) {
    int n = s->index + by;
    if (n >= s->count) s->playing = false;       // the story is over
    else show(s, n < 0 ? 0 : n);
}

bool stories_update(Stories *s, const StoryInput *in) {
    int x = in->x, y = in->y;
    if (!s->playing) {
        if ((in->keys & SK_B) || (in->down && y < 38 && x < 48)) return true;
        if ((in->keys & SK_A) || (in->down && ((y >= 48 && y < 128) || (y >= 150 && y < 178)))) play(s);
        if (!s->count && in->down && y >= 180) return true;      // "take a snap"
        return false;
    }
    if ((in->keys & SK_B) || (in->down && y >= 180)) { s->playing = false; return false; }
    int by = 0;
    bool pause = (in->keys & SK_A) != 0;
    if (in->keys & SK_LEFT) by = -1;
    if (in->keys & SK_RIGHT) by = 1;
    if (in->down && y >= 60 && y < 140) {
        if (x < 107) by = -1;
        else if (x >= 213) by = 1;
        else pause = true;
    }
    if (by) { step(s, by); return false; }
    if (pause) s->paused = !s->paused;

    if (s->lastMs && !s->paused && in->ms > s->lastMs) s->elapsed += (unsigned)(in->ms - s->lastMs);
    s->lastMs = in->ms;
    if (s->elapsed >= ST_SHOW_MS) step(s, 1);
    return false;
}

/* ---------------- drawing ---------------- */

static void disc(uint16_t *c, float x, float y, float r, uint32_t rgb) {
    draw_ellipse(c, x, y, r, r, 0, rgb, 255);
}

static void centred(uint16_t *c, int cx, int y, int scale, const char *t, uint32_t rgb) {
    draw_text(c, cx - text_width(t, scale) / 2, y, scale, t, rgb);
}

static void bar(uint16_t *c, int y, const char *t, uint32_t bg, uint32_t fg) {
    draw_rect(c, 24, y, CW - 48, 28, bg);
    centred(c, CW / 2, y + 7, 2, t, fg);
}

// "8:05 AM" from a name like 20261128_080500_00.bmp
static void taken_at(const char *name, char *out, size_t len) {
    int h, m;
    if (sscanf(name, "%*8d_%2d%2d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60)
        snprintf(out, len, "TODAY %d:%02d %s", h % 12 ? h % 12 : 12, m, h < 12 ? "AM" : "PM");
    else snprintf(out, len, "TODAY");
}

// The snap with a small round picture of itself, inside a ring.
static void bubble(uint16_t *c, const Stories *s, int cx, int cy, int r) {
    disc(c, cx, cy, r + 4, s->count ? PURPLE : 0x45454C);
    disc(c, cx, cy, r + 2, BG);
    disc(c, cx, cy, r, CARD);
    if (!s->coverOk) return;
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++)
            if (i * i + j * j <= r * r)          // the middle square of the picture
                c[(cy + j) * CW + cx + i] =
                    s->cover[(j + r) * (CH - 1) / (2 * r) * CW + (CW - CH) / 2 + (i + r) * (CH - 1) / (2 * r)];
}

static void draw_home(const Stories *s, uint16_t *top, uint16_t *bottom) {
    if (s->coverOk) memcpy(top, s->cover, sizeof(s->cover));
    else draw_rect(top, 0, 0, CW, CH, BLACK);
    if (!s->count) {
        centred(top, CW / 2, 100, 2, "NO SNAPS TODAY", WHITE);
        centred(top, CW / 2, 126, 1, "YOUR STORY STARTS WITH YOUR NEXT SNAP", GREY);
    }

    draw_rect(bottom, 0, 0, CW, CH, BG);
    draw_line(bottom, 21, 10, 15, 18, 2.8f, WHITE);              // back
    draw_line(bottom, 15, 18, 21, 26, 2.8f, WHITE);
    centred(bottom, CW / 2, 11, 2, "STORIES", WHITE);

    draw_rect(bottom, 8, 48, CW - 16, 80, CARD);
    bubble(bottom, s, 52, 88, 28);
    draw_text(bottom, 96, 70, 2, "MY STORY", WHITE);
    char t[48];
    if (s->count) snprintf(t, sizeof(t), "%d SNAP%s FROM TODAY", s->count, s->count == 1 ? "" : "S");
    else snprintf(t, sizeof(t), "NOTHING YET");
    draw_text(bottom, 96, 94, 1, t, GREY);
    if (s->count) {
        bar(bottom, 150, "A: PLAY", YELLOW, BLACK);
        centred(bottom, CW / 2, 220, 1, "B: BACK TO CAMERA", GREY);
    } else {
        bar(bottom, 190, "TAKE A SNAP", YELLOW, BLACK);
    }
}

static void draw_playing(const Stories *s, uint16_t *top, uint16_t *bottom) {
    if (s->ok) memcpy(top, s->img, sizeof(s->img));
    else {
        draw_rect(top, 0, 0, CW, CH, BLACK);
        centred(top, CW / 2, 108, 2, "CANT OPEN THIS FILE", WHITE);
    }
    // one bar per snap along the top; the current one fills up
    int n = s->count;
    for (int i = 0; i < n; i++) {
        int x0 = 4 + i * (CW - 8) / n, x1 = 4 + (i + 1) * (CW - 8) / n;
        int w = x1 - x0 - (n <= 40 ? 2 : 1);
        draw_rect(top, x0, 4, w, 3, 0x66666C);
        int full = i < s->index ? w : i == s->index ? (int)(w * s->elapsed / ST_SHOW_MS) : 0;
        draw_rect(top, x0, 4, full > w ? w : full, 3, WHITE);
    }
    char when[32];
    taken_at(s->names[s->index], when, sizeof(when));
    draw_rect(top, 6, 12, text_width(when, 1) + 12, 15, BLACK);
    draw_text(top, 12, 16, 1, when, WHITE);

    draw_rect(bottom, 0, 0, CW, CH, BG);
    char t[32];
    snprintf(t, sizeof(t), "SNAP %d OF %d", s->index + 1, n);
    centred(bottom, CW / 2, 20, 2, t, WHITE);
    if (s->paused) centred(bottom, CW / 2, 44, 1, "PAUSED", YELLOW);

    disc(bottom, 54, 100, 30, CARD);                             // previous
    draw_line(bottom, 60, 86, 46, 100, 4.0f, WHITE);
    draw_line(bottom, 46, 100, 60, 114, 4.0f, WHITE);
    disc(bottom, 266, 100, 30, CARD);                            // next
    draw_line(bottom, 260, 86, 274, 100, 4.0f, WHITE);
    draw_line(bottom, 274, 100, 260, 114, 4.0f, WHITE);
    disc(bottom, 160, 100, 36, YELLOW);                          // pause / play
    if (s->paused)
        for (int i = 0; i < 26; i++) draw_rect(bottom, 150 + i, 100 - (26 - i) / 2 - 3, 1, 26 - i + 6, BLACK);
    else {
        draw_rect(bottom, 147, 84, 9, 32, BLACK);
        draw_rect(bottom, 164, 84, 9, 32, BLACK);
    }
    centred(bottom, CW / 2, 156, 1, "A: PAUSE   D-PAD: PREVIOUS / NEXT", GREY);
    bar(bottom, 190, "B: CLOSE", CARD, WHITE);
}

void stories_draw(const Stories *s, uint16_t *top, uint16_t *bottom) {
    if (s->playing) draw_playing(s, top, bottom);
    else draw_home(s, top, bottom);
}
