// chat.c - the Chat screen, made to look like Snapchat's.
// The chats are FAKE. "My AI" picks an answer from a built-in list, "Team Snapchat"
// only has a welcome message. No 3DS code in here, only drawing with art.h.
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/stat.h>
#include "chat.h"
#include "extra.h"
#include "memories.h"                    // mem_load_bmp

#define BG     0x111113
#define BAR    0x1A1A1D
#define SELBG  0x18181B
#define LINEC  0x2A2A2E
#define GREY   0x9A9AA0
#define WHITE  0xFFFFFF
#define BLACK  0x000000
#define YELLOW 0xFFFC00
#define BLUE   0x14ADFF
#define RED    0xF23C57
#define CIRCLE 0x232327
#define NAVBG  0x0D0D0F

#define HEAD_H 30                        // header of the conversation (top screen)
#define LIST_Y 70                        // first chat row (bottom screen)
#define ROW_H 48
#define NAV_Y 214
#define IN_Y 172                         // "send a chat" bar
#define IN_H 32

static const char *const NAMES[CT_CUSTOM] = { "MY AI", "TEAM SNAPCHAT", "MIA", "JAY", "LIA", "KAI" };

// The nickname you gave the chat, or else the real name.
static const char *name_of(const Chat *m, int thread) {
    if (m->th[thread].nick[0]) return m->th[thread].nick;
    return thread < CT_CUSTOM ? NAMES[thread] : m->custom[thread - CT_CUSTOM].name;
}

/* ---------------- themes and streaks ---------------- */

typedef struct { const char *name; uint32_t bg, bar, mine, theirs; } Theme;
static const Theme THEMES[N_THEMES] = {
    { "DEFAULT", 0x111113, 0x1A1A1D, 0xF23C57, 0x14ADFF },
    { "SUNSET",  0x2A1220, 0x3D1A2C, 0xFF8A3D, 0xFF4F9A },
    { "OCEAN",   0x0A1C2B, 0x10293D, 0x35D0C8, 0x4DA3FF },
    { "FOREST",  0x0F1D14, 0x16291C, 0xC8E04A, 0x3DDC84 },
    { "GRAPE",   0x1B1230, 0x281A45, 0xFFD23F, 0xB58CFF },
    { "ROSE",    0x2B1218, 0x3F1A24, 0xFFB3C7, 0xFF5C7A },
};

// The theme a chat is drawn with (the highlighted one while you are choosing).
static int theme_of(const Chat *m, int thread) {
    if (m->view == CV_THEMES && thread == m->sel) return m->themeSel;
    int v = m->th[thread].theme;
    return v >= 0 && v < N_THEMES ? v : 0;
}

// Day number on the clock of the console (so a new day starts at midnight, not at 8 AM).
static long local_day(void) {
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    if (!lt) return (long)(now / 86400);
    long y = lt->tm_year + 1900, mo = lt->tm_mon + 1, d = lt->tm_mday;
    if (mo <= 2) y--;
    long era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    long doy = (153 * (mo > 2 ? mo - 3 : mo + 9) + 2) / 5 + d - 1;
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

static long g_today;                     // set once per frame in chat_draw

// Streak that is still alive: it dies when a whole day passes without a snap.
static int streak_now(const ChatThread *t, long today) {
    return t->streak > 0 && today - t->lastDay <= 1 ? t->streak : 0;
}

static void bump_streak(ChatThread *t, long today) {
    if (t->streak > 0 && t->lastDay == today) return;        // already counted today
    t->streak = (t->streak > 0 && t->lastDay == today - 1) ? t->streak + 1 : 1;
    t->lastDay = today;
}

// Can you send messages in this chat?
static bool can_chat(const Chat *m, int thread) {
    if (thread == CT_AI) return true;
    if (thread >= CT_FRIEND && thread < CT_CUSTOM) return true;
    return thread >= CT_CUSTOM && thread < CHAT_THREADS && m->custom[thread - CT_CUSTOM].used;
}

/* ---------------- the choices for your own friends ---------------- */

static const char *const STYLE_NAME[] = { "LONG", "SHORT", "BUNS", "CAP" };
static const char *const HAIR_NAME[] = { "BLACK", "DARK BROWN", "BROWN", "BLONDE", "RED", "PINK", "BLUE" };
static const uint32_t HAIR_COL[] = { 0x1C1618, 0x3A2418, 0x6B4428, 0xD8B060, 0xA8402A, 0xE87CA8, 0x4A7CD0 };
static const char *const SKIN_NAME[] = { "FAIR", "LIGHT", "TAN", "BROWN", "DEEP" };
static const uint32_t SKIN_COL[] = { 0xF6D3BC, 0xEBBB95, 0xD99A68, 0xA86C46, 0x70452E };
// shirt, background and favorite color share these
static const char *const COLOR_NAME[] = { "PINK", "BLUE", "GREEN", "ORANGE", "RED", "PURPLE", "YELLOW", "BLACK" };
static const uint32_t SHIRT_COL[] = { 0xD6409F, 0x2456A8, 0x2E8858, 0xE0661E, 0xC83038, 0x6A48C0, 0xE0B820, 0x202024 };
static const uint32_t BACK_COL[] = { 0xF7A8C8, 0x8CC8F0, 0xA8DC8C, 0xF8C878, 0xF09090, 0xC0A8F0, 0xF8E880, 0x606068 };
static const char *const YESNO_NAME[] = { "NO", "YES" };

#define N_LIKES 5
static const char *const LIKES_NAME[N_LIKES] = { "ART", "GAMES", "FOOD AND ANIMALS", "SCIENCE AND SPORTS", "MUSIC" };
static const char *const LIKES_HOBBY[N_LIKES] = {
    "I LOVE DRAWING AND TAKING PHOTOS.", "I LOVE VIDEO GAMES AND PUZZLES.",
    "I LOVE COOKING AND CUTE ANIMALS.", "I LOVE SPACE AND SPORTS.", "I LOVE MUSIC AND SINGING."
};
static const char *const LIKES_TIP[N_LIKES] = {
    "ART TIP: SKETCH LIGHTLY FIRST, THEN DARKEN THE LINES YOU LIKE.",
    "GAME TIP: IF A LEVEL IS HARD, TAKE A SHORT BREAK AND TRY A NEW PLAN.",
    "COOKING TIP: READ THE WHOLE RECIPE BEFORE YOU START.",
    "STUDY TIP: EXPLAIN IT IN YOUR OWN WORDS. THAT SHOWS WHAT YOU KNOW.",
    "MUSIC TIP: PRACTICE SLOWLY FIRST, THEN SPEED UP."
};

static const char *const FD_LABEL[FD_COUNT] = {
    "HAIR STYLE", "HAIR COLOR", "SKIN", "SHIRT", "BACKGROUND", "GLASSES", "LIKES", "FAV COLOR"
};
static const int FD_CHOICES[FD_COUNT] = { 4, 7, 5, 8, 8, 2, N_LIKES, 8 };

static const char *fd_value(const FriendDef *d, int k) {
    int v = d->v[k];
    if (k == FD_STYLE) return STYLE_NAME[v];
    if (k == FD_HAIR) return HAIR_NAME[v];
    if (k == FD_SKIN) return SKIN_NAME[v];
    if (k == FD_GLASSES) return YESNO_NAME[v];
    if (k == FD_LIKES) return LIKES_NAME[v];
    return COLOR_NAME[v];
}

static bool fd_valid(const int *v) {
    for (int k = 0; k < FD_COUNT; k++)
        if (v[k] < 0 || v[k] >= FD_CHOICES[k]) return false;
    return true;
}

// Printable upper-case letters only, no spaces at the ends, at most max letters.
static void clean_word(const char *in, char *out, int max) {
    while (*in == ' ') in++;
    int n = 0;
    for (; *in && n < max; in++) {
        unsigned char ch = (unsigned char)*in;
        if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 32);
        out[n++] = (ch >= 32 && ch < 127) ? (char)ch : ' ';
    }
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
}

static const char *const FRIEND_LIKES[CHAT_FRIENDS] = {
    "ART AND PHOTOS", "GAMES AND PUZZLES", "COOKING AND ANIMALS", "SCIENCE AND SPORTS"
};
#define FR_Y 40                          // first friend row (bottom screen)
#define FR_H 43

/* ---------------- small drawing helpers ---------------- */

static void disc(uint16_t *c, float x, float y, float r, uint32_t rgb) {
    draw_ellipse(c, x, y, r, r, 0, rgb, 255);
}

static void rrect(uint16_t *c, int x, int y, int w, int h, int r, uint32_t rgb) {
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    draw_rect(c, x + r, y, w - 2 * r, h, rgb);
    draw_rect(c, x, y + r, w, h - 2 * r, rgb);
    disc(c, x + r - 0.5f, y + r - 0.5f, (float)r, rgb);
    disc(c, x + w - r - 0.5f, y + r - 0.5f, (float)r, rgb);
    disc(c, x + r - 0.5f, y + h - r - 0.5f, (float)r, rgb);
    disc(c, x + w - r - 0.5f, y + h - r - 0.5f, (float)r, rgb);
}

// A rounded outline: fill, then hollow it out with the background colour.
static void rring(uint16_t *c, int x, int y, int w, int h, int r, int t, uint32_t rgb, uint32_t bg) {
    rrect(c, x, y, w, h, r, rgb);
    rrect(c, x + t, y + t, w - 2 * t, h - 2 * t, r > t ? r - t : 1, bg);
}

