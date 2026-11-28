// art.c - drawing, filters, face tracking, photo saving
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "art.h"

#define PI 3.14159265f

const char *const FILTER_NAMES[F_COUNT] = {
    "THUG LIFE", "KITTY", "MUSTACHE", "STRAWBERRY", "DALMATIAN", "PUPPY"
};
_Static_assert(sizeof(FILTER_NAMES) / sizeof(FILTER_NAMES[0]) == F_COUNT, "FILTER_NAMES must list every filter");

/* ---------------- drawing ---------------- */

static inline uint16_t pack(uint32_t rgb) {
    return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

static inline void blend(uint16_t *p, uint32_t rgb, int a) { // a: 0..255
    uint16_t s = pack(rgb);
    if (a >= 255) { *p = s; return; }
    int dr = *p >> 11, dg = (*p >> 5) & 63, db = *p & 31;
    dr += (((s >> 11) - dr) * a) / 255;
    dg += ((((s >> 5) & 63) - dg) * a) / 255;
    db += (((s & 31) - db) * a) / 255;
    *p = (uint16_t)((dr << 11) | (dg << 5) | db);
}

void draw_rect(uint16_t *c, int x, int y, int w, int h, uint32_t rgb) {
    uint16_t s = pack(rgb);
    for (int j = y < 0 ? 0 : y; j < y + h && j < CH; j++)
        for (int i = x < 0 ? 0 : x; i < x + w && i < CW; i++)
            c[j * CW + i] = s;
}

void draw_ellipse(uint16_t *c, float cx, float cy, float rx, float ry,
                  float angle, uint32_t rgb, int alpha) {
    if (rx < 0.5f || ry < 0.5f) return;
    float cs = cosf(angle), sn = sinf(angle);
    float r = rx > ry ? rx : ry;
    int x0 = (int)(cx - r), x1 = (int)(cx + r) + 1;
    int y0 = (int)(cy - r), y1 = (int)(cy + r) + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > CW - 1) x1 = CW - 1;
    if (y1 > CH - 1) y1 = CH - 1;
    float ix = 1.0f / (rx * rx), iy = 1.0f / (ry * ry);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = x - cx, dy = y - cy;
            float u = dx * cs + dy * sn, v = -dx * sn + dy * cs;
            if (u * u * ix + v * v * iy <= 1.0f) blend(&c[y * CW + x], rgb, alpha);
        }
    }
}

static void dot(uint16_t *c, float x, float y, float r, uint32_t rgb) {
    draw_ellipse(c, x, y, r, r, 0, rgb, 255);
}

void draw_line(uint16_t *c, float x0, float y0, float x1, float y1,
               float thick, uint32_t rgb) {
    float dx = x1 - x0, dy = y1 - y0;
    float r = thick * 0.5f < 0.8f ? 0.8f : thick * 0.5f;
    int n = (int)(sqrtf(dx * dx + dy * dy) / (r * 0.5f)) + 1;
    for (int i = 0; i <= n; i++)
        dot(c, x0 + dx * i / n, y0 + dy * i / n, r, rgb);
}

