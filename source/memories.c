// memories.c - the Memories screen, drawn to look like Snapchat's.
//
// Bottom (touch) screen, grid page:  header, search bar, "Recently Added" row and a
// scrolling grid of all snaps, newest first. Drag to scroll, tap a snap to open it,
// hold a snap (or tap the check circle) to pick several and delete them.
// Bottom screen, photo page: the snap full size. Swipe or tap the arrows to move.
// Top screen: the snap that is open, with its date.
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "memories.h"

#define BG      0x121214
#define CARD    0x26262A
#define GREY    0x8E8E93
#define YELLOW  0xFFFC00
#define BLUE    0x0EADFF
#define RED     0xFF3B30

// Page layout. "Content" y = 0 is TOP_Y on the screen and moves up as you scroll.
#define HEAD_H 30
#define TOP_Y 32
#define SEARCH_Y 0
#define RECENT_LABEL_Y 28
#define RECENT_Y 40
#define SNAPS_LABEL_Y 106
#define GRID_Y 118
#define ROWP 60                          // grid row height
#define COLS 4
#define GX0 3                            // left edge of the first tile
#define GPITCH 79                        // tile width + gap

static uint16_t pack565(uint32_t rgb) {
    return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

/* ---------------- reading photos ---------------- */

static uint32_t rd32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Reads a BMP (16, 24 or 32 bit, no compression) and shrinks or stretches it to ow x oh.
// It only reads the rows it needs, so a small thumbnail is quick to make.
bool mem_load_bmp(const char *path, uint16_t *out, int ow, int oh) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t h[54];
    if (fread(h, 1, 54, f) != 54 || h[0] != 'B' || h[1] != 'M') { fclose(f); return false; }
    uint32_t off = rd32(h + 10);
    int w = (int)rd32(h + 18), ht = (int)rd32(h + 22);
    int bpp = h[28] | (h[29] << 8);
    uint32_t comp = rd32(h + 30);
    bool topDown = ht < 0;
    if (topDown) ht = -ht;
    if (w <= 0 || ht <= 0 || w > 4096 || ht > 4096 || (comp != 0 && comp != 3) ||
        (bpp != 16 && bpp != 24 && bpp != 32)) {
        fclose(f);
        return false;
    }
    size_t bytes = (size_t)bpp / 8;
    size_t stride = ((size_t)w * bpp + 31) / 32 * 4;
    uint8_t *row = (uint8_t *)malloc(stride);
    if (!row) { fclose(f); return false; }

    bool ok = true;
    int lastRow = -1;
    for (int oy = 0; oy < oh && ok; oy++) {
        int sy = (oy * 2 + 1) * ht / (oh * 2);          // sample the middle of each cell
        int fileRow = topDown ? sy : ht - 1 - sy;
        if (fileRow != lastRow) {
            if (fseek(f, (long)(off + (size_t)fileRow * stride), SEEK_SET) != 0 ||
                fread(row, 1, stride, f) < (size_t)w * bytes) {
                ok = false;
                break;
            }
            lastRow = fileRow;
        }
        for (int ox = 0; ox < ow; ox++) {
            const uint8_t *p = row + (size_t)((ox * 2 + 1) * w / (ow * 2)) * bytes;
            int r, g, b;
            if (bpp == 16) {
                uint16_t v = (uint16_t)(p[0] | (p[1] << 8));
                if (comp == 3) { out[oy * ow + ox] = v; continue; }   // already 565
                r = ((v >> 10) & 31) << 3; g = ((v >> 5) & 31) << 3; b = (v & 31) << 3;
            } else {
                b = p[0]; g = p[1]; r = p[2];
            }
            out[oy * ow + ox] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
    free(row);
    fclose(f);
    return ok;
}

static void photo_path(const Memories *m, int n, char *path, size_t len) {
    snprintf(path, len, "%s/%s", m->dir, m->names[n]);
}

static void load_full(Memories *m) {
    m->ok = false;
    if (m->count <= 0) return;
    char path[160];
    photo_path(m, m->index, path, sizeof(path));
    m->ok = mem_load_bmp(path, m->img, CW, CH);
}

static void load_thumb(Memories *m, int n) {
    char path[160];
    photo_path(m, n, path, sizeof(path));
    m->thumbState[n] = mem_load_bmp(path, m->thumbs[n], MEM_TW, MEM_TH) ? 1 : 2;
}

/* ---------------- the list of photos ---------------- */

static int cmp_name(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

// Fills out[] with the .bmp names, sorted. If there are more than MEM_MAX, the newest are kept.
static int scan(const char *dir, char out[][40]) {
    int count = 0;
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            size_t n = strlen(e->d_name);
            if (n < 5 || n >= 40 || e->d_name[0] == '.') continue;
            if (strcasecmp(e->d_name + n - 4, ".bmp") != 0) continue;
            if (count < MEM_MAX) { memcpy(out[count++], e->d_name, n + 1); continue; }
            int oldest = 0;
            for (int i = 1; i < count; i++)
                if (strcmp(out[i], out[oldest]) < 0) oldest = i;
            if (strcmp(e->d_name, out[oldest]) > 0) memcpy(out[oldest], e->d_name, n + 1);
        }
        closedir(d);
    }
    qsort(out, (size_t)count, 40, cmp_name);
    return count;
}

static void set_select(Memories *m, bool on) {
    m->select = on;
    memset(m->sel, 0, sizeof(m->sel));
    m->nsel = 0;
    m->confirm = false;
}

static void toggle(Memories *m, int n) {
    m->sel[n] = !m->sel[n];
    m->nsel += m->sel[n] ? 1 : -1;
    m->confirm = false;
}

void mem_open(Memories *m, const char *dir) {
    static char fresh[MEM_MAX][40];
    snprintf(m->dir, sizeof(m->dir), "%s", dir);
    int n = scan(m->dir, fresh);
    // New snaps are always the newest names, so thumbnails at the front of the list are still right.
    int keep = 0;
    while (keep < m->count && keep < n && strcmp(m->names[keep], fresh[keep]) == 0) keep++;
    for (int i = 0; i < n; i++) {
        memcpy(m->names[i], fresh[i], 40);
        if (i >= keep) m->thumbState[i] = 0;
    }
    m->count = n;

    set_select(m, false);
    m->view = MV_GRID;
    m->scroll = m->vel = 0;
    m->touching = m->dragged = m->longDone = false;
    m->note[0] = 0;
    m->noteUntil = 0;
    m->index = n ? n - 1 : 0;            // the newest
    load_full(m);
}

// Deletes the marked photos from the card and from the list. Returns how many went.
static int remove_marked(Memories *m, const bool *mark) {
    int w = 0, removed = 0;
    for (int r = 0; r < m->count; r++) {
        if (mark[r]) {
            char path[160];
            photo_path(m, r, path, sizeof(path));
            if (remove(path) == 0) { removed++; continue; }   // if the card refuses, keep it listed
        }
        if (w != r) {
            memcpy(m->names[w], m->names[r], sizeof(m->names[0]));
            memcpy(m->thumbs[w], m->thumbs[r], sizeof(m->thumbs[0]));
            m->thumbState[w] = m->thumbState[r];
        }
        w++;
    }
    m->count = w;
    return removed;
}

static void say(Memories *m, const char *text) {
    snprintf(m->note, sizeof(m->note), "%s", text);
    m->noteUntil = (unsigned long long)-1;      // mem_draw starts the clock
}

/* ---------------- layout ---------------- */

static int max_scroll(const Memories *m) {
    if (m->count == 0) return 0;
    int rows = (m->count + COLS - 1) / COLS;
    int h = GRID_Y + rows * ROWP + 40;           // room for the round button at the bottom
    int view = CH - TOP_Y;
    return h > view ? h - view : 0;
}

// Which photo (index in names[]) is under this screen position? -1 if none.
static int tile_at(const Memories *m, int x, int y) {
    if (m->count == 0 || y < TOP_Y || x < GX0) return -1;
    int col = (x - GX0) / GPITCH;
    if (col >= COLS || (x - GX0) % GPITCH >= MEM_TW) return -1;
    int cy = y - TOP_Y + (int)m->scroll;
    int d = -1;
    if (cy >= RECENT_Y && cy < RECENT_Y + MEM_TH) {
        d = col;
    } else if (cy >= GRID_Y && (cy - GRID_Y) % ROWP < MEM_TH) {
        d = (cy - GRID_Y) / ROWP * COLS + col;
    }
    if (d < 0 || d >= m->count) return -1;
    return m->count - 1 - d;
}

typedef struct { int kind, x, y, w, h; } FloatBtn;      // kind: 0 none, 1 snap, 2 delete

static FloatBtn float_btn(const Memories *m) {
    if (!m->select) return (FloatBtn){ 1, 206, 206, 108, 28 };
    if (m->nsel > 0) return (FloatBtn){ 2, 98, 206, 216, 28 };
    return (FloatBtn){ 0, 0, 0, 0, 0 };
}

static bool inside(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

// Is this photo's thumbnail on the screen right now? (position in the list: 0 = newest)
static bool on_screen(const Memories *m, int d) {
    int top = (int)m->scroll, bot = top + (CH - TOP_Y);
    if (d < COLS && RECENT_Y + MEM_TH > top && RECENT_Y < bot) return true;
    int y = GRID_Y + d / COLS * ROWP;
    return y + MEM_TH > top && y < bot;
}

// Makes one thumbnail per frame, the ones you can see first.
static void pump_thumb(Memories *m) {
    for (int pass = 0; pass < 2; pass++)
        for (int d = 0; d < m->count; d++) {
            int n = m->count - 1 - d;
            if (m->thumbState[n]) continue;
            if (pass == 0 && !on_screen(m, d)) continue;
            load_thumb(m, n);
            return;
        }
}

/* ---------------- input ---------------- */

// Follows the stylus. Returns true on the frame it lifts.
static bool touch_track(Memories *m, const MemInput *in) {
    if (in->down && !m->touching) {
        m->touching = true;
        m->dragged = m->longDone = false;
        m->sx = m->lastX = in->x;
        m->sy = m->lastY = in->y;
        m->downMs = in->ms;
        m->vel = 0;
        m->startScroll = m->scroll;
    }
    if (!m->touching) return false;
    if (in->held) return false;
    m->touching = false;
    return true;
}

static void open_photo(Memories *m, int n) {
    m->index = n;
    m->view = MV_PHOTO;
    m->confirm = false;
    m->note[0] = 0;
    load_full(m);
}

static void delete_selected(Memories *m) {
    int wanted = m->nsel;
    int removed = remove_marked(m, m->sel);
    set_select(m, false);
    m->index = m->count ? m->count - 1 : 0;
    load_full(m);
    m->scroll = fminf(m->scroll, (float)max_scroll(m));
    char t[32];
    if (removed == wanted) snprintf(t, sizeof(t), "DELETED %d", removed);
    else snprintf(t, sizeof(t), "COULD NOT DELETE ALL");
    say(m, t);
}

// Returns true to leave Memories.
static bool tap_grid(Memories *m, int x, int y) {
    if (y < HEAD_H) {
        if (x < 48) {                                    // chevron
            if (m->select) set_select(m, false);
            else return true;
        } else if (x >= CW - 48) {                       // check circle
            set_select(m, !m->select);
        }
        return false;
    }
    FloatBtn fb = float_btn(m);
    if (fb.kind && inside(x, y, fb.x, fb.y, fb.w, fb.h)) {
        if (fb.kind == 1) return true;                   // SNAP: back to the camera
        if (m->confirm) delete_selected(m);
        else m->confirm = true;
        return false;
    }
    int n = tile_at(m, x, y);
    if (n < 0) return false;
    if (m->select) { toggle(m, n); return false; }
    if (m->count > 4 && n == m->count - 4 && y - TOP_Y + (int)m->scroll < RECENT_Y + MEM_TH) {
        m->scroll = fminf((float)(SNAPS_LABEL_Y - 6), (float)max_scroll(m));   // the "+N" tile
        return false;
    }
    open_photo(m, n);
    return false;
}

static bool update_grid(Memories *m, const MemInput *in) {
    bool leave = false;
    if (in->keys & MK_B) {
        if (m->select) set_select(m, false);
        else leave = true;
    }
    if ((in->keys & MK_X) && m->count) set_select(m, !m->select);
    if (in->keys & MK_UP) { m->scroll -= 60; m->vel = 0; }
    if (in->keys & MK_DOWN) { m->scroll += 60; m->vel = 0; }
    if (m->count > 0 && (in->keys & (MK_L | MK_R | MK_LEFT | MK_RIGHT))) {   // flip the top screen
        int step = (in->keys & (MK_L | MK_LEFT)) ? -1 : 1;
        m->index = (m->index + step + m->count) % m->count;
        load_full(m);
    }
    if ((in->keys & MK_A) && m->count) open_photo(m, m->index);

    int prevY = m->lastY;
    bool lifted = touch_track(m, in);
    if (m->touching) {
        m->lastX = in->x;
        m->lastY = in->y;
        if (!m->dragged && abs(in->y - m->sy) > 6) m->dragged = true;
        if (m->dragged) {                                // the page follows the stylus
            m->scroll = m->startScroll + (float)(m->sy - in->y);
            m->vel = m->vel * 0.5f + (float)(prevY - in->y) * 0.5f;
        } else if (!m->longDone && !m->select && in->ms - m->downMs > 450) {
            int n = tile_at(m, m->sx, m->sy);            // hold a snap to start picking
            if (n >= 0) { m->longDone = true; set_select(m, true); toggle(m, n); }
        }
    }
    if (lifted && !m->dragged && !m->longDone) leave |= tap_grid(m, m->sx, m->sy);
    if (!m->touching) {                                  // coast after a flick
        m->scroll += m->vel;
        m->vel *= 0.92f;
        if (fabsf(m->vel) < 0.3f) m->vel = 0;
    }
    float top = (float)max_scroll(m);
    if (m->scroll < 0) { m->scroll = 0; m->vel = 0; }
    if (m->scroll > top) { m->scroll = top; m->vel = 0; }
    return leave;
}

static void update_photo(Memories *m, const MemInput *in) {
    int step = 0;
    bool del = (in->keys & MK_X) != 0, back = (in->keys & MK_B) != 0;
    if (in->keys & (MK_LEFT | MK_L)) step = -1;
    if (in->keys & (MK_RIGHT | MK_R)) step = 1;

    if (touch_track(m, in)) {
        int dx = m->lastX - m->sx, dy = m->lastY - m->sy;
        if (abs(dx) > 40 && abs(dx) > 2 * abs(dy)) {
            step = dx < 0 ? 1 : -1;                      // swipe left = next (newer) photo
        } else if (abs(dx) < 12 && abs(dy) < 12) {
            int x = m->sx, y = m->sy;
            if (x < 44 && y < 38) back = true;
            else if (x >= CW - 44 && y < 38) del = true;
            else if (x < 44 && y >= 84 && y < 156) step = -1;
            else if (x >= CW - 44 && y >= 84 && y < 156) step = 1;
        }
    }
    if (m->touching) { m->lastX = in->x; m->lastY = in->y; }

    if (back || m->count == 0) { m->view = MV_GRID; m->confirm = false; return; }
    if (step) {
        m->index = (m->index + step + m->count) % m->count;
        m->confirm = false;
        load_full(m);
    } else if (del) {
        if (!m->confirm) { m->confirm = true; return; }
        bool mark[MEM_MAX] = { false };
        mark[m->index] = true;
        int removed = remove_marked(m, mark);
        m->confirm = false;
        say(m, removed ? "DELETED 1" : "COULD NOT DELETE");
        if (m->count == 0) { m->index = 0; m->ok = false; m->view = MV_GRID; return; }
        if (m->index >= m->count) m->index = m->count - 1;
        load_full(m);
    }
}

bool mem_update(Memories *m, const MemInput *in) {
    if (m->view == MV_PHOTO) {
        update_photo(m, in);
        return false;
    }
    if (!(m->touching && m->dragged)) pump_thumb(m);     // keep dragging smooth
    return update_grid(m, in);
}

/* ---------------- drawing ---------------- */

static void pill(uint16_t *c, int x, int y, int w, int h, uint32_t rgb) {
    float r = h / 2.0f;
    draw_ellipse(c, x + r, y + r - 0.5f, r, r, 0, rgb, 255);
    draw_ellipse(c, x + w - r - 1, y + r - 0.5f, r, r, 0, rgb, 255);
    draw_rect(c, x + (int)r, y, w - 2 * (int)r, h, rgb);
}

static void pill_text(uint16_t *c, int x, int y, int w, int h, const char *s, uint32_t rgb) {
    pill(c, x, y, w, h, rgb);
    draw_text(c, x + (w - text_width(s, 1)) / 2, y + (h - 7) / 2, 1, s, 0xFFFFFF);
}

static void label(uint16_t *c, int x, int y, const char *s, uint32_t rgb) {   // fake bold
    draw_text(c, x, y, 1, s, rgb);
    draw_text(c, x + 1, y, 1, s, rgb);
}

static void chevron_down(uint16_t *c, float x, float y, float s, uint32_t rgb) {
    draw_line(c, x - s, y - s * 0.45f, x, y + s * 0.55f, 2.6f, rgb);
    draw_line(c, x, y + s * 0.55f, x + s, y - s * 0.45f, 2.6f, rgb);
}

static void chevron_side(uint16_t *c, float x, float y, int dir, uint32_t rgb) {
    draw_line(c, x + 3 * -dir, y - 8, x + 3 * dir, y, 2.6f, rgb);
    draw_line(c, x + 3 * dir, y, x + 3 * -dir, y + 8, 2.6f, rgb);
}

static void check_mark(uint16_t *c, float x, float y, float s, float thick, uint32_t rgb) {
    draw_line(c, x - s * 0.5f, y, x - s * 0.12f, y + s * 0.4f, thick, rgb);
    draw_line(c, x - s * 0.12f, y + s * 0.4f, x + s * 0.5f, y - s * 0.38f, thick, rgb);
}

static void magnifier(uint16_t *c, float x, float y, uint32_t rgb) {
    draw_ellipse(c, x - 1.5f, y - 1.5f, 6, 6, 0, rgb, 255);
    draw_ellipse(c, x - 1.5f, y - 1.5f, 4, 4, 0, CARD, 255);
    draw_line(c, x + 2.5f, y + 2.5f, x + 7, y + 7, 2.4f, rgb);
}

static void plus_mark(uint16_t *c, float x, float y, float s, uint32_t rgb) {
    draw_line(c, x - s, y, x + s, y, 3.0f, rgb);
    draw_line(c, x, y - s, x, y + s, 3.0f, rgb);
}

static void trash_can(uint16_t *c, float x, float y, uint32_t rgb) {
    draw_line(c, x - 6, y - 4, x + 6, y - 4, 2.2f, rgb);
    draw_line(c, x - 2, y - 7, x + 2, y - 7, 2.2f, rgb);
    draw_line(c, x - 4.5f, y - 2, x - 3.5f, y + 7, 2.2f, rgb);
    draw_line(c, x - 3.5f, y + 7, x + 3.5f, y + 7, 2.2f, rgb);
    draw_line(c, x + 3.5f, y + 7, x + 4.5f, y - 2, 2.2f, rgb);
}

// One thumbnail with rounded corners. tint: 0 normal, 1 picked (blue), 2 dimmed.
static void draw_thumb(uint16_t *c, const Memories *m, int n, int x, int y, int tint) {
    static const uint8_t CORNER[4] = { 3, 2, 1, 1 };
    const uint16_t *src = m->thumbState[n] == 1 ? m->thumbs[n] : NULL;
    uint16_t blue = pack565(BLUE), empty = pack565(0x2A2A2E);
    for (int j = 0; j < MEM_TH; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= CH) continue;
        int cut = j < 4 ? CORNER[j] : j >= MEM_TH - 4 ? CORNER[MEM_TH - 1 - j] : 0;
        for (int i = cut; i < MEM_TW - cut; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= CW) continue;
            uint16_t p = src ? src[j * MEM_TW + i] : empty;
            if (tint == 1) p = (uint16_t)(((p >> 1) & 0x7BEF) + ((p >> 2) & 0x39E7) + ((blue >> 2) & 0x39E7));   // 3/4 photo, 1/4 blue
            else if (tint == 2) p = (uint16_t)((p >> 1) & 0x7BEF);
            c[yy * CW + xx] = p;
        }
    }
    if (m->thumbState[n] == 2) draw_text(c, x + MEM_TW / 2 - 5, y + MEM_TH / 2 - 7, 2, "!", GREY);
}