static bool inside(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

// The 5x7 font in art.c has no ? , ' ( ) so those are drawn here.
static const struct { char ch; uint8_t g[7]; } EXTRA[] = {
    { '=',  { 0, 0, 31, 0, 31, 0, 0 } },
    { '*',  { 0, 4, 21, 14, 21, 4, 0 } },
    { '%',  { 24, 25, 2, 4, 8, 19, 3 } },
    { '?',  { 14, 17, 1, 2, 4, 0, 4 } },
    { ',',  { 0, 0, 0, 0, 12, 4, 8 } },
    { '\'', { 4, 4, 8, 0, 0, 0, 0 } },
    { '"',  { 10, 10, 0, 0, 0, 0, 0 } },
    { '(',  { 2, 4, 8, 8, 8, 4, 2 } },
    { ')',  { 8, 4, 2, 2, 2, 4, 8 } },
};

static void txt(uint16_t *c, int x, int y, int scale, const char *s, uint32_t rgb) {
    char one[2] = { 0, 0 };
    for (; *s; s++, x += 6 * scale) {
        const uint8_t *g = NULL;
        for (unsigned i = 0; i < sizeof(EXTRA) / sizeof(EXTRA[0]); i++)
            if (EXTRA[i].ch == *s) g = EXTRA[i].g;
        if (g) {
            for (int row = 0; row < 7; row++)
                for (int col = 0; col < 5; col++)
                    if (g[row] & (16 >> col))
                        draw_rect(c, x + col * scale, y + row * scale, scale, scale, rgb);
        } else {
            one[0] = *s;
            draw_text(c, x, y, scale, one, rgb);
        }
    }
}

static void bold(uint16_t *c, int x, int y, int scale, const char *s, uint32_t rgb) {   // fake bold
    txt(c, x, y, scale, s, rgb);
    txt(c, x + 1, y, scale, s, rgb);
}

static void pill_text(uint16_t *c, int x, int y, int w, int h, const char *s, uint32_t bg, uint32_t fg) {
    rrect(c, x, y, w, h, h / 2, bg);
    txt(c, x + (w - text_width(s, 1)) / 2, y + (h - 7) / 2, 1, s, fg);
}

/* ---------------- avatars and icons ---------------- */

static void avatar_ai(uint16_t *c, float x, float y, float r) {           // green face, pink hair
    disc(c, x, y, r, 0x24262B);
    draw_ellipse(c, x, y - 0.12f * r, 0.74f * r, 0.80f * r, 0, 0xF29BC4, 255);
    draw_ellipse(c, x, y + 0.14f * r, 0.50f * r, 0.58f * r, 0, 0x8ED63E, 255);
    disc(c, x - 0.2f * r, y + 0.05f * r, 0.13f * r, WHITE);
    disc(c, x + 0.2f * r, y + 0.05f * r, 0.13f * r, WHITE);
    disc(c, x - 0.2f * r, y + 0.07f * r, 0.07f * r, BLACK);
    disc(c, x + 0.2f * r, y + 0.07f * r, 0.07f * r, BLACK);
    draw_line(c, x - 0.16f * r, y + 0.36f * r, x + 0.16f * r, y + 0.36f * r, 0.12f * r < 1 ? 1 : 0.12f * r, 0x2F5A12);
}

static void ghost_shape(uint16_t *c, float x, float y, float r, uint32_t rgb) {
    draw_ellipse(c, x, y - 0.1f * r, r, r, 0, rgb, 255);
    draw_rect(c, (int)(x - r + 0.5f), (int)(y - 0.1f * r), (int)(2 * r), (int)(0.62f * r), rgb);
    disc(c, x - 0.62f * r, y + 0.52f * r, 0.38f * r, rgb);
    disc(c, x, y + 0.52f * r, 0.38f * r, rgb);
    disc(c, x + 0.62f * r, y + 0.52f * r, 0.38f * r, rgb);
}

static void avatar_ghost(uint16_t *c, float x, float y, float r) {      // Team Snapchat
    disc(c, x, y, r, YELLOW);
    ghost_shape(c, x, y - 0.06f * r, 0.56f * r, BLACK);
    ghost_shape(c, x, y - 0.06f * r, 0.46f * r, WHITE);
    disc(c, x - 0.16f * r, y - 0.16f * r, 0.07f * r < 0.8f ? 0.8f : 0.07f * r, BLACK);
    disc(c, x + 0.16f * r, y - 0.16f * r, 0.07f * r < 0.8f ? 0.8f : 0.07f * r, BLACK);
}

static void avatar_me(uint16_t *c, float x, float y, float r) {           // you
    disc(c, x, y, r, 0x6E8FB5);
    draw_ellipse(c, x, y - 0.1f * r, 0.66f * r, 0.74f * r, 0, 0x4B2E22, 255);
    draw_ellipse(c, x, y + 0.14f * r, 0.46f * r, 0.54f * r, 0, 0xE6B592, 255);
    disc(c, x - 0.18f * r, y + 0.08f * r, 0.07f * r, BLACK);
    disc(c, x + 0.18f * r, y + 0.08f * r, 0.07f * r, BLACK);
}

// The friends' cartoon faces.
typedef struct { uint32_t back, hair, skin, top; int style; bool glasses; } Look;

static void avatar_look(uint16_t *c, const Look *l, float x, float y, float r) {
    const uint32_t hair = l->hair;
    int style = l->style;
    disc(c, x, y, r, l->back);
    draw_ellipse(c, x, y + 0.72f * r, 0.58f * r, 0.26f * r, 0, l->top, 255);
    if (style == 0) draw_ellipse(c, x, y + 0.04f * r, 0.66f * r, 0.78f * r, 0, hair, 255);
    else draw_ellipse(c, x, y - 0.14f * r, 0.58f * r, 0.56f * r, 0, hair, 255);
    if (style == 2) {
        disc(c, x - 0.5f * r, y - 0.54f * r, 0.24f * r, hair);
        disc(c, x + 0.5f * r, y - 0.54f * r, 0.24f * r, hair);
    }
    draw_ellipse(c, x, y + 0.1f * r, 0.44f * r, 0.50f * r, 0, l->skin, 255);
    draw_ellipse(c, x, y - 0.3f * r, 0.42f * r, 0.17f * r, 0, hair, 255);     // fringe
    if (style == 3) {
        draw_ellipse(c, x, y - 0.46f * r, 0.52f * r, 0.26f * r, 0, 0xC83038, 255);
        draw_ellipse(c, x + 0.32f * r, y - 0.32f * r, 0.46f * r, 0.09f * r, 0, 0xC83038, 255);
    }
    float eye = 0.07f * r < 0.8f ? 0.8f : 0.07f * r;
    if (l->glasses) {
        disc(c, x - 0.19f * r, y + 0.06f * r, 0.17f * r, 0x222226);
        disc(c, x + 0.19f * r, y + 0.06f * r, 0.17f * r, 0x222226);
        disc(c, x - 0.19f * r, y + 0.06f * r, 0.12f * r, 0xE8F4FF);
        disc(c, x + 0.19f * r, y + 0.06f * r, 0.12f * r, 0xE8F4FF);
    }
    disc(c, x - 0.19f * r, y + 0.07f * r, eye, BLACK);
    disc(c, x + 0.19f * r, y + 0.07f * r, eye, BLACK);
    draw_line(c, x - 0.13f * r, y + 0.34f * r, x, y + 0.4f * r, 1.6f, 0x8A2E30);
    draw_line(c, x, y + 0.4f * r, x + 0.13f * r, y + 0.34f * r, 1.6f, 0x8A2E30);
}

static Look look_of(const FriendDef *d) {
    Look l = { BACK_COL[d->v[FD_BACK]], HAIR_COL[d->v[FD_HAIR]], SKIN_COL[d->v[FD_SKIN]],
               SHIRT_COL[d->v[FD_SHIRT]], d->v[FD_STYLE], d->v[FD_GLASSES] != 0 };
    return l;
}

static void avatar(uint16_t *c, const Chat *m, int who, float x, float y, float r) {
    static const Look BUILT_IN[CHAT_FRIENDS] = {
        { 0xF7A8C8, 0x4B2E22, 0xF1C7A5, 0xD6409F, 0, false },   // Mia: long hair
        { 0x8CC8F0, 0x1C1618, 0xD99A68, 0x2456A8, 1, true },    // Jay: short hair, glasses
        { 0xA8DC8C, 0x2A1A16, 0xE6B592, 0x2E8858, 2, false },   // Lia: two buns
        { 0xF8C878, 0x6B4428, 0xB87C54, 0xE0661E, 3, false },   // Kai: cap
    };
    if (who == CT_AI) avatar_ai(c, x, y, r);
    else if (who == CT_TEAM) avatar_ghost(c, x, y, r);
    else if (who < CT_CUSTOM) avatar_look(c, &BUILT_IN[who - CT_FRIEND], x, y, r);
    else {
        Look l = look_of(&m->custom[who - CT_CUSTOM]);
        avatar_look(c, &l, x, y, r);
    }
}

static void magnifier(uint16_t *c, float x, float y, uint32_t rgb, uint32_t bg) {
    disc(c, x - 1.5f, y - 1.5f, 6, rgb);
    disc(c, x - 1.5f, y - 1.5f, 4, bg);
    draw_line(c, x + 2.5f, y + 2.5f, x + 7, y + 7, 2.4f, rgb);
}

static void bell(uint16_t *c, float x, float y) {
    draw_ellipse(c, x, y - 1.5f, 5.5f, 6.0f, 0, WHITE, 255);
    draw_ellipse(c, x, y - 1.5f, 3.6f, 4.3f, 0, CIRCLE, 255);
    draw_rect(c, (int)x - 6, (int)y + 2, 13, 3, WHITE);
    disc(c, x, y + 7.5f, 1.8f, WHITE);
}

static void add_friend(uint16_t *c, float x, float y) {
    disc(c, x - 3.5f, y - 4.5f, 3.8f, BLACK);
    draw_ellipse(c, x - 3.5f, y + 6, 7.0f, 4.5f, 0, BLACK, 255);
    draw_line(c, x + 4.5f, y + 1, x + 11.5f, y + 1, 2.2f, BLACK);
    draw_line(c, x + 8, y - 2.5f, x + 8, y + 4.5f, 2.2f, BLACK);
}

static void dots3(uint16_t *c, float x, float y) {
    disc(c, x - 6, y, 1.9f, WHITE);
    disc(c, x, y, 1.9f, WHITE);
    disc(c, x + 6, y, 1.9f, WHITE);
}

static void robot(uint16_t *c, int x, int y) {
    rrect(c, x - 7, y - 4, 14, 11, 3, 0x9DBBD1);
    draw_rect(c, x - 9, y - 1, 2, 5, 0x9DBBD1);
    draw_rect(c, x + 7, y - 1, 2, 5, 0x9DBBD1);
    draw_rect(c, x, y - 8, 1, 4, 0x9DBBD1);
    disc(c, x + 0.5f, y - 8.5f, 1.7f, 0x9DBBD1);
    disc(c, x - 2.5f, y + 1.5f, 1.4f, 0x1E2A35);
    disc(c, x + 3.5f, y + 1.5f, 1.4f, 0x1E2A35);
    draw_rect(c, x - 2, y + 4, 5, 1, 0x1E2A35);
}

static void scan_icon(uint16_t *c, int x, int y, uint32_t bg) {          // corner brackets and a ring
    const uint32_t k = 0xE6E6E6;
    draw_rect(c, x - 9, y - 9, 7, 2, k); draw_rect(c, x - 9, y - 9, 2, 7, k);
    draw_rect(c, x + 3, y - 9, 7, 2, k); draw_rect(c, x + 8, y - 9, 2, 7, k);
    draw_rect(c, x - 9, y + 8, 7, 2, k); draw_rect(c, x - 9, y + 3, 2, 7, k);
    draw_rect(c, x + 3, y + 8, 7, 2, k); draw_rect(c, x + 8, y + 3, 2, 7, k);
    disc(c, x + 0.5f, y + 0.5f, 4.5f, k);
    disc(c, x + 0.5f, y + 0.5f, 2.6f, bg);
}

static void camera_icon(uint16_t *c, int x, int y, uint32_t bg) {
    const uint32_t k = 0xE6E6E6;
    draw_rect(c, x - 4, y - 10, 9, 4, k);
    rring(c, x - 11, y - 7, 23, 17, 4, 2, k, bg);
    disc(c, x + 0.5f, y + 1.5f, 4.5f, k);
    disc(c, x + 0.5f, y + 1.5f, 2.6f, bg);
}

static void received_icon(uint16_t *c, int x, int y, uint32_t bg) {      // blue speech bubble
    rring(c, x, y, 12, 10, 3, 2, BLUE, bg);
}

static void sent_icon(uint16_t *c, int x, int y) {                       // red arrow
    draw_line(c, x + 1, y, x + 11, y + 4.5f, 2.0f, RED);
    draw_line(c, x + 11, y + 4.5f, x + 1, y + 9, 2.0f, RED);
    draw_line(c, x + 1, y, x + 1, y + 9, 2.0f, RED);
}

static void compose_icon(uint16_t *c, float x, float y) {
    rring(c, (int)x - 9, (int)y - 7, 17, 14, 4, 2, BLACK, YELLOW);
    draw_line(c, x - 2, y + 2, x + 8, y - 8, 3.6f, BLACK);
    draw_line(c, x - 7, y + 7, x - 6, y + 4, 2.0f, BLACK);
}

static void nav_bar(uint16_t *c, int on) {      // on: 1 = chat, 3 = friends
    draw_rect(c, 0, NAV_Y, CW, CH - NAV_Y, NAVBG);
    draw_rect(c, 0, NAV_Y, CW, 1, LINEC);
    const uint32_t k = 0xE6E6E6;
    // map pin
    disc(c, 32, 224, 6, k); disc(c, 32, 224, 2.6f, NAVBG);
    draw_line(c, 27.5f, 227, 32, 236, 2.2f, k);
    draw_line(c, 36.5f, 227, 32, 236, 2.2f, k);
    draw_rect(c, on * CW / 5 + 18, 238, 28, 2, YELLOW);        // the page we are on
    // chat
    rrect(c, 85, 219, 22, 16, 4, k);
    draw_line(c, 91, 233, 88, 238, 2.4f, k);
    // camera
    rring(c, 148, 220, 24, 17, 5, 2, k, NAVBG);
    disc(c, 160, 228.5f, 5, k); disc(c, 160, 228.5f, 3, NAVBG);
    // friends
    disc(c, 219, 222, 4.4f, k); disc(c, 219, 222, 2.5f, NAVBG);
    disc(c, 231, 222, 4.4f, k); disc(c, 231, 222, 2.5f, NAVBG);
    draw_line(c, 211, 234, 213, 229, 2.0f, k); draw_line(c, 213, 229, 219, 227, 2.0f, k);
    draw_line(c, 219, 227, 225, 229, 2.0f, k); draw_line(c, 225, 229, 227, 234, 2.0f, k);
    draw_line(c, 233, 227, 237, 230, 2.0f, k); draw_line(c, 237, 230, 239, 234, 2.0f, k);
    // play (stories) with the red dot
    draw_line(c, 282, 219, 299, 227, 2.2f, k);
    draw_line(c, 299, 227, 282, 236, 2.2f, k);
    draw_line(c, 282, 236, 282, 219, 2.2f, k);
    disc(c, 301, 219, 3.6f, RED);
}

/* ---------------- streak badge ---------------- */

static void flame(uint16_t *c, float x, float y) {
    draw_ellipse(c, x, y + 2.5f, 4.6f, 5.2f, 0, 0xFF7A1A, 255);
    draw_ellipse(c, x, y - 1.5f, 3.0f, 6.0f, 0, 0xFF7A1A, 255);
    draw_ellipse(c, x, y + 3.2f, 2.6f, 3.4f, 0, 0xFFD23F, 255);
}

static void hourglass(uint16_t *c, int x, int y, uint32_t rgb) {
    draw_rect(c, x - 4, y - 6, 9, 2, rgb);
    draw_rect(c, x - 4, y + 5, 9, 2, rgb);
    draw_line(c, x - 3, y - 4, x + 3, y + 4, 1.6f, rgb);
    draw_line(c, x + 3, y - 4, x - 3, y + 4, 1.6f, rgb);
}

// Flame and number; the hourglass means: send a snap today or the streak ends.
static void streak_badge(uint16_t *c, int x, int y, int days, bool atRisk) {
    char s[12];
    snprintf(s, sizeof(s), "%d", days);
    flame(c, (float)x, (float)y);
    bold(c, x + 9, y - 3, 1, s, WHITE);
    if (atRisk) hourglass(c, x + 9 + text_width(s, 1) + 10, y, YELLOW);
}

/* ---------------- text: wrapping, messages, replies ---------------- */

#define LINE_MAX 64
#define LINES_MAX 6

// Splits text into lines of at most maxc letters, breaking at spaces. Returns the line count.
static int wrap(const char *s, int maxc, char out[][LINE_MAX]) {
    int n = 0, len = 0;
    char word[CHAT_TEXT];
    out[0][0] = 0;
    while (*s && n < LINES_MAX) {
        while (*s == ' ') s++;
        if (!*s) break;
        int w = 0;
        while (s[w] && s[w] != ' ' && w < CHAT_TEXT - 1) { word[w] = s[w]; w++; }
        word[w] = 0;
        s += w;
        for (int off = 0; off < w && n < LINES_MAX; ) {         // a word longer than a line is cut
            int part = w - off > maxc ? maxc : w - off;
            if (len > 0 && len + 1 + part > maxc) { n++; len = 0; if (n >= LINES_MAX) break; out[n][0] = 0; }
            if (len > 0) out[n][len++] = ' ';
            memcpy(out[n] + len, word + off, (size_t)part);
            len += part;
            out[n][len] = 0;
            off += part;
            if (off < w) { n++; len = 0; if (n >= LINES_MAX) break; out[n][0] = 0; }
        }
    }
    if (n < LINES_MAX && (len > 0 || n == 0)) n++;
    return n > LINES_MAX ? LINES_MAX : n;
}

static void add_msg(ChatThread *t, bool mine, const char *text) {
    if (t->count == CHAT_MAX_MSG) {
        memmove(&t->msg[0], &t->msg[1], sizeof(ChatMsg) * (CHAT_MAX_MSG - 1));
        t->count--;
    }
    ChatMsg *m = &t->msg[t->count++];
    m->mine = mine;
    snprintf(m->text, sizeof(m->text), "%s", text);
}

/* ---------------- saving and loading ---------------- */

// chats.txt, one item per line:  N name | C color | F food | T thread (you wrote
// there) | M thread mine text.  It is written to chats.tmp first, so switching off
// during a save does not lose the old chats.
#define SAVE_HEAD "CHATS1"

static void save_msg(FILE *f, int thread, bool mine, const char *text) {
    fprintf(f, "M %d %d %s\n", thread, mine ? 1 : 0, text);
}

static bool chat_save(const Chat *m) {
    if (!m->dir[0]) return true;
    char path[128], tmp[128];
    snprintf(path, sizeof(path), "%s/chats.txt", m->dir);
    snprintf(tmp, sizeof(tmp), "%s/chats.tmp", m->dir);
    mkdir(m->dir, 0777);                 // fine if it is already there
    FILE *f = fopen(tmp, "w");
    if (!f) return false;
    fprintf(f, SAVE_HEAD "\n");
    if (m->memory.name[0]) fprintf(f, "N %s\n", m->memory.name);
    if (m->memory.color[0]) fprintf(f, "C %s\n", m->memory.color);
    if (m->memory.food[0]) fprintf(f, "F %s\n", m->memory.food);
    for (int i = 0; i < CHAT_CUSTOM; i++) {   // your own friends: P = choices, Q = name, R = food
        const FriendDef *d = &m->custom[i];
        if (!d->used) continue;
        fprintf(f, "P %d", i);
        for (int k = 0; k < FD_COUNT; k++) fprintf(f, " %d", d->v[k]);
        fprintf(f, "\nQ %d %s\n", i, d->name);
        if (d->food[0]) fprintf(f, "R %d %s\n", i, d->food);
    }
    for (int t = 0; t < CHAT_THREADS; t++) {
        const ChatThread *ct = &m->th[t];     // K = nickname, H = theme, G = streak, S = snap sent
        if (ct->nick[0]) fprintf(f, "K %d %s\n", t, ct->nick);
        if (ct->theme) fprintf(f, "H %d %d\n", t, ct->theme);
        if (ct->streak > 0) fprintf(f, "G %d %d %ld\n", t, ct->streak, ct->lastDay);
        for (int i = 0; i < ct->snapCount; i++) fprintf(f, "S %d %s\n", t, ct->snaps[i]);
        if (m->th[t].touched) fprintf(f, "T %d\n", t);
        for (int i = 0; i < m->th[t].count; i++)
            save_msg(f, t, m->th[t].msg[i].mine, m->th[t].msg[i].text);
    }
    // an answer still being "typed" is saved too, so closing now does not lose it
    if (m->pending) save_msg(f, m->replyTo, false, m->reply);
    bool ok = !ferror(f);
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(tmp); return false; }
    remove(path);                        // the 3DS cannot rename over a file
    return rename(tmp, path) == 0;
}

