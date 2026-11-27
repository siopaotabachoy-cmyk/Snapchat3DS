// art.c - drawing, filters, face tracking, photo saving
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "art.h"

#define PI 3.14159265f

const char *const FILTER_NAMES[F_COUNT] = {
    "DOG", "FLOWER CROWN", "BUNNY", "BEE", "RAINBOW PUKE"
};

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
static const char GLYPHS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:/.-+_!";
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
 {0,4,4,31,4,4,0},{0,0,0,0,0,0,31},{4,4,4,4,4,0,4}
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

/* ---------------- filters ----------------
   Positions are in "face units": x and y are fractions of face width,
   measured from the face centre. Negative y is up.                     */

#define X(u) (f.x + (u) * f.w)
#define Y(v) (f.y + (v) * f.w)
#define S(s) ((s) * f.w)
#define OVAL(u, v, rx, ry, a, col) draw_ellipse(c, X(u), Y(v), S(rx), S(ry), a, col, 255)
#define SOFT(u, v, rx, ry, col, al) draw_ellipse(c, X(u), Y(v), S(rx), S(ry), 0, col, al)
#define LINE(u0, v0, u1, v1, t, col) draw_line(c, X(u0), Y(v0), X(u1), Y(v1), S(t), col)

static void dog(uint16_t *c, Face f, bool action, float time) {
    for (int s = -1; s <= 1; s += 2) {   // floppy ears
        OVAL(s * 0.50f, -0.58f, 0.17f, 0.32f, -s * 0.55f, 0x6B3F22);
        OVAL(s * 0.51f, -0.55f, 0.09f, 0.22f, -s * 0.55f, 0xC98A6B);
    }
    if (action) {                        // tongue, drawn under the nose
        float wag = 0.025f * sinf(time * 7.0f);
        OVAL(wag * 0.5f, 0.50f, 0.14f, 0.13f, 0, 0xE8607A);
        OVAL(wag, 0.64f, 0.14f, 0.17f, 0, 0xE8607A);
        LINE(wag * 0.4f, 0.47f, wag, 0.72f, 0.018f, 0xB83A58);
    }
    OVAL(0, 0.14f, 0.17f, 0.11f, 0, 0x1C1412);       // nose
    SOFT(-0.06f, 0.10f, 0.04f, 0.022f, 0xFFFFFF, 190);
    LINE(0, 0.22f, 0, 0.31f, 0.025f, 0x1C1412);
}

static void flower(uint16_t *c, float x, float y, float r, uint32_t petal, float turn) {
    for (int i = 0; i < 6; i++) {
        float a = turn + i * PI / 3.0f;
        draw_ellipse(c, x + cosf(a) * r * 0.58f, y + sinf(a) * r * 0.58f,
                     r * 0.52f, r * 0.34f, a, petal, 255);
    }
    dot(c, x, y, r * 0.34f, 0xFFD23F);
}

static void flower_crown(uint16_t *c, Face f) {
    static const uint32_t COL[7] = {0xFF7BAC,0xFFFFFF,0xB58CFF,0xFF5E5B,0xB58CFF,0xFFFFFF,0xFF7BAC};
    for (int i = 0; i < 12; i++) {       // leaves
        float u = -0.58f + i * (1.16f / 11.0f);
        float v = -0.64f + 0.65f * u * u;
        OVAL(u, v + 0.02f, 0.11f, 0.045f, (i & 1) ? 0.7f : -0.7f, (i & 1) ? 0x3E8E41 : 0x6BBF59);
    }
    for (int i = 0; i < 7; i++) {
        float u = -0.54f + i * 0.18f;
        float v = -0.65f + 0.65f * u * u;
        flower(c, X(u), Y(v), S(i == 3 ? 0.17f : 0.13f), COL[i], i * 0.4f);
    }
    SOFT(-0.30f, 0.14f, 0.13f, 0.08f, 0xFF6F91, 80);
    SOFT( 0.30f, 0.14f, 0.13f, 0.08f, 0xFF6F91, 80);
}

static void bunny(uint16_t *c, Face f) {
    for (int s = -1; s <= 1; s += 2) {
        OVAL(s * 0.24f, -0.98f, 0.14f, 0.43f, s * 0.20f, 0xF4F1EE);
        OVAL(s * 0.24f, -0.97f, 0.065f, 0.32f, s * 0.20f, 0xF7A8BC);
        SOFT(s * 0.30f, 0.17f, 0.12f, 0.08f, 0xFF8FAB, 90);
        for (int k = -1; k <= 1; k++)    // whiskers
            LINE(s * 0.11f, 0.20f + k * 0.03f, s * 0.52f, 0.18f + k * 0.11f, 0.012f, 0xFFFFFF);
    }
    OVAL(0, 0.14f, 0.085f, 0.058f, 0, 0xF2789A);
    LINE(0, 0.18f, 0, 0.27f, 0.018f, 0x8A4A5C);
    for (int s = -1; s <= 1; s += 2)     // front teeth
        draw_rect(c, (int)X(s < 0 ? -0.078f : 0.006f), (int)Y(0.35f),
                  (int)S(0.072f), (int)S(0.11f), 0xFFFFFF);
}