/* 5x7 font. Each byte is one row, bit 4 = left pixel. */
static const char GLYPHS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:/.-+_!<>";
static const uint8_t FONT[][7] = {
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
 {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
 {14,4,4,4,4,4,14},{7,2,2,2,2,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
 {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
 {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{31,2,4,2,1,17,14},
 {2,6,10,18,31,2,2},{31,16,30,1,1,17,14},{6,8,16,30,17,17,14},{31,1,2,4,8,8,8},
 {14,17,17,14,17,17,14},{14,17,17,15,1,2,12},
 {0,12,12,0,12,12,0},{1,1,2,4,8,16,16},{0,0,0,0,0,12,12},{0,0,0,31,0,0,0},
 {0,4,4,31,4,4,0},{0,0,0,0,0,0,31},{4,4,4,4,4,0,4},
 {2,4,8,16,8,4,2},{8,4,2,1,2,4,8}
};

int text_width(const char *s, int scale) { return (int)strlen(s) * 6 * scale - scale; }

void draw_text(uint16_t *c, int x, int y, int scale, const char *s, uint32_t rgb) {
    for (; *s; s++, x += 6 * scale) {
        int ch = *s;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        const char *p = strchr(GLYPHS, ch);
        if (!p) continue;
        const uint8_t *g = FONT[p - GLYPHS];
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (g[row] & (16 >> col))
                    draw_rect(c, x + col * scale, y + row * scale, scale, scale, rgb);
    }
}

/* ---------------- face tracking ----------------
   Finds the biggest skin-coloured blob in an 80x60 copy of the picture.
   This is NOT real face recognition: hands or wooden walls can fool it. */

#define TW 80
#define TH 60

void tracker_reset(Tracker *t) {
    t->face = (Face){ CW / 2.0f, CH * 0.52f, 120.0f };
    t->found = false;
}

static bool is_skin(uint16_t p) {
    int r = (p >> 11) << 3, g = ((p >> 5) & 63) << 2, b = (p & 31) << 3;
    int y  = (77 * r + 150 * g + 29 * b) >> 8;
    int cb = 128 + ((-43 * r - 85 * g + 128 * b) >> 8);
    int cr = 128 + ((128 * r - 107 * g - 21 * b) >> 8);
    return y > 40 && cb >= 77 && cb <= 127 && cr >= 135 && cr <= 175;
}

void tracker_update(Tracker *t, const uint16_t *img) {
    static uint8_t mask[TW * TH];
    static int16_t label[TW * TH], stack[TW * TH];
    static int rowMin[TH], rowMax[TH];

    for (int y = 0; y < TH; y++)
        for (int x = 0; x < TW; x++)
            mask[y * TW + x] = is_skin(img[(y * 4 + 2) * CW + x * 4 + 2]);
    memset(label, 0, sizeof(label));

    int best = 0, next = 0;
    float bestScore = 0;
    for (int start = 0; start < TW * TH; start++) {
        if (!mask[start] || label[start]) continue;
        int id = ++next, top = 0, area = 0;
        long sx = 0, sy = 0;
        label[start] = id;
        stack[top++] = start;
        while (top) {
            int p = stack[--top], x = p % TW, y = p / TW;
            area++; sx += x; sy += y;
            int n[4] = { x > 0 ? p - 1 : -1, x < TW - 1 ? p + 1 : -1,
                         y > 0 ? p - TW : -1, y < TH - 1 ? p + TW : -1 };
            for (int i = 0; i < 4; i++)
                if (n[i] >= 0 && mask[n[i]] && !label[n[i]]) {
                    label[n[i]] = id;
                    stack[top++] = n[i];
                }
        }
        // too small = noise, too big = skin-coloured background
        if (area < 90 || area > TW * TH * 6 / 10) continue;
        float dx = (float)sx / area * 4 - t->face.x, dy = (float)sy / area * 4 - t->face.y;
        float score = area / (1.0f + sqrtf(dx * dx + dy * dy) / 120.0f); // prefer last spot
        if (score > bestScore) { bestScore = score; best = id; }
    }
    t->found = best != 0;
    if (!best) return;                   // keep the last position

    int first = -1;
    for (int y = 0; y < TH; y++) {
        rowMin[y] = TW; rowMax[y] = -1;
        int count = 0;
        for (int x = 0; x < TW; x++)
            if (label[y * TW + x] == best) {
                if (x < rowMin[y]) rowMin[y] = x;
                if (x > rowMax[y]) rowMax[y] = x;
                count++;
            }
        if (first < 0 && count >= 4) first = y;
    }
    if (first < 0) return;

    // face width = average of the widest rows near the top of the blob
    int widths[TH], n = 0;
    for (int y = first; y < TH && y < first + 28; y++)
        if (rowMax[y] >= 0) widths[n++] = rowMax[y] - rowMin[y] + 1;
    if (n == 0) return;
    for (int i = 1; i < n; i++) {        // sort, largest first
        int v = widths[i], j = i - 1;
        while (j >= 0 && widths[j] < v) { widths[j + 1] = widths[j]; j--; }
        widths[j + 1] = v;
    }
    int top3 = n < 6 ? n : 6, sum = 0;
    for (int i = n / 8; i < n / 8 + top3 && i < n; i++) sum += widths[i];
    int used = n - n / 8 < top3 ? n - n / 8 : top3;
    float w = (float)sum / used;

    // centre line: only rows a face could cover, so neck and chest are ignored
    float mid = 0; int rows = 0;
    for (int y = first; y < TH && y < first + (int)w; y++)
        if (rowMax[y] >= 0) { mid += (rowMin[y] + rowMax[y] + 1) * 0.5f; rows++; }
    if (!rows) return;

    // Vertical position. Bangs or a hat can hide the forehead, so the top of the skin
    // blob is not always the top of the head. The eyes are a better anchor: a left and
    // a right dark spot, side by side, on either side of the middle of the face.
    // (Single strands of hair give one dark spot, not a matching pair.)
    float eyeY = -1.0f, eyeX = -1.0f;
    {
        float midc = mid / rows;
        int lo = first, hi = first + (int)(w * 0.75f);
        if (hi > TH - 2) hi = TH - 2;
        int pairScore[TH] = { 0 };
        float pairMid[TH] = { 0 };               // x halfway between the two eyes, per row
        for (int y = lo; y <= hi; y++) {
            if (rowMax[y] < 0) continue;
            int bestL = 0, bestR = 0, cL = 0, cR = 0;   // longest dark run on each side
            float absL = 0, absR = 0;
            int x0 = rowMin[y] + (int)(w * 0.12f), x1 = rowMax[y] - (int)(w * 0.12f);
            int run = 0, runStart = 0;
            for (int x = x0; x <= x1 + 1; x++) {
                bool dark = x <= x1 && !mask[y * TW + x];
                if (dark) { if (!run) runStart = x; run++; continue; }
                if (run >= 2) {
                    float c = runStart + run * 0.5f - midc;
                    float ac = c < 0 ? -c : c;
                    if (ac >= w * 0.12f && ac <= w * 0.45f) {
                        if (c < 0 && run > bestL) { bestL = run; cL = (int)(-c); absL = c + midc; }
                        if (c > 0 && run > bestR) { bestR = run; cR = (int)c; absR = c + midc; }
                    }
                }
                run = 0;
            }
            if (bestL && bestR && abs(cL - cR) <= (int)(w * 0.2f) + 1)
            {
                pairScore[y] = bestL < bestR ? bestL : bestR;
                pairMid[y] = (absL + absR) * 0.5f;
            }
        }
        int band[TH] = { 0 }, bestSum = 0;
        for (int y = lo + 1; y < hi; y++) {
            band[y] = pairScore[y - 1] + pairScore[y] + pairScore[y + 1];
            if (band[y] > bestSum) bestSum = band[y];
        }
        int need = bestSum / 2 > 5 ? bestSum / 2 : 5;
        if (bestSum >= 5) {
            // Hair strands hang ABOVE the eyes, so take the lowest strong pair.
            int y = hi - 1;
            while (y > lo + 1 && band[y] < need) y--;
            float sy = 0, sn = 0, sx = 0, sxn = 0;
            for (int k = y; k > lo && k > y - 3; k--)
                if (band[k] >= need) { sy += k * (float)band[k]; sn += band[k]; }
            for (int k = y - 2; k <= y + 1; k++)
                if (k > lo && k < hi && pairScore[k] > 0) { sx += pairMid[k] * pairScore[k]; sxn += pairScore[k]; }
            eyeY = (sy / sn) * 4 + 0.5f;            // the dark band centre sits a little low (eyelids, shadow)
            if (sxn > 0) eyeX = sx / sxn * 4;
        }
    }
    float faceW = w * 4 * 1.05f;
    float faceY = eyeY >= 0 ? eyeY + 0.06f * faceW : first * 4 + w * 4 * 0.58f;
    float faceX = mid / rows * 4;
    // Halfway between the eyes is a better centre than the skin blob: a washed-out cheek
    // or one-sided light drags the blob sideways.
    if (eyeX >= 0 && fabsf(eyeX - faceX) < faceW * 0.25f) faceX = eyeX;
    Face seen = { faceX, faceY, faceW };
    if (seen.w < 50) seen.w = 50;
    if (seen.w > 220) seen.w = 220;
    // A sudden far jump must last several frames before we believe it.
    static int jumps = 0;
    float dx = seen.x - t->face.x, dy = seen.y - t->face.y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist > 70.0f && ++jumps < 8) return;
    if (dist <= 70.0f) jumps = 0;

    // Ignore tiny shakes. Follow small moves slowly and big moves faster.
    if (dist > 3.0f) {
        float speed = 0.08f + (dist - 3.0f) / 100.0f;
        if (speed > 0.35f) speed = 0.35f;
        t->face.x += dx * speed;
        t->face.y += dy * speed;
    }
    float dw = seen.w - t->face.w;
    if (fabsf(dw) > 6.0f) t->face.w += dw * 0.07f;
}

/* ---------------- photo ---------------- */

static void le32(uint8_t *p, uint32_t v) {
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

bool save_bmp(const char *path, const uint16_t *img) {
    uint8_t h[54] = { 'B', 'M' };
    uint32_t size = CW * CH * 3;
    le32(h + 2, 54 + size); le32(h + 10, 54); le32(h + 14, 40);
    le32(h + 18, CW); le32(h + 22, CH);
    h[26] = 1; h[28] = 24;
    le32(h + 34, size);

    FILE *fp = fopen(path, "wb");
    if (!fp) return false;
    bool ok = fwrite(h, 1, sizeof(h), fp) == sizeof(h);
    static uint8_t row[CW * 3];
    for (int y = CH - 1; y >= 0 && ok; y--) { // BMP rows go bottom to top
        for (int x = 0; x < CW; x++) {
            uint16_t p = img[y * CW + x];
            int r = p >> 11, g = (p >> 5) & 63, b = p & 31;
            row[x * 3 + 0] = (b << 3) | (b >> 2);
            row[x * 3 + 1] = (g << 2) | (g >> 4);
            row[x * 3 + 2] = (r << 3) | (r >> 2);
        }
        ok = fwrite(row, 1, sizeof(row), fp) == sizeof(row);
    }
    if (fclose(fp) != 0) ok = false;
    if (!ok) remove(path);
    return ok;
}