static void add_msg(ChatThread *t, bool mine, const char *text);

// A photo file name from this app: letters, digits, _ . - only, ending in .bmp.
static bool safe_name(const char *s) {
    size_t n = strlen(s);
    if (n < 5 || n >= 40 || strstr(s, "..")) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!isalnum(c) && c != '_' && c != '.' && c != '-') return false;
    }
    return strcasecmp(s + n - 4, ".bmp") == 0;
}

void chat_load(Chat *m, const char *dir) {
    snprintf(m->dir, sizeof(m->dir), "%s", dir);
    char path[128], line[CHAT_TEXT + 32];
    snprintf(path, sizeof(path), "%s/chats.txt", m->dir);
    FILE *f = fopen(path, "r");
    if (!f) {                            // switched off in the middle of a save
        snprintf(path, sizeof(path), "%s/chats.tmp", m->dir);
        f = fopen(path, "r");
    }
    if (!f) return;
    if (!fgets(line, sizeof(line), f) || strncmp(line, SAVE_HEAD, strlen(SAVE_HEAD)) != 0) {
        fclose(f);
        return;
    }
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (strlen(line) < 3 || line[1] != ' ') continue;
        const char *rest = line + 2;
        int t, mine, at = 0;
        if (line[0] == 'N') snprintf(m->memory.name, sizeof(m->memory.name), "%.19s", rest);
        else if (line[0] == 'C') snprintf(m->memory.color, sizeof(m->memory.color), "%.19s", rest);
        else if (line[0] == 'F') snprintf(m->memory.food, sizeof(m->memory.food), "%.23s", rest);
        else if (line[0] == 'P') {
            int v[FD_COUNT];
            if (sscanf(rest, "%d %d %d %d %d %d %d %d %d", &t, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) == 1 + FD_COUNT &&
                t >= 0 && t < CHAT_CUSTOM && fd_valid(v)) {
                memcpy(m->custom[t].v, v, sizeof(v));
                m->custom[t].used = true;
                snprintf(m->custom[t].name, sizeof(m->custom[t].name), "FRIEND %d", t + 1);
            }
        } else if (line[0] == 'Q' || line[0] == 'R') {
            if (sscanf(rest, "%d %n", &t, &at) == 1 && at > 0 && rest[at] &&
                t >= 0 && t < CHAT_CUSTOM && m->custom[t].used) {
                if (line[0] == 'Q') clean_word(rest + at, m->custom[t].name, FRIEND_NAME);
                else clean_word(rest + at, m->custom[t].food, FRIEND_FOOD);
            }
        } else if (line[0] == 'K') {
            at = 0;
            if (sscanf(rest, "%d %n", &t, &at) == 1 && at > 0 && rest[at] && t >= 0 && t < CHAT_THREADS)
                clean_word(rest + at, m->th[t].nick, NICK_MAX);
        } else if (line[0] == 'H') {
            int v;
            if (sscanf(rest, "%d %d", &t, &v) == 2 && t >= 0 && t < CHAT_THREADS && v >= 0 && v < N_THEMES)
                m->th[t].theme = v;
        } else if (line[0] == 'G') {
            int s;
            long d;
            if (sscanf(rest, "%d %d %ld", &t, &s, &d) == 3 && t >= 0 && t < CHAT_THREADS && s > 0 && s < 100000) {
                m->th[t].streak = s;
                m->th[t].lastDay = d;
            }
        } else if (line[0] == 'S') {
            at = 0;
            if (sscanf(rest, "%d %n", &t, &at) == 1 && at > 0 && rest[at] && t >= 0 && t < CHAT_THREADS &&
                m->th[t].snapCount < SNAP_KEEP && safe_name(rest + at))
                snprintf(m->th[t].snaps[m->th[t].snapCount++], sizeof(m->th[t].snaps[0]), "%s", rest + at);
        } else if (line[0] == 'T') {
            if (sscanf(rest, "%d", &t) == 1 && t >= 0 && t < CHAT_THREADS) m->th[t].touched = true;
        } else if (line[0] == 'M') {
            if (sscanf(rest, "%d %d %n", &t, &mine, &at) == 2 && at > 0 && rest[at] &&
                t >= 0 && t < CHAT_THREADS) {
                add_msg(&m->th[t], mine != 0, rest + at);
                m->started = true;       // no new starting messages
            }
        }
    }
    fclose(f);
}