static void draw_tile(uint16_t *c, const Memories *m, int n, int x, int y, bool dim) {
    bool picked = m->select && m->sel[n];
    draw_thumb(c, m, n, x, y, picked ? 1 : dim ? 2 : 0);
    if (!m->select) return;
    float bx = x + MEM_TW - 11, by = y + 11;             // round badge in the corner
    draw_ellipse(c, bx, by, 8, 8, 0, 0xFFFFFF, 255);
    if (picked) {
        draw_ellipse(c, bx, by, 6.5f, 6.5f, 0, BLUE, 255);
        check_mark(c, bx, by, 8, 1.8f, 0xFFFFFF);
    } else {
        draw_ellipse(c, bx, by, 6.5f, 6.5f, 0, 0x303034, 255);
    }
}

// "TODAY 8:17 AM", "NOV 28 2026 8:17 AM", or just the file name if it is not one of ours.
static void date_label(const char *name, char *out, size_t len) {
    static const char *const MON[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    int y, mo, d, hh, mm;
    if (sscanf(name, "%4d%2d%2d_%2d%2d", &y, &mo, &d, &hh, &mm) == 5 &&
        mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && hh < 24 && mm < 60) {
        time_t t = time(NULL);
        struct tm *lt = localtime(&t);
        char day[16];
        if (lt && lt->tm_year + 1900 == y && lt->tm_mon + 1 == mo && lt->tm_mday == d)
            snprintf(day, sizeof(day), "TODAY");
        else
            snprintf(day, sizeof(day), "%s %d %d", MON[mo - 1], d, y);
        snprintf(out, len, "%s  %d:%02d %s", day, hh % 12 ? hh % 12 : 12, mm, hh < 12 ? "AM" : "PM");
        return;
    }
    size_t i = 0;
    for (; name[i] && name[i] != '.' && i < len - 1; i++)
        out[i] = (char)((name[i] >= 'a' && name[i] <= 'z') ? name[i] - 32 : name[i]);
    out[i] = 0;
}

static void draw_header(uint16_t *c, const Memories *m) {
    draw_rect(c, 0, 0, CW, HEAD_H, BG);
    if (m->scroll > 0) draw_rect(c, 0, HEAD_H - 1, CW, 1, 0x2A2A2E);
    chevron_down(c, 24, 13, 8, 0xFFFFFF);
    char title[24] = "MEMORIES";
    if (m->select) {
        if (m->nsel) snprintf(title, sizeof(title), "%d SELECTED", m->nsel);
        else snprintf(title, sizeof(title), "SELECT");
    }
    draw_text(c, (CW - text_width(title, 2)) / 2, 8, 2, title, 0xFFFFFF);
    if (m->select) {                                     // check circle, lit while picking
        draw_ellipse(c, 296, 15, 11, 11, 0, BLUE, 255);
        check_mark(c, 296, 15, 10, 2.2f, 0xFFFFFF);
    } else {
        draw_ellipse(c, 296, 15, 11, 11, 0, GREY, 255);
        draw_ellipse(c, 296, 15, 9.2f, 9.2f, 0, BG, 255);
        check_mark(c, 296, 15, 10, 2.2f, GREY);
    }
}

static void draw_float(uint16_t *c, const Memories *m) {
    FloatBtn fb = float_btn(m);
    if (!fb.kind) return;
    int cy = fb.y + fb.h / 2;
    if (fb.kind == 1) {
        pill(c, fb.x, fb.y, fb.w, fb.h, YELLOW);
        int x0 = fb.x + (fb.w - 66) / 2;
        plus_mark(c, x0 + 6, cy, 6, 0x16161A);
        draw_text(c, x0 + 20, cy - 7, 2, "SNAP", 0x16161A);
    } else {
        char t[40];
        pill(c, fb.x, fb.y, fb.w, fb.h, RED);
        if (m->confirm) snprintf(t, sizeof(t), "TAP AGAIN TO DELETE %d", m->nsel);
        else snprintf(t, sizeof(t), "DELETE %d", m->nsel);
        int scale = m->confirm ? 1 : 2;
        draw_text(c, fb.x + (fb.w - text_width(t, scale)) / 2, cy - (scale == 2 ? 7 : 3), scale, t, 0xFFFFFF);
    }
}

static void draw_note(uint16_t *c, Memories *m, unsigned long long now) {
    if (!m->note[0]) return;
    if (m->noteUntil == (unsigned long long)-1) m->noteUntil = now + 2000;   // start the 2 second clock
    if (now >= m->noteUntil) { m->note[0] = 0; return; }
    int w = text_width(m->note, 1) + 24;
    pill_text(c, (CW - w) / 2, 172, w, 20, m->note, 0x38383D);
}

static void draw_grid(uint16_t *c, Memories *m, unsigned long long now) {
    draw_rect(c, 0, 0, CW, CH, BG);
    int oy = TOP_Y - (int)m->scroll;

    pill(c, 8, oy + SEARCH_Y, 304, 20, CARD);
    magnifier(c, 25, oy + SEARCH_Y + 10, GREY);
    draw_text(c, 40, oy + SEARCH_Y + 7, 1, "SEARCH MEMORIES", 0x6E6E74);

    if (m->count == 0) {
        const char *a = "NO SNAPS YET", *b = "TAKE A SNAP AND IT SHOWS UP HERE";
        draw_text(c, (CW - text_width(a, 2)) / 2, oy + 80, 2, a, 0xFFFFFF);
        draw_text(c, (CW - text_width(b, 1)) / 2, oy + 104, 1, b, GREY);
    } else {
        label(c, 8, oy + RECENT_LABEL_Y, "RECENTLY ADDED", GREY);
        for (int d = 0; d < COLS && d < m->count; d++) {
            int x = GX0 + d * GPITCH, y = oy + RECENT_Y;
            bool more = d == COLS - 1 && m->count > COLS && !m->select;
            draw_tile(c, m, m->count - 1 - d, x, y, more);
            if (more) {
                char t[16];
                snprintf(t, sizeof(t), "+%d", m->count - COLS);
                draw_text(c, x + (MEM_TW - text_width(t, 2)) / 2, y + (MEM_TH - 14) / 2, 2, t, 0xFFFFFF);
            }
        }
        char head[24];
        snprintf(head, sizeof(head), "%d SNAP%s", m->count, m->count == 1 ? "" : "S");
        label(c, 8, oy + SNAPS_LABEL_Y, head, GREY);
        for (int d = 0; d < m->count; d++) {
            int y = oy + GRID_Y + d / COLS * ROWP;
            if (y + MEM_TH <= HEAD_H || y >= CH) continue;
            draw_tile(c, m, m->count - 1 - d, GX0 + d % COLS * GPITCH, y, false);
        }
    }
    draw_header(c, m);
    draw_float(c, m);
    draw_note(c, m, now);
}

static void round_button(uint16_t *c, int x, int y, int r, uint32_t rgb, int alpha) {
    draw_ellipse(c, x, y, r, r, 0, rgb, alpha);
}

static void draw_photo(uint16_t *c, Memories *m, unsigned long long now) {
    if (m->ok) {
        memcpy(c, m->img, sizeof(m->img));
    } else {
        draw_rect(c, 0, 0, CW, CH, 0x000000);
        const char *t = "CANT OPEN THIS FILE";
        draw_text(c, (CW - text_width(t, 2)) / 2, 108, 2, t, 0xFFFFFF);
    }
    round_button(c, 22, 20, 15, 0x000000, 140);                        // back
    chevron_down(c, 22, 18, 7, 0xFFFFFF);
    round_button(c, 298, 20, 15, m->confirm ? RED : 0x000000, m->confirm ? 255 : 140);   // delete
    trash_can(c, 298, 20, 0xFFFFFF);
    round_button(c, 20, 120, 13, 0x000000, 140);                       // older / newer
    chevron_side(c, 20, 120, -1, 0xFFFFFF);
    round_button(c, 300, 120, 13, 0x000000, 140);
    chevron_side(c, 300, 120, 1, 0xFFFFFF);

    char pos[24];
    snprintf(pos, sizeof(pos), "%d / %d", m->index + 1, m->count);
    pill_text(c, (CW - 64) / 2, 8, 64, 18, pos, 0x101012);
    char when[40];
    date_label(m->names[m->index], when, sizeof(when));
    int w = text_width(when, 1) + 20;
    pill_text(c, (CW - w) / 2, 214, w, 18, when, 0x101012);
    if (m->confirm) pill_text(c, (CW - 190) / 2, 150, 190, 22, "TAP THE BIN AGAIN TO DELETE", RED);
    draw_note(c, m, now);
}

static void draw_top(uint16_t *c, const Memories *m) {
    if (m->count > 0 && m->ok) {
        memcpy(c, m->img, sizeof(m->img));
        char when[40];
        date_label(m->names[m->index], when, sizeof(when));
        int w = text_width(when, 1) + 20;
        pill_text(c, (CW - w) / 2, 214, w, 18, when, 0x101012);
    } else {
        draw_rect(c, 0, 0, CW, CH, 0x000000);
        const char *t = m->count ? "CANT OPEN THIS FILE" : "NO SNAPS YET";
        draw_text(c, (CW - text_width(t, 2)) / 2, 108, 2, t, 0xFFFFFF);
    }
}

void mem_draw(Memories *m, uint16_t *top, uint16_t *bottom, unsigned long long nowMs) {
    draw_top(top, m);
    if (m->view == MV_PHOTO) draw_photo(bottom, m, nowMs);
    else draw_grid(bottom, m, nowMs);
}