static void little_bee(uint16_t *c, float x, float y, float r) {
    draw_ellipse(c, x - r * 0.3f, y - r * 0.8f, r * 0.45f, r * 0.70f, -0.5f, 0xDDF4FF, 200);
    draw_ellipse(c, x + r * 0.4f, y - r * 0.8f, r * 0.45f, r * 0.70f,  0.5f, 0xDDF4FF, 200);
    draw_ellipse(c, x, y, r, r * 0.70f, 0, 0xFFC914, 255);
    draw_ellipse(c, x - r * 0.15f, y, r * 0.17f, r * 0.68f, 0, 0x1C1412, 255);
    draw_ellipse(c, x + r * 0.40f, y, r * 0.15f, r * 0.58f, 0, 0x1C1412, 255);
    dot(c, x - r * 0.65f, y - r * 0.15f, r * 0.16f, 0x1C1412);
}

static void bee(uint16_t *c, Face f, float time) {
    float sway = 0.04f * sinf(time * 5.0f);
    LINE(-0.46f, -0.55f, 0.0f, -0.68f, 0.07f, 0x1C1412);   // headband
    LINE( 0.0f, -0.68f, 0.46f, -0.55f, 0.07f, 0x1C1412);
    for (int s = -1; s <= 1; s += 2) {
        LINE(s * 0.20f, -0.65f, s * 0.34f + sway, -1.05f, 0.035f, 0x1C1412);
        OVAL(s * 0.34f + sway, -1.08f, 0.11f, 0.11f, 0, 0xFFC914);
        OVAL(s * 0.34f + sway, -1.08f, 0.11f, 0.03f, 0, 0x1C1412);
        for (int k = 0; k < 3; k++)      // cheek stripes
            LINE(s * 0.21f, 0.10f + k * 0.075f, s * 0.41f, 0.08f + k * 0.075f, 0.035f,
                 k == 1 ? 0x1C1412 : 0xFFC914);
    }
    OVAL(0, 0.14f, 0.07f, 0.05f, 0, 0x1C1412);
    for (int i = 0; i < 2; i++) {        // bees flying around your head
        float a = time * 2.2f + i * PI;
        little_bee(c, X(0.78f * cosf(a)), Y(-0.25f + 0.28f * sinf(a * 1.7f)), S(0.10f));
    }
}

static void rainbow(uint16_t *c, Face f, bool action, float time) {
    if (!action) {                       // small hint until A is held
        draw_text(c, (CW - text_width("HOLD A", 2)) / 2, CH - 24, 2, "HOLD A", 0xFFFFFF);
        return;
    }
    static const uint32_t COL[6] = {0xFF3B30,0xFF9500,0xFFE100,0x34C759,0x1E90FF,0x9B59FF};
    float top = Y(0.38f);
    for (int y = (int)top; y < CH; y++) {
        if (y < 0) continue;
        float d = (y - top) / f.w;
        float half = S(0.16f + 0.22f * (d > 1.2f ? 1.2f : d));
        float mid = f.x + S(0.05f) * sinf(d * 7.0f - time * 12.0f) * (d > 1 ? 1 : d);
        float stripe = half * 2.0f / 6.0f;
        for (int x = (int)(mid - half); x < mid + half; x++) {
            if (x < 0 || x >= CW) continue;
            int i = (int)((x - (mid - half)) / stripe);
            blend(&c[y * CW + x], COL[i < 0 ? 0 : i > 5 ? 5 : i], 225);
        }
    }
    OVAL(0, 0.39f, 0.18f, 0.07f, 0, 0x3A0D16);   // open mouth
    OVAL(0, 0.42f, 0.14f, 0.035f, 0, 0xFF3B30);
    for (int i = 0; i < 14; i++) {       // sparkles
        float p = fmodf(time * 0.9f + i * 0.137f, 1.0f);
        float side = (i & 1) ? 1.0f : -1.0f;
        float x = X(side * (0.18f + 0.45f * p + 0.02f * (i % 5)));
        float y = Y(0.45f + 0.75f * p);
        dot(c, x, y, S(0.030f * (1.0f - p) + 0.008f), COL[i % 6]);
    }
}

void filter_draw(uint16_t *c, int filter, Face f, bool action, float time) {
    switch (filter) {
        case F_DOG:     dog(c, f, action, time); break;
        case F_FLOWER:  flower_crown(c, f); break;
        case F_BUNNY:   bunny(c, f); break;
        case F_BEE:     bee(c, f, time); break;
        case F_RAINBOW: rainbow(c, f, action, time); break;
        default: break;
    }
}

void filter_icon(uint16_t *c, int filter, int cx, int cy) {
    Face f = { (float)cx, (float)cy, 30.0f };
    switch (filter) {
        case F_DOG:    f.y += 6; dog(c, f, true, 0); break;
        case F_FLOWER: flower(c, cx, cy, 18, 0xFF7BAC, 0); break;
        case F_BUNNY:
            f.w = 28; f.y += 26;
            for (int s = -1; s <= 1; s += 2) {
                OVAL(s * 0.24f, -0.98f, 0.17f, 0.43f, s * 0.20f, 0xF4F1EE);
                OVAL(s * 0.24f, -0.97f, 0.08f, 0.32f, s * 0.20f, 0xF7A8BC);
            }
            break;
        case F_BEE: little_bee(c, cx, cy + 5, 17); break;
        case F_RAINBOW: {
            static const uint32_t COL[6] = {0xFF3B30,0xFF9500,0xFFE100,0x34C759,0x1E90FF,0x9B59FF};
            for (int i = 0; i < 6; i++) draw_rect(c, cx - 18 + i * 6, cy - 14, 6, 30, COL[i]);
            break;
        }
        default: break;
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

    Face seen = { mid / rows * 4, first * 4 + w * 4 * 0.58f, w * 4 * 1.05f };
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