/* ---------------- open, update, send ---------------- */

static void say(Chat *m, const char *text) {
    snprintf(m->note, sizeof(m->note), "%s", text);
    m->noteUntil = (unsigned long long)-1;       // chat_draw starts the clock
}

void chat_open(Chat *m) {
    if (!m->started) {
        m->started = true;
        add_msg(&m->th[CT_AI], false, "HEY! I AM MY AI. ASK ME A QUESTION, OR TRY ONE OF THE QUICK CHATS.");
        add_msg(&m->th[CT_TEAM], false, "WELCOME TO SNAP3DS!");
        add_msg(&m->th[CT_TEAM], false, "TAP THE BIG CIRCLE TO TAKE A SNAP. SWIPE THE CIRCLES TO TRY FILTERS.");
        for (int f = 0; f < CHAT_FRIENDS; f++) {
            char hi[CHAT_TEXT];
            snprintf(hi, sizeof(hi), "%s %s ASK ME STUFF!", PERSONAS[BP_MIA + f].hello, PERSONAS[BP_MIA + f].hobby);
            add_msg(&m->th[CT_FRIEND + f], false, hi);
        }
    }
    m->view = CV_LIST;
    if (m->sel >= CHAT_LISTED) m->sel = CT_AI;
    m->scroll = 0;
    m->note[0] = 0;
}

void chat_open_friends(Chat *m) {
    chat_open(m);
    m->view = CV_FRIENDS;
    m->sel = CT_FRIEND + m->friendPage * CHAT_FRIENDS + m->friendSel;
}

static void deliver(Chat *m) {
    if (!m->pending) return;
    add_msg(&m->th[m->replyTo], false, m->reply);
    m->pending = false;
    if (m->sel == m->replyTo) m->scroll = 0;
}

// A friend you made answers with the same rules, using your choices as their personality.
static void reply_custom(Chat *m, const FriendDef *d, const char *said) {
    char about[64], hello[32];
    snprintf(about, sizeof(about), "I AM %s, THE AI FRIEND YOU MADE.", d->name);
    snprintf(hello, sizeof(hello), "HEY, IT IS %s!", d->name);
    Persona who = { d->name, about, LIKES_HOBBY[d->v[FD_LIKES]], COLOR_NAME[d->v[FD_COLOR]],
                    d->food[0] ? d->food : "SURPRISE SNACKS", LIKES_TIP[d->v[FD_LIKES]], hello };
    brain_reply_as(&m->memory, &who, said, time(NULL), m->reply, sizeof(m->reply));
}

/* ---------------- the friend maker ---------------- */

// Rows of the maker: the name, the FD_ choices, then the favorite food.
#define ED_ROWS (FD_COUNT + 2)
#define ED_NAME 0
#define ED_FOOD (ED_ROWS - 1)
#define ED_Y 24
#define ED_H 17
#define ED_BTN_Y 207

static void open_editor(Chat *m, int slot) {
    m->editSlot = slot;
    m->editRow = 0;
    m->confirmDelete = false;
    m->note[0] = 0;
    if (m->custom[slot].used) m->draft = m->custom[slot];
    else {
        static const int START[FD_COUNT] = { 0, 2, 1, 5, 5, 0, 0, 1 };
        memset(&m->draft, 0, sizeof(m->draft));
        memcpy(m->draft.v, START, sizeof(START));
    }
    m->view = CV_EDIT;
}

static void close_editor(Chat *m) {
    m->view = CV_FRIENDS;
    m->friendPage = 1;
    m->friendSel = m->editSlot;
    m->sel = CT_CUSTOM + m->editSlot;
    m->scroll = 0;
}

static void store(Chat *m) {
    m->saveFailed = !chat_save(m);
    if (m->saveFailed) say(m, "NOT SAVED - CHECK SD CARD");
}

static void save_friend(Chat *m) {
    if (!m->draft.name[0]) {
        say(m, "GIVE YOUR FRIEND A NAME FIRST");
        m->editRow = ED_NAME;
        return;
    }
    bool isNew = !m->custom[m->editSlot].used;
    m->draft.used = true;
    m->custom[m->editSlot] = m->draft;
    ChatThread *t = &m->th[CT_CUSTOM + m->editSlot];
    if (isNew) {
        memset(t, 0, sizeof(*t));
        char hi[CHAT_TEXT];
        snprintf(hi, sizeof(hi), "HEY, IT IS %s! %s ASK ME STUFF!", m->draft.name, LIKES_HOBBY[m->draft.v[FD_LIKES]]);
        add_msg(t, false, hi);
    }
    close_editor(m);
    say(m, "FRIEND SAVED");
    store(m);
}

static void delete_friend(Chat *m) {
    int thread = CT_CUSTOM + m->editSlot;
    if (m->pending && m->replyTo == thread) m->pending = false;
    memset(&m->custom[m->editSlot], 0, sizeof(m->custom[0]));
    memset(&m->th[thread], 0, sizeof(m->th[0]));
    close_editor(m);
    say(m, "FRIEND DELETED");
    store(m);
}

void chat_set_text(Chat *m, int what, const char *text) {
    if (what == CR_ASK_NICK && m->view == CV_OPTIONS) {
        clean_word(text, m->th[m->sel].nick, NICK_MAX);
        say(m, m->th[m->sel].nick[0] ? "NICKNAME SAVED" : "NICKNAME CLEARED");
        store(m);
        return;
    }
    if (m->view != CV_EDIT) return;
    m->note[0] = 0;
    if (what == CR_ASK_NAME) clean_word(text, m->draft.name, FRIEND_NAME);
    else if (what == CR_ASK_FOOD) clean_word(text, m->draft.food, FRIEND_FOOD);
}

static int update_editor(Chat *m, const ChatInput *in) {
    if (in->keys & CK_B) { close_editor(m); return CR_NONE; }
    if (in->keys & CK_X) { save_friend(m); return CR_NONE; }
    int change = 0;
    bool open = false;                   // open the keyboard on a name or food row
    if (in->keys & CK_UP) m->editRow = (m->editRow + ED_ROWS - 1) % ED_ROWS;
    if (in->keys & CK_DOWN) m->editRow = (m->editRow + 1) % ED_ROWS;
    if (in->keys & CK_LEFT) change = -1;
    if (in->keys & CK_RIGHT) change = 1;
    if (in->keys & CK_A) { change = 1; open = true; }
    if (in->down) {
        int x = in->x, y = in->y;
        if (y >= ED_BTN_Y) {                                     // CANCEL | DELETE | SAVE
            if (x < 104) close_editor(m);
            else if (x >= 216) save_friend(m);
            else if (m->custom[m->editSlot].used) {
                if (m->confirmDelete) delete_friend(m);
                else m->confirmDelete = true;
            }
            return CR_NONE;
        }
        if (y >= ED_Y && y < ED_Y + ED_ROWS * ED_H) {
            m->editRow = (y - ED_Y) / ED_H;
            change = x < CW / 2 ? -1 : 1;                        // left half: previous
            open = true;
        }
    }
    if (change || open) m->confirmDelete = false;
    if (m->editRow == ED_NAME) return open ? CR_ASK_NAME : CR_NONE;
    if (m->editRow == ED_FOOD) return open ? CR_ASK_FOOD : CR_NONE;
    if (change) {
        int k = m->editRow - 1;
        m->draft.v[k] = (m->draft.v[k] + change + FD_CHOICES[k]) % FD_CHOICES[k];
    }
    return CR_NONE;
}

void chat_send(Chat *m, const char *text, unsigned long long nowMs) {
    if (m->view != CV_THREAD || !can_chat(m, m->sel)) return;
    char clean[CHAT_TEXT];
    size_t n = 0;
    for (; text[n] && n < sizeof(clean) - 1; n++) {
        unsigned char ch = (unsigned char)text[n];
        clean[n] = (ch >= 32 && ch < 127) ? (char)ch : ' ';
    }
    clean[n] = 0;
    char *p = clean;
    while (*p == ' ') p++;
    for (size_t i = strlen(p); i > 0 && p[i - 1] == ' '; i--) p[i - 1] = 0;
    if (!*p) return;

    deliver(m);                                  // an unanswered one arrives first
    add_msg(&m->th[m->sel], true, p);
    m->th[m->sel].touched = true;
    if (m->sel >= CT_CUSTOM) reply_custom(m, &m->custom[m->sel - CT_CUSTOM], p);
    else brain_reply(&m->memory, m->sel == CT_AI ? BP_MYAI : BP_MIA + m->sel - CT_FRIEND, p,
                     time(NULL), m->reply, sizeof(m->reply));
    m->replyTo = m->sel;
    m->pending = true;
    m->replyAt = nowMs + 800 + (unsigned long long)(rand() % 900);
    m->scroll = 0;
    m->saveFailed = !chat_save(m);
    if (m->saveFailed) say(m, "CHAT NOT SAVED - CHECK SD CARD");
}

// Quick chats shown on the bottom screen while My AI is open.
static const char *const CHIPS[6] = {
    "HI!", "TELL ME A JOKE", "WHAT DO YOU LIKE?", "WHAT TIME IS IT?", "GIVE ME A TIP", "HELP"
};
static int chip_x(int i) { return 8 + (i % 2) * 156; }
static int chip_y(int i) { return 62 + (i / 2) * 36; }

/* ---------------- options, themes, photo history ---------------- */

enum { OP_NICK, OP_THEME, OP_SEND, OP_HISTORY, OP_RESET, OP_ROWS };
#define OP_Y 46                          // first option row (bottom screen)
#define OP_H 28

static int gx(int i) { return 3 + (i % 4) * 79; }              // photo grid: 4 x 2 thumbnails
static int gy(int i) { return 44 + (i / 4) * 62; }
static int tx(int i) { return 8 + (i % 2) * 156; }             // theme swatches: 2 x 3
static int ty(int i) { return 46 + (i / 2) * 50; }

static void open_options(Chat *m) {
    m->view = CV_OPTIONS;
    m->opSel = 0;
    m->note[0] = 0;
}

static int cmp_newest(const void *a, const void *b) { return strcmp((const char *)b, (const char *)a); }

// Opens the grid: CV_PICK = the newest photos on the card, CV_HISTORY = snaps sent in this chat.
// Returns false (and says why) when there is nothing to show.
static bool grid_start(Chat *m, int view) {
    m->gcount = m->gsel = 0;
    m->bigIdx = -1;
    m->bigOk = false;
    memset(m->gstate, 0, sizeof(m->gstate));
    if (view == CV_HISTORY) {
        const ChatThread *t = &m->th[m->sel];
        for (int i = t->snapCount - 1; i >= 0 && m->gcount < GRID_N; i--)
            memcpy(m->gname[m->gcount++], t->snaps[i], sizeof(m->gname[0]));
    } else {
        DIR *d = m->dir[0] ? opendir(m->dir) : NULL;
        struct dirent *e;
        while (d && (e = readdir(d)) != NULL) {  // keep the GRID_N newest (names start with the date)
            if (!safe_name(e->d_name)) continue;
            if (m->gcount < GRID_N) { snprintf(m->gname[m->gcount++], sizeof(m->gname[0]), "%.39s", e->d_name); continue; }
            int oldest = 0;
            for (int i = 1; i < m->gcount; i++)
                if (strcmp(m->gname[i], m->gname[oldest]) < 0) oldest = i;
            if (strcmp(e->d_name, m->gname[oldest]) > 0)
                snprintf(m->gname[oldest], sizeof(m->gname[0]), "%.39s", e->d_name);
        }
        if (d) closedir(d);
        qsort(m->gname, (size_t)m->gcount, sizeof(m->gname[0]), cmp_newest);
    }
    if (!m->gcount) {
        say(m, view == CV_HISTORY ? "NO SNAPS SENT HERE YET" : "NO PHOTOS YET - TAKE A SNAP FIRST");
        return false;
    }
    m->view = view;
    m->note[0] = 0;
    return true;
}

// Loads one thing per frame (the big photo first, then thumbnails), so opening is quick.
static void grid_load_step(Chat *m) {
    if (!m->gcount) return;
    char path[160];
    if (m->bigIdx != m->gsel) {
        snprintf(path, sizeof(path), "%.95s/%.39s", m->dir, m->gname[m->gsel]);
        m->bigOk = mem_load_bmp(path, m->big, CW, CH);
        m->bigIdx = m->gsel;
        return;
    }
    for (int i = 0; i < m->gcount; i++)
        if (m->gstate[i] == 0) {
            snprintf(path, sizeof(path), "%.95s/%.39s", m->dir, m->gname[i]);
            m->gstate[i] = mem_load_bmp(path, m->gthumb[i], GRID_TW, GRID_TH) ? 1 : 2;
            return;
        }
}

// Sends the highlighted photo to the open chat. Friends send one back on their own.
static void send_snap(Chat *m, unsigned long long nowMs) {
    if (!can_chat(m, m->sel) || m->gcount == 0) return;
    ChatThread *t = &m->th[m->sel];
    deliver(m);                                  // an unanswered one arrives first
    add_msg(t, true, "SENT A SNAP");
    t->touched = true;
    if (t->snapCount == SNAP_KEEP) {
        memmove(t->snaps[0], t->snaps[1], sizeof(t->snaps[0]) * (SNAP_KEEP - 1));
        t->snapCount--;
    }
    memcpy(t->snaps[t->snapCount++], m->gname[m->gsel], sizeof(t->snaps[0]));
    bool isFriend = m->sel >= CT_FRIEND;         // no streaks with My AI
    if (isFriend) bump_streak(t, local_day());

    static const char *const BACK[4] = {
        "LOVE THIS SNAP! SNAPPING YOU BACK.", "HA, NICE ONE! HERE IS MINE.",
        "WOW, GREAT PHOTO! SNAP BACK SENT.", "COOL! KEEP THE STREAK GOING."
    };
    snprintf(m->reply, sizeof(m->reply), "%s", isFriend ? BACK[rand() % 4] :
             "THANKS FOR THE SNAP! I CANT SEE PICTURES YET, BUT I LIKE GETTING THEM.");
    m->replyTo = m->sel;
    m->pending = true;
    m->replyAt = nowMs + 800 + (unsigned long long)(rand() % 900);
    m->scroll = 0;
    m->view = CV_THREAD;
    char note[44];
    if (isFriend) snprintf(note, sizeof(note), "SNAP SENT - STREAK %d", t->streak);
    else snprintf(note, sizeof(note), "SNAP SENT");
    say(m, note);
    store(m);                                    // says so itself if the card is full
}

static int update_options(Chat *m, const ChatInput *in) {
    if (in->keys & CK_B) { m->view = CV_THREAD; return CR_NONE; }
    if (in->keys & CK_UP) m->opSel = (m->opSel + OP_ROWS - 1) % OP_ROWS;
    if (in->keys & CK_DOWN) m->opSel = (m->opSel + 1) % OP_ROWS;
    bool act = (in->keys & CK_A) != 0;
    if (in->down) {
        if (in->y < 38) { if (in->x < 40) m->view = CV_THREAD; return CR_NONE; }
        if (in->y >= OP_Y && in->y < OP_Y + OP_ROWS * OP_H) { m->opSel = (in->y - OP_Y) / OP_H; act = true; }
    }
    if (!act) return CR_NONE;
    ChatThread *t = &m->th[m->sel];
    switch (m->opSel) {
    case OP_NICK:
        return CR_ASK_NICK;
    case OP_THEME:
        m->themeSel = theme_of(m, m->sel);
        m->view = CV_THEMES;
        break;
    case OP_SEND:
        if (!can_chat(m, m->sel)) say(m, "TEAM SNAPCHAT CANT GET SNAPS");
        else grid_start(m, CV_PICK);
        break;
    case OP_HISTORY:
        grid_start(m, CV_HISTORY);
        break;
    case OP_RESET:
        if (!t->nick[0]) { say(m, "NO NICKNAME SET"); break; }
        t->nick[0] = 0;
        say(m, "NICKNAME CLEARED");
        store(m);
        break;
    }
    return CR_NONE;
}

static int update_themes(Chat *m, const ChatInput *in) {
    if (in->keys & CK_B) { m->view = CV_OPTIONS; return CR_NONE; }
    int s = m->themeSel;
    if (in->keys & CK_LEFT) s--;
    if (in->keys & CK_RIGHT) s++;
    if (in->keys & CK_UP) s -= 2;
    if (in->keys & CK_DOWN) s += 2;
    if (s >= 0 && s < N_THEMES) m->themeSel = s;
    bool apply = (in->keys & CK_A) != 0;
    if (in->down) {
        if (in->y < 38) { if (in->x < 40) m->view = CV_OPTIONS; return CR_NONE; }
        for (int i = 0; i < N_THEMES; i++)
            if (inside(in->x, in->y, tx(i), ty(i), 148, 44)) { m->themeSel = i; apply = true; }
    }
    if (apply) {
        char note[44];
        m->th[m->sel].theme = m->themeSel;
        m->view = CV_OPTIONS;                    // theme_of now gives the saved theme
        snprintf(note, sizeof(note), "THEME: %s", THEMES[m->themeSel].name);
        say(m, note);
        store(m);
    }
    return CR_NONE;
}

static int update_grid(Chat *m, const ChatInput *in) {
    grid_load_step(m);
    if (in->keys & CK_B) { m->view = CV_OPTIONS; return CR_NONE; }
    int s = m->gsel;
    if (in->keys & CK_LEFT) s--;
    if (in->keys & CK_RIGHT) s++;
    if (in->keys & CK_UP) s -= 4;
    if (in->keys & CK_DOWN) s += 4;
    if (s >= 0 && s < m->gcount) m->gsel = s;
    bool send = m->view == CV_PICK && (in->keys & CK_A);
    if (in->down) {
        if (in->y < 38) { if (in->x < 40) m->view = CV_OPTIONS; return CR_NONE; }
        for (int i = 0; i < m->gcount; i++)
            if (inside(in->x, in->y, gx(i), gy(i), GRID_TW, GRID_TH)) m->gsel = i;
        if (m->view == CV_PICK && inside(in->x, in->y, 90, 176, 140, 24)) send = true;
    }
    if (send) send_snap(m, in->ms);
    return CR_NONE;
}

int chat_update(Chat *m, const ChatInput *in) {
    if (m->pending && in->ms >= m->replyAt) deliver(m);

    if (m->view == CV_EDIT) return update_editor(m, in);
    if (m->view == CV_OPTIONS) return update_options(m, in);
    if (m->view == CV_THEMES) return update_themes(m, in);
    if (m->view == CV_PICK || m->view == CV_HISTORY) return update_grid(m, in);

    if (m->view == CV_FRIENDS) {
        if (in->keys & CK_B) return CR_EXIT;
        if (in->keys & CK_UP) m->friendSel = (m->friendSel + CHAT_FRIENDS - 1) % CHAT_FRIENDS;
        if (in->keys & CK_DOWN) m->friendSel = (m->friendSel + 1) % CHAT_FRIENDS;
        bool flip = (in->keys & (CK_LEFT | CK_RIGHT)) || (in->down && in->y < 26 && in->x >= 240);
        if (flip) { m->friendPage = !m->friendPage; m->note[0] = 0; }
        int base = m->friendPage ? CT_CUSTOM : CT_FRIEND;
        m->sel = base + m->friendSel;
        bool open = (in->keys & CK_A) != 0, edit = (in->keys & CK_X) != 0;
        if (flip) return CR_NONE;
        if (in->down && in->y >= NAV_Y) {                        // bottom bar
            int slot = in->x * 5 / CW;
            if (slot == 0) return CR_MAP;
            if (slot == 1) chat_open(m);
            else if (slot == 2) return CR_EXIT;
            else if (slot == 4) return CR_STORIES;
            return CR_NONE;
        }
        if (in->down && in->y >= FR_Y && in->y < FR_Y + CHAT_FRIENDS * FR_H) {
            m->friendSel = (in->y - FR_Y) / FR_H;
            m->sel = base + m->friendSel;
            if (m->friendPage && in->x >= 206 && in->x < 254 && m->custom[m->friendSel].used) edit = true;
            else open = true;
        }
        if (m->friendPage) {
            bool used = m->custom[m->friendSel].used;
            if ((edit && used) || (open && !used)) { open_editor(m, m->friendSel); return CR_NONE; }
        }
        if (open) { m->view = CV_THREAD; m->scroll = 0; m->note[0] = 0; }
        return CR_NONE;
    }

    if (m->view == CV_LIST) {
        if (in->keys & CK_B) return CR_EXIT;
        if (in->keys & CK_UP) m->sel = CT_AI;
        if (in->keys & CK_DOWN) m->sel = CT_TEAM;
        if (in->keys & CK_A) { m->view = CV_THREAD; m->scroll = 0; return CR_NONE; }
        if (!in->down) return CR_NONE;

        int x = in->x, y = in->y;
        if (y >= NAV_Y) {                                        // bottom bar
            int slot = x * 5 / CW;
            if (slot == 2) return CR_EXIT;                       // camera
            if (slot == 0) return CR_MAP;                        // map
            else if (slot == 3) chat_open_friends(m);
            else if (slot == 4) return CR_STORIES;
            return CR_NONE;
        }
        if ((x - 286) * (x - 286) + (y - 184) * (y - 184) <= 20 * 20) {
            say(m, "NEW CHAT NEEDS ONLINE - NOT IN APP");
        } else if (y < 38) {                                     // top row
            if (x < 36) say(m, "OPEN YOUR PROFILE ON THE CAMERA SCREEN");
            else if (x < 66) say(m, "SEARCH IS NOT IN THIS APP");
            else if (x >= 196 && x < 228) say(m, "NO NEW NOTIFICATIONS");
            else if (x >= 230 && x < 264) say(m, "ADD FRIENDS NEEDS ONLINE - NOT IN APP");
            else if (x >= 266) say(m, "NO MORE OPTIONS YET");
        } else if (y >= 40 && y < 68) {                          // tabs
            if (x >= 96 && x < 198) say(m, "NEAR ME NEEDS ONLINE - NOT IN APP");
            else if (x >= 198) say(m, "CHAT GROUPS NEED ONLINE - NOT IN APP");
        } else if (y >= LIST_Y && y < LIST_Y + CHAT_LISTED * ROW_H) {
            m->sel = (y - LIST_Y) / ROW_H;                       // open that chat
            m->view = CV_THREAD;
            m->scroll = 0;
        }
        return CR_NONE;
    }

    /* one chat open */
    int back = m->sel >= CT_FRIEND ? CV_FRIENDS : CV_LIST;       // where this chat was opened from
    if (in->keys & CK_B) { m->view = back; return CR_NONE; }
    if (in->keys & CK_UP) m->scroll += 24;
    if (in->keys & CK_DOWN) m->scroll -= 24;
    if (m->scroll > m->maxScroll) m->scroll = m->maxScroll;
    if (m->scroll < 0) m->scroll = 0;
    if (in->keys & CK_X) { open_options(m); return CR_NONE; }
    if ((in->keys & CK_A) && can_chat(m, m->sel)) return CR_KEYBOARD;
    if (!in->down) return CR_NONE;

    int x = in->x, y = in->y;
    if (y < 38) {
        if (x < 40) m->view = back;                              // back
        else if (x >= 266) open_options(m);                      // the three dots
        return CR_NONE;
    }
    if (!can_chat(m, m->sel)) return CR_NONE;                    // Team Snapchat cannot get replies
    if (inside(x, y, 8, IN_Y, 304, IN_H)) return CR_KEYBOARD;
    for (int i = 0; i < 6; i++)
        if (inside(x, y, chip_x(i), chip_y(i), 148, 30)) { chat_send(m, CHIPS[i], in->ms); break; }
    return CR_NONE;
}

/* ---------------- drawing ---------------- */

static void draw_note(uint16_t *c, Chat *m, unsigned long long now, int y) {
    if (!m->note[0]) return;
    if (m->noteUntil == (unsigned long long)-1) m->noteUntil = now + 2000;   // start the 2 second clock
    if (now >= m->noteUntil) { m->note[0] = 0; return; }
    int w = text_width(m->note, 1) + 24;
    pill_text(c, (CW - w) / 2, y, w, 18, m->note, 0x38383D, WHITE);
}

#define MSG_X 16
#define MSG_CHARS 48

static int msg_height(int lines) { return 11 + lines * 10 + 9; }

// The open chat (or the highlighted one), Snapchat style: coloured name, a bar and the text.
static void draw_thread(uint16_t *c, Chat *m, unsigned long long now) {
    ChatThread *t = &m->th[m->sel];
    bool typing = m->pending && m->sel == m->replyTo;
    char lines[LINES_MAX][LINE_MAX];
    const Theme *th = &THEMES[theme_of(m, m->sel)];
    draw_rect(c, 0, 0, CW, CH, th->bg);

    int total = 0;
    for (int i = 0; i < t->count; i++) total += msg_height(wrap(t->msg[i].text, MSG_CHARS, lines));
    if (typing) total += msg_height(1);

    int top = HEAD_H + 4, bottom = CH - 6, view = bottom - top;
    m->maxScroll = total > view ? total - view : 0;
    if (m->scroll > m->maxScroll) m->scroll = m->maxScroll;
    if (m->scroll < 0) m->scroll = 0;

    int y = bottom + m->scroll - total;                          // newest message sits at the bottom
    for (int i = 0; i < t->count; i++) {
        int n = wrap(t->msg[i].text, MSG_CHARS, lines);
        int h = msg_height(n);
        if (y + h > top && y < CH) {
            uint32_t col = t->msg[i].mine ? th->mine : th->theirs;
            draw_rect(c, 8, y, 2, 11 + n * 10 - 3, col);
            bold(c, MSG_X, y, 1, t->msg[i].mine ? "ME" : name_of(m, m->sel), col);
            for (int k = 0; k < n; k++) txt(c, MSG_X, y + 11 + k * 10, 1, lines[k], WHITE);
        }
        y += h;
    }
    if (typing) {
        const char *dots[3] = { ".", ". .", ". . ." };
        draw_rect(c, 8, y, 2, 18, th->theirs);
        bold(c, MSG_X, y, 1, name_of(m, m->sel), th->theirs);
        txt(c, MSG_X, y + 11, 1, dots[(now / 300) % 3], GREY);
    }

    draw_rect(c, 0, 0, CW, HEAD_H, th->bar);                     // header covers scrolled text
    draw_rect(c, 0, HEAD_H, CW, 1, LINEC);
    avatar(c, m, m->sel, 22, 15, 11);
    bold(c, 42, 8, 2, name_of(m, m->sel), WHITE);
    if (m->scroll > 0) txt(c, CW - 8 - text_width("UP", 1), 12, 1, "UP", GREY);
    int sn = m->sel >= CT_FRIEND ? streak_now(t, g_today) : 0;
    if (sn) streak_badge(c, 236, 15, sn, t->lastDay < g_today);
}

static void draw_list(uint16_t *c, Chat *m, unsigned long long now) {
    draw_rect(c, 0, 0, CW, CH, BG);

    // top row: you, search, title, bell, add friend, more
    avatar_me(c, 20, 19, 14);
    disc(c, 30, 9, 4.2f, BG); disc(c, 30, 9, 3.2f, RED);         // red dot on you
    disc(c, 50, 19, 14, CIRCLE);
    magnifier(c, 50, 19, WHITE, CIRCLE);
    bold(c, 108, 12, 2, "CHAT", WHITE);
    disc(c, 212, 19, 14, CIRCLE);
    bell(c, 212, 19);
    disc(c, 246, 19, 14, YELLOW);
    add_friend(c, 246, 19);
    disc(c, 282, 19, 14, CIRCLE);
    dots3(c, 282, 19);

    // tabs
    avatar_ai(c, 15, 53, 8);
    bold(c, 30, 46, 2, "MY AI", WHITE);
    bold(c, 106, 46, 2, "NEAR ME", WHITE);
    txt(c, 206, 46, 2, "NEW", 0x6E6E74);
    txt(c, 207, 46, 2, "NEW", 0x6E6E74);
    disc(c, 262, 53, 8, 0x5A5A60);
    draw_line(c, 258, 53, 266, 53, 2.2f, WHITE);
    draw_line(c, 262, 49, 262, 57, 2.2f, WHITE);
    draw_rect(c, 0, LIST_Y - 2, CW, 1, LINEC);

    // chats
    for (int i = 0; i < CHAT_LISTED; i++) {
        int y0 = LIST_Y + i * ROW_H;
        uint32_t bg = i == m->sel ? SELBG : BG;
        draw_rect(c, 0, y0, CW, ROW_H, bg);
        avatar(c, m, i, 26, (float)y0 + 24, 17);
        bold(c, 52, y0 + 9, 2, name_of(m, i), WHITE);

        ChatThread *t = &m->th[i];
        bool sent = t->count > 0 && t->msg[t->count - 1].mine;
        if (sent) sent_icon(c, 52, y0 + 28);
        else received_icon(c, 52, y0 + 28, bg);
        char sub[32];
        snprintf(sub, sizeof(sub), "%s - %s", sent ? "SENT" : "RECEIVED", t->touched ? "NOW" : "2MO");
        txt(c, 69, y0 + 29, 1, sub, GREY);

        if (i == CT_AI) { robot(c, 252, y0 + 24); scan_icon(c, 292, y0 + 24, bg); }
        else camera_icon(c, 292, y0 + 24, bg);
        draw_rect(c, 0, y0 + ROW_H - 1, CW, 1, LINEC);
    }

    // new chat button
    disc(c, 286, 184, 20, YELLOW);
    compose_icon(c, 286, 184);

    nav_bar(c, 1);
    draw_note(c, m, now, 172);
}

static void draw_friends(uint16_t *c, Chat *m, unsigned long long now) {
    draw_rect(c, 0, 0, CW, CH, BG);
    const char *title = m->friendPage ? "MY FRIENDS" : "FRIENDS";
    bold(c, (CW - text_width(title, 2)) / 2, 8, 2, title, WHITE);
    pill_text(c, 250, 5, 64, 19, m->friendPage ? "< BACK" : "MINE >", YELLOW, BLACK);
    const char *sub = "AI CHARACTERS - NOT REAL PEOPLE";
    txt(c, (CW - text_width(sub, 1)) / 2, 28, 1, sub, GREY);
    for (int f = 0; f < CHAT_FRIENDS; f++) {
        int y0 = FR_Y + f * FR_H;
        uint32_t bg = f == m->friendSel ? 0x26262C : BG;
        draw_rect(c, 0, y0, CW, FR_H, bg);
        draw_rect(c, 0, y0 + FR_H - 1, CW, 1, LINEC);
        int thread = (m->friendPage ? CT_CUSTOM : CT_FRIEND) + f;
        if (m->friendPage && !m->custom[f].used) {                // an empty place
            disc(c, 26, (float)y0 + 21, 18, CIRCLE);
            draw_rect(c, 18, y0 + 20, 17, 3, YELLOW);
            draw_rect(c, 25, y0 + 13, 3, 17, YELLOW);
            bold(c, 54, y0 + 17, 1, "CREATE A FRIEND", WHITE);
            continue;
        }
        avatar(c, m, thread, 26, (float)y0 + 21, 18);
        bold(c, 54, y0 + 8, 2, name_of(m, thread), WHITE);
        txt(c, 54, y0 + 28, 1, m->friendPage ? LIKES_NAME[m->custom[f].v[FD_LIKES]] : FRIEND_LIKES[f], GREY);
        disc(c, 38, (float)y0 + 34, 5, bg);
        disc(c, 38, (float)y0 + 34, 3.5f, 0x35D065);             // always ready to chat
        int sn = streak_now(&m->th[thread], g_today);
        if (sn) streak_badge(c, 170, y0 + 32, sn, m->th[thread].lastDay < g_today);
        if (m->friendPage) pill_text(c, 208, y0 + 12, 44, 19, "EDIT", 0x45454C, WHITE);
        pill_text(c, 258, y0 + 12, 52, 19, "CHAT", BLUE, WHITE);
    }
    nav_bar(c, 3);
    draw_note(c, m, now, 188);
}

// The friend maker: preview on the top screen, choices on the bottom one.
static void draw_editor(uint16_t *top, uint16_t *c, Chat *m, unsigned long long now) {
    const FriendDef *d = &m->draft;
    const char *name = d->name[0] ? d->name : "NO NAME YET";

    draw_rect(top, 0, 0, CW, CH, BG);
    bold(top, (CW - text_width("YOUR FRIEND", 1)) / 2, 12, 1, "YOUR FRIEND", GREY);
    Look l = look_of(d);
    avatar_look(top, &l, CW / 2, 98, 66);
    bold(top, (CW - text_width(name, 3)) / 2, 178, 3, name, WHITE);
    txt(top, (CW - text_width(LIKES_HOBBY[d->v[FD_LIKES]], 1)) / 2, 212, 1, LIKES_HOBBY[d->v[FD_LIKES]], GREY);

    draw_rect(c, 0, 0, CW, CH, BG);
    const char *title = m->custom[m->editSlot].used ? "EDIT FRIEND" : "CREATE A FRIEND";
    bold(c, (CW - text_width(title, 2)) / 2, 5, 2, title, WHITE);
    for (int r = 0; r < ED_ROWS; r++) {
        int y = ED_Y + r * ED_H;
        bool lit = r == m->editRow;
        draw_rect(c, 6, y + 1, CW - 12, ED_H - 2, lit ? 0x38383F : CIRCLE);
        bool typed = r == ED_NAME || r == ED_FOOD;
        const char *label = r == ED_NAME ? "NAME" : r == ED_FOOD ? "FAV FOOD" : FD_LABEL[r - 1];
        const char *value = r == ED_NAME ? (d->name[0] ? d->name : "TAP TO TYPE")
                          : r == ED_FOOD ? (d->food[0] ? d->food : "TAP TO TYPE") : fd_value(d, r - 1);
        txt(c, 14, y + 5, 1, label, lit ? YELLOW : GREY);
        txt(c, 192 - text_width(value, 1) / 2, y + 5, 1, value, WHITE);
        if (!typed) {
            txt(c, 106, y + 5, 1, "<", YELLOW);
            txt(c, CW - 19, y + 5, 1, ">", YELLOW);
        }
    }
    pill_text(c, 6, ED_BTN_Y + 4, 92, 22, "B: CANCEL", 0x45454C, WHITE);
    if (m->custom[m->editSlot].used)
        pill_text(c, 108, ED_BTN_Y + 4, 104, 22, m->confirmDelete ? "TAP AGAIN" : "DELETE", RED, WHITE);
    pill_text(c, 222, ED_BTN_Y + 4, 92, 22, "X: SAVE", YELLOW, BLACK);
    draw_note(c, m, now, 185);
}

// The bottom screen while a chat is open: back, quick chats and the "send a chat" bar.
static void draw_thread_controls(uint16_t *c, Chat *m, unsigned long long now) {
    draw_rect(c, 0, 0, CW, CH, THEMES[theme_of(m, m->sel)].bg);
    draw_line(c, 21, 10, 15, 18, 2.8f, WHITE);                   // back
    draw_line(c, 15, 18, 21, 26, 2.8f, WHITE);
    avatar(c, m, m->sel, 56, 18, 13);
    bold(c, 76, 11, 2, name_of(m, m->sel), WHITE);
    disc(c, 296, 18, 13, CIRCLE);
    dots3(c, 296, 18);
    draw_rect(c, 0, 37, CW, 1, LINEC);

    if (can_chat(m, m->sel)) {
        bold(c, 8, 47, 1, "QUICK CHATS", GREY);
        for (int i = 0; i < 6; i++) {
            rrect(c, chip_x(i), chip_y(i), 148, 30, 15, CIRCLE);
            txt(c, chip_x(i) + (148 - text_width(CHIPS[i], 1)) / 2, chip_y(i) + 11, 1, CHIPS[i], WHITE);
        }
        rrect(c, 8, IN_Y, 250, IN_H, 16, CIRCLE);
        txt(c, 26, IN_Y + 12, 1, "SEND A CHAT", 0x7A7A80);
        disc(c, 286, IN_Y + IN_H / 2, 16, YELLOW);               // send arrow
        draw_line(c, 279, IN_Y + 16, 293, IN_Y + 16, 2.4f, BLACK);
        draw_line(c, 287, IN_Y + 9, 294, IN_Y + 16, 2.4f, BLACK);
        draw_line(c, 287, IN_Y + 23, 294, IN_Y + 16, 2.4f, BLACK);
        txt(c, 8, 218, 1, "A: TYPE   X: OPTIONS   B: BACK   D-PAD: SCROLL", 0x6E6E74);
    } else {
        const char *a = "TEAM SNAPCHAT SENDS UPDATES", *b = "YOU CANT REPLY TO THIS CHAT";
        txt(c, (CW - text_width(a, 1)) / 2, 90, 1, a, WHITE);
        txt(c, (CW - text_width(b, 1)) / 2, 106, 1, b, GREY);
        rrect(c, 8, IN_Y, 304, IN_H, 16, 0x1C1C1F);
        txt(c, 26, IN_Y + 12, 1, "CANT REPLY", 0x55555A);
        txt(c, 8, 218, 1, "X: OPTIONS   B: BACK   D-PAD: SCROLL CHAT", 0x6E6E74);
    }
    draw_note(c, m, now, 144);
}


/* ---------------- drawing: options, themes, photo grid ---------------- */

// Back arrow, the chat's face and a title; the bottom screen of every chat sub-page.
static void sub_header(uint16_t *c, Chat *m, const char *title) {
    draw_rect(c, 0, 0, CW, CH, THEMES[theme_of(m, m->sel)].bg);
    draw_line(c, 21, 10, 15, 18, 2.8f, WHITE);
    draw_line(c, 15, 18, 21, 26, 2.8f, WHITE);
    avatar(c, m, m->sel, 56, 18, 13);
    bold(c, 76, 11, 2, title, WHITE);
    draw_rect(c, 0, 37, CW, 1, LINEC);
}

static void draw_options(uint16_t *c, Chat *m, unsigned long long now) {
    const ChatThread *t = &m->th[m->sel];
    sub_header(c, m, "OPTIONS");
    bool snapOk = can_chat(m, m->sel);
    char hist[16];
    snprintf(hist, sizeof(hist), "%d SENT", t->snapCount);
    const char *label[OP_ROWS] = { "NICKNAME", "THEME", "SEND A SNAP", "PHOTO HISTORY", "RESET NICKNAME" };
    const char *value[OP_ROWS] = { t->nick[0] ? t->nick : "NONE", THEMES[theme_of(m, m->sel)].name,
                                   snapOk ? ">" : "NOT HERE", hist, t->nick[0] ? ">" : "-" };
    for (int r = 0; r < OP_ROWS; r++) {
        int y = OP_Y + r * OP_H;
        bool lit = r == m->opSel;
        draw_rect(c, 6, y + 1, CW - 12, OP_H - 2, lit ? 0x38383F : CIRCLE);
        bool off = (r == OP_SEND && !snapOk) || (r == OP_RESET && !t->nick[0]);
        txt(c, 14, y + 10, 1, label[r], off ? 0x6E6E74 : lit ? YELLOW : WHITE);
        txt(c, CW - 14 - text_width(value[r], 1), y + 10, 1, value[r], GREY);
    }
    // streak line (friends only; there are no streaks with My AI or Team Snapchat)
    if (m->sel >= CT_FRIEND && snapOk) {
        int n = streak_now(t, g_today);
        if (n) {
            bool risk = t->lastDay < g_today;
            streak_badge(c, 20, 202, n, risk);
            txt(c, 66, 199, 1, risk ? "DAY STREAK - SNAP TODAY TO KEEP IT" : "DAY STREAK - SAFE FOR TODAY", WHITE);
        } else {
            txt(c, 14, 199, 1, "NO STREAK YET - SEND A SNAP TO START ONE", GREY);
        }
    } else {
        txt(c, 14, 199, 1, "STREAKS ARE FOR YOUR FRIENDS", GREY);
    }
    txt(c, 8, 218, 1, "A OR TAP: SELECT   B: BACK", 0x6E6E74);
    draw_note(c, m, now, 168);
}

static void draw_themes(uint16_t *c, Chat *m, unsigned long long now) {
    sub_header(c, m, "THEMES");
    for (int i = 0; i < N_THEMES; i++) {
        const Theme *th = &THEMES[i];
        int x = tx(i), y = ty(i);
        if (i == m->themeSel) draw_rect(c, x - 2, y - 2, 152, 48, YELLOW);
        draw_rect(c, x, y, 148, 44, th->bg);
        draw_rect(c, x, y, 148, 12, th->bar);
        bold(c, x + 8, y + 18, 1, th->name, WHITE);
        if (m->th[m->sel].theme == i) txt(c, x + 8, y + 31, 1, "CURRENT", GREY);
        disc(c, (float)x + 118, (float)y + 28, 6, th->mine);
        disc(c, (float)x + 134, (float)y + 28, 6, th->theirs);
    }
    txt(c, 8, 218, 1, "A OR TAP: APPLY   D-PAD: LOOK   B: CANCEL", 0x6E6E74);
    draw_note(c, m, now, 196);
}

// "11/28 8:05 AM" from a name like 20261128_080500_00.bmp
static void snap_time(const char *name, char *out, size_t len) {
    int y, mo, d, h, mi;
    if (sscanf(name, "%4d%2d%2d_%2d%2d", &y, &mo, &d, &h, &mi) == 5 && mo >= 1 && mo <= 12 && d >= 1 &&
        d <= 31 && h >= 0 && h < 24 && mi >= 0 && mi < 60)
        snprintf(out, len, "%d/%d %d:%02d %s", mo, d, h % 12 ? h % 12 : 12, mi, h < 12 ? "AM" : "PM");
    else snprintf(out, len, "A SNAP");
}

static void draw_grid_top(uint16_t *c, Chat *m) {
    bool shown = m->gcount && m->bigIdx == m->gsel && m->bigOk;
    if (shown) memcpy(c, m->big, sizeof(m->big));
    else {
        draw_rect(c, 0, 0, CW, CH, BLACK);
        const char *s = m->bigIdx == m->gsel ? "CANT OPEN THIS FILE" : "LOADING";
        txt(c, (CW - text_width(s, 2)) / 2, 108, 2, s, WHITE);
    }
    char when[24], label[64];
    snap_time(m->gname[m->gsel], when, sizeof(when));
    if (m->view == CV_PICK) snprintf(label, sizeof(label), "SEND TO %s - %s", name_of(m, m->sel), when);
    else snprintf(label, sizeof(label), "SENT %s", when);
    draw_rect(c, 6, 6, text_width(label, 1) + 12, 15, BLACK);
    txt(c, 12, 10, 1, label, WHITE);
}

static void draw_grid(uint16_t *c, Chat *m, unsigned long long now) {
    bool pick = m->view == CV_PICK;
    sub_header(c, m, pick ? "SEND A SNAP" : "PHOTO HISTORY");
    if (m->gcount) draw_rect(c, gx(m->gsel) - 2, gy(m->gsel) - 2, GRID_TW + 4, GRID_TH + 4, YELLOW);
    for (int i = 0; i < m->gcount; i++) {
        int x = gx(i), y = gy(i);
        if (m->gstate[i] == 1) {
            for (int j = 0; j < GRID_TH; j++)
                memcpy(c + (y + j) * CW + x, m->gthumb[i] + j * GRID_TW, GRID_TW * sizeof(uint16_t));
        } else {
            draw_rect(c, x, y, GRID_TW, GRID_TH, CIRCLE);
            if (m->gstate[i] == 2) txt(c, x + 14, y + 25, 1, "MISSING", GREY);
        }
    }
    if (pick) {
        pill_text(c, 90, 176, 140, 24, "SEND SNAP", YELLOW, BLACK);
        txt(c, 8, 218, 1, "A: SEND   D-PAD OR TAP: CHOOSE   B: BACK", 0x6E6E74);
    } else {
        txt(c, 8, 218, 1, "D-PAD OR TAP: LOOK   B: BACK", 0x6E6E74);
    }
    draw_note(c, m, now, 200);
}

void chat_draw(Chat *m, uint16_t *top, uint16_t *bottom, unsigned long long nowMs) {
    g_today = local_day();
    if (m->view == CV_EDIT) { draw_editor(top, bottom, m, nowMs); return; }
    if (m->view == CV_PICK || m->view == CV_HISTORY) {
        draw_grid_top(top, m);
        draw_grid(bottom, m, nowMs);
        return;
    }
    draw_thread(top, m, nowMs);
    if (m->view == CV_OPTIONS) { draw_options(bottom, m, nowMs); return; }
    if (m->view == CV_THEMES) { draw_themes(bottom, m, nowMs); return; }
    if (m->view == CV_FRIENDS && !can_chat(m, m->sel)) {         // an empty place is highlighted
        draw_rect(top, 0, 0, CW, CH, BG);
        const char *a = "MAKE YOUR OWN FRIEND", *b = "PICK A NAME, A LOOK AND WHAT THEY LIKE";
        bold(top, (CW - text_width(a, 2)) / 2, 100, 2, a, WHITE);
        txt(top, (CW - text_width(b, 1)) / 2, 128, 1, b, GREY);
    }
    if (m->view == CV_FRIENDS) draw_friends(bottom, m, nowMs);
    else if (m->view == CV_THREAD) draw_thread_controls(bottom, m, nowMs);
    else draw_list(bottom, m, nowMs);
}
