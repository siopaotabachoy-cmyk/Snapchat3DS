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
    if (thread >= CT_ONLINE) return m->online[thread - CT_ONLINE].name;
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
    if (thread >= CT_ONLINE && thread < CHAT_THREADS) return m->online[thread - CT_ONLINE].used;
    return thread >= CT_CUSTOM && thread < CT_ONLINE && m->custom[thread - CT_CUSTOM].used;
}

#define LIST_VIS 3                       // chat rows that fit on the bottom screen

// The chats on the chat list: My AI, Team Snapchat, then every friend you made.
// Friends are chatted with offline, so a new friend is listed the moment it is saved.
static int list_threads(const Chat *m, int *ids) {
    int n = 0;
    ids[n++] = CT_AI;
    ids[n++] = CT_TEAM;
    for (int k = 0; k < CHAT_ONLINE; k++)                        // real friends first
        if (m->online[k].used) ids[n++] = CT_ONLINE + k;
    for (int k = 0; k < CHAT_CUSTOM; k++)
        if (m->custom[k].used) ids[n++] = CT_CUSTOM + k;
    return n;
}

static int list_index(const int *ids, int n, int thread) {
    for (int i = 0; i < n; i++)
        if (ids[i] == thread) return i;
    return -1;
}

static void list_clamp(Chat *m) {
    int ids[CHAT_THREADS], n = list_threads(m, ids);
    int most = n > LIST_VIS ? n - LIST_VIS : 0;
    if (m->listScroll > most) m->listScroll = most;
    if (m->listScroll < 0) m->listScroll = 0;
}

// Scrolls the chat list so the highlighted chat is on screen.
static void list_fit(Chat *m) {
    int ids[CHAT_THREADS], n = list_threads(m, ids);
    int at = list_index(ids, n, m->sel);
    if (at >= 0) {
        if (at < m->listScroll) m->listScroll = at;
        if (at >= m->listScroll + LIST_VIS) m->listScroll = at - LIST_VIS + 1;
    }
    list_clamp(m);
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
    else if (who >= CT_ONLINE) {                                 // a real friend: a face made from the username
        static const uint32_t BACKS[6] = { 0xF7A8C8, 0x8CC8F0, 0xA8DC8C, 0xF8C878, 0xB58CFF, 0x7FE0D0 };
        static const uint32_t HAIRS[5] = { 0x1C1618, 0x4B2E22, 0x6B4428, 0xC8A050, 0xB03A2E };
        static const uint32_t SKINS[4] = { 0xF1C7A5, 0xD99A68, 0xB87C54, 0xE6B592 };
        static const uint32_t TOPS[5]  = { 0xD6409F, 0x2456A8, 0x2E8858, 0xE0661E, 0x6A4BC8 };
        unsigned h = 5381;
        for (const char *s = m->online[who - CT_ONLINE].user; *s; s++) h = h * 33 + (unsigned char)*s;
        Look l = { BACKS[h % 6], HAIRS[(h >> 3) % 5], SKINS[(h >> 6) % 4], TOPS[(h >> 9) % 5],
                   (int)((h >> 12) % 4), ((h >> 15) & 3) == 0 };
        avatar_look(c, &l, x, y, r);
    } else {
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

/* ---------------- emoji and voice notes ----------------
   They are ordinary chat text, so they are saved and sent like any message:
   "[E:3]" is emoji number 3, "[V:5:0369630]" is a 5 second voice note whose
   loudness bars are the digits. Friends who run Snap3DS see the pictures too. */

static bool safe_name(const char *s);

static void tri_down(uint16_t *c, int cx, int top, int halfw, int h, uint32_t rgb) {
    for (int i = 0; i < h; i++) {
        int w = halfw * (h - i) / h;
        if (w > 0) draw_rect(c, cx - w, top + i, w * 2, 1, rgb);
    }
}

static void tri_up(uint16_t *c, int cx, int top, int halfw, int h, uint32_t rgb) {
    for (int i = 0; i < h; i++) {
        int w = halfw * (i + 1) / h;
        draw_rect(c, cx - w, top + i, w * 2, 1, rgb);
    }
}

static void heart(uint16_t *c, float cx, float cy, float s, uint32_t rgb) {
    disc(c, cx - 0.5f * s, cy - 0.3f * s, 0.55f * s, rgb);
    disc(c, cx + 0.5f * s, cy - 0.3f * s, 0.55f * s, rgb);
    tri_down(c, (int)cx, (int)(cy - 0.15f * s), (int)(1.02f * s), (int)(1.1f * s), rgb);
}

// 0 smile, 1 laugh, 2 wink, 3 cool, 4 heart eyes, 5 cry, 6 angry, 7 surprised, 8 heart, 9 star, 10 fire, 11 ghost
static void draw_emoji(uint16_t *c, int id, float cx, float cy, float r) {
    const uint32_t Y = 0xFFCC33, K = 0x2B2118;
    float t = 0.13f * r < 1.0f ? 1.0f : 0.13f * r;               // line thickness
    float ex = 0.36f * r, ey = cy - 0.18f * r;                   // eye spots
    if (id <= 7) disc(c, cx, cy, r, id == 6 ? 0xF0643C : Y);
    if (id == 0 || id == 2 || id == 3 || id == 4) {              // the smile
        draw_line(c, cx - 0.45f * r, cy + 0.15f * r, cx - 0.25f * r, cy + 0.45f * r, t, K);
        draw_line(c, cx - 0.25f * r, cy + 0.45f * r, cx + 0.25f * r, cy + 0.45f * r, t, K);
        draw_line(c, cx + 0.25f * r, cy + 0.45f * r, cx + 0.45f * r, cy + 0.15f * r, t, K);
    }
    if (id == 5 || id == 6) {                                    // the frown
        draw_line(c, cx - 0.4f * r, cy + 0.58f * r, cx - 0.2f * r, cy + 0.38f * r, t, K);
        draw_line(c, cx - 0.2f * r, cy + 0.38f * r, cx + 0.2f * r, cy + 0.38f * r, t, K);
        draw_line(c, cx + 0.2f * r, cy + 0.38f * r, cx + 0.4f * r, cy + 0.58f * r, t, K);
    }
    switch (id) {
    case 0:
        disc(c, cx - ex, ey, 0.12f * r, K); disc(c, cx + ex, ey, 0.12f * r, K);
        break;
    case 1:
        draw_line(c, cx - ex - 0.2f * r, ey + 0.1f * r, cx - ex, ey - 0.1f * r, t, K);
        draw_line(c, cx - ex, ey - 0.1f * r, cx - ex + 0.2f * r, ey + 0.1f * r, t, K);
        draw_line(c, cx + ex - 0.2f * r, ey + 0.1f * r, cx + ex, ey - 0.1f * r, t, K);
        draw_line(c, cx + ex, ey - 0.1f * r, cx + ex + 0.2f * r, ey + 0.1f * r, t, K);
        draw_ellipse(c, cx, cy + 0.32f * r, 0.5f * r, 0.36f * r, 0, K, 255);
        draw_rect(c, (int)(cx - 0.4f * r), (int)(cy + 0.16f * r), (int)(0.8f * r), r > 12 ? 2 : 1, WHITE);
        break;
    case 2:
        disc(c, cx - ex, ey, 0.12f * r, K);
        draw_line(c, cx + ex - 0.18f * r, ey, cx + ex + 0.18f * r, ey, t, K);
        break;
    case 3:
        draw_rect(c, (int)(cx - 0.78f * r), (int)(ey - 0.2f * r), (int)(0.68f * r), (int)(0.4f * r) + 1, K);
        draw_rect(c, (int)(cx + 0.10f * r), (int)(ey - 0.2f * r), (int)(0.68f * r), (int)(0.4f * r) + 1, K);
        draw_line(c, cx - 0.12f * r, ey - 0.1f * r, cx + 0.12f * r, ey - 0.1f * r, t, K);
        break;
    case 4:
        heart(c, cx - ex, ey, 0.24f * r, RED); heart(c, cx + ex, ey, 0.24f * r, RED);
        break;
    case 5:
        disc(c, cx - ex, ey, 0.12f * r, K); disc(c, cx + ex, ey, 0.12f * r, K);
        disc(c, cx - ex - 0.04f * r, ey + 0.38f * r, 0.13f * r, 0x4DB8FF);
        disc(c, cx + ex + 0.04f * r, ey + 0.38f * r, 0.13f * r, 0x4DB8FF);
        break;
    case 6:
        disc(c, cx - ex, ey + 0.06f * r, 0.12f * r, K); disc(c, cx + ex, ey + 0.06f * r, 0.12f * r, K);
        draw_line(c, cx - 0.65f * r, ey - 0.32f * r, cx - 0.1f * r, ey - 0.08f * r, t, K);
        draw_line(c, cx + 0.65f * r, ey - 0.32f * r, cx + 0.1f * r, ey - 0.08f * r, t, K);
        break;
    case 7:
        disc(c, cx - ex, ey, 0.13f * r, K); disc(c, cx + ex, ey, 0.13f * r, K);
        draw_line(c, cx - 0.6f * r, ey - 0.3f * r, cx - 0.15f * r, ey - 0.34f * r, t, K);
        draw_line(c, cx + 0.6f * r, ey - 0.3f * r, cx + 0.15f * r, ey - 0.34f * r, t, K);
        draw_ellipse(c, cx, cy + 0.42f * r, 0.18f * r, 0.26f * r, 0, K, 255);
        break;
    case 8:
        heart(c, cx, cy - 0.05f * r, 0.9f * r, RED);
        break;
    case 9: {                                                    // a thick pentagram looks like a star
        static const float PX[5] = { 0.0f, 0.951f, 0.588f, -0.588f, -0.951f };
        static const float PY[5] = { -1.0f, -0.309f, 0.809f, 0.809f, -0.309f };
        for (int i = 0; i < 5; i++) {
            int j = (i + 2) % 5;
            draw_line(c, cx + PX[i] * 0.92f * r, cy + PY[i] * 0.92f * r + 0.05f * r,
                      cx + PX[j] * 0.92f * r, cy + PY[j] * 0.92f * r + 0.05f * r, 0.32f * r, 0xFFD23F);
        }
        disc(c, cx, cy + 0.05f * r, 0.45f * r, 0xFFD23F);
        break;
    }
    case 10:
        disc(c, cx, cy + 0.25f * r, 0.62f * r, 0xFF7A1A);
        tri_up(c, (int)(cx + 0.05f * r), (int)(cy - 0.95f * r), (int)(0.5f * r), (int)(1.1f * r), 0xFF7A1A);
        disc(c, cx, cy + 0.38f * r, 0.36f * r, 0xFFC93C);
        disc(c, cx, cy + 0.46f * r, 0.18f * r, 0xFFF1A8);
        break;
    case 11:
        ghost_shape(c, cx, cy, 0.82f * r, 0xEDEDF2);
        disc(c, cx - 0.28f * r, cy - 0.12f * r, 0.12f * r, K);
        disc(c, cx + 0.28f * r, cy - 0.12f * r, 0.12f * r, K);
        draw_ellipse(c, cx, cy + 0.2f * r, 0.12f * r, 0.16f * r, 0, K, 255);
        break;
    }
}

// Text made only of "[E:n]" pieces. Returns how many emoji (ids in ids[]), or 0 when it is anything else.
static int parse_emoji(const char *s, int *ids, int max) {
    int n = 0;
    while (*s) {
        if (s[0] != '[' || toupper((unsigned char)s[1]) != 'E' || s[2] != ':') return 0;
        s += 3;
        int v = 0, d = 0;
        while (isdigit((unsigned char)*s) && d < 3) { v = v * 10 + (*s - '0'); s++; d++; }
        if (!d || *s != ']' || v >= EMOJI_COUNT || n >= max) return 0;
        ids[n++] = v;
        s++;
        while (*s == ' ') s++;
    }
    return n;
}

// "[V:seconds:digits]". The digits are the loudness bars, 0..9.
static bool parse_voice(const char *s, int *secs, int *bars, int *nb) {
    if (s[0] != '[' || toupper((unsigned char)s[1]) != 'V' || s[2] != ':') return false;
    s += 3;
    int v = 0, d = 0;
    while (isdigit((unsigned char)*s) && d < 3) { v = v * 10 + (*s - '0'); s++; d++; }
    if (!d || *s != ':' || v < 1 || v > 60) return false;
    s++;
    int n = 0;
    while (isdigit((unsigned char)*s) && n < VOICE_BARS) bars[n++] = *s++ - '0';
    if (n < 1 || *s != ']' || s[1]) return false;
    *secs = v;
    *nb = n;
    return true;
}

// "[S:file.bmp]" is a snap you sent, "[I:file.bmp]" a photo. Returns 'S', 'I' or 0.
static char parse_media(const char *s, char *name, size_t len) {
    if (s[0] != '[' || (s[1] != 'S' && s[1] != 'I') || s[2] != ':') return 0;
    const char *e = strchr(s + 3, ']');
    if (!e || e[1] || (size_t)(e - (s + 3)) >= len) return 0;
    size_t n = (size_t)(e - (s + 3));
    memcpy(name, s + 3, n);
    name[n] = 0;
    return safe_name(name) ? s[1] : 0;
}

static bool g_mediaLoaded;                                       // one picture is read from the card per frame

// The small picture for a snap or photo. NULL while it is loading or when the file is gone.
static const uint16_t *media_thumb(Chat *m, const char *name, bool *missing) {
    *missing = false;
    for (int i = 0; i < MEDIA_CACHE; i++)
        if (m->tc[i].state && strcmp(m->tc[i].name, name) == 0) {
            *missing = m->tc[i].state == 2;
            return m->tc[i].state == 1 ? m->tc[i].px : NULL;
        }
    if (g_mediaLoaded || !m->dir[0]) return NULL;
    g_mediaLoaded = true;
    int k = m->tcNext;
    m->tcNext = (m->tcNext + 1) % MEDIA_CACHE;
    char path[160];
    snprintf(m->tc[k].name, sizeof(m->tc[k].name), "%s", name);
    snprintf(path, sizeof(path), "%.95s/%.39s", m->dir, name);
    m->tc[k].state = mem_load_bmp(path, m->tc[k].px, MEDIA_TW, MEDIA_TH) ? 1 : 2;
    *missing = m->tc[k].state == 2;
    return m->tc[k].state == 1 ? m->tc[k].px : NULL;
}

// A snap or photo inside the chat: the picture in a frame (purple for snaps).
static void draw_media(uint16_t *c, Chat *m, int x, int y, char kind, const char *name) {
    bool missing;
    const uint16_t *px = media_thumb(m, name, &missing);
    draw_rect(c, x - 2, y - 2, MEDIA_TW + 4, MEDIA_TH + 4, kind == 'S' ? 0xA259FF : 0x55555C);
    if (px) {
        for (int j = 0; j < MEDIA_TH; j++)
            if (y + j >= 0 && y + j < CH) memcpy(c + (y + j) * CW + x, px + j * MEDIA_TW, MEDIA_TW * sizeof(uint16_t));
    } else {
        draw_rect(c, x, y, MEDIA_TW, MEDIA_TH, CIRCLE);
        const char *t = missing ? "MISSING" : "LOADING";
        txt(c, x + (MEDIA_TW - text_width(t, 1)) / 2, y + 26, 1, t, GREY);
    }
    if (kind == 'S') pill_text(c, x + 4, y + 4, 30, 12, "SNAP", 0xA259FF, WHITE);
}

// A voice note bubble: microphone, the sound wave and how long it is.
static void draw_voice_bubble(uint16_t *c, int x, int y, int secs, const int *bars, int nb, uint32_t col) {
    char len[8];
    snprintf(len, sizeof(len), "%dS", secs);
    int w = 26 + nb * 4 + text_width(len, 1) + 10;
    rrect(c, x, y, w, 22, 11, 0x2E2E34);
    rrect(c, x + 9, y + 4, 6, 10, 3, WHITE);                     // microphone
    draw_rect(c, x + 11, y + 15, 2, 3, WHITE);
    draw_rect(c, x + 8, y + 17, 8, 1, WHITE);
    for (int i = 0; i < nb; i++) {
        int h = 2 + bars[i] * 14 / 9;
        draw_rect(c, x + 24 + i * 4, y + 11 - h / 2, 2, h, col);
    }
    txt(c, x + 28 + nb * 4, y + 8, 1, len, GREY);
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
    for (int i = 0; i < CHAT_ONLINE; i++)     // U = real friend in this slot
        if (m->online[i].used) fprintf(f, "U %d %s %s\n", i, m->online[i].user, m->online[i].name);
    if (m->lastId > 0) fprintf(f, "I %lld\n", m->lastId);
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
        else if (line[0] == 'U') {
            char u[ONLINE_USER + 2];
            at = 0;
            if (sscanf(rest, "%d %19s %n", &t, u, &at) == 2 && t >= 0 && t < CHAT_ONLINE &&
                strlen(u) <= ONLINE_USER) {
                m->online[t].used = true;
                snprintf(m->online[t].user, sizeof(m->online[t].user), "%.*s", ONLINE_USER, u);
                clean_word(at > 0 ? rest + at : "", m->online[t].name, ONLINE_NAME);
                if (!m->online[t].name[0]) snprintf(m->online[t].name, sizeof(m->online[t].name), "%.*s", ONLINE_NAME, u);
            }
        } else if (line[0] == 'I') {
            long long id;
            if (sscanf(rest, "%lld", &id) == 1 && id > 0) m->lastId = id;
        } else if (line[0] == 'P') {
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
    {
        int ids[CHAT_THREADS], n = list_threads(m, ids);
        if (list_index(ids, n, m->sel) < 0) m->sel = CT_AI;
    }
    m->friendsFromList = false;
    list_fit(m);
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
    m->addFromList = false;
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
    if (m->addFromList) {                                        // ADD FRIENDS: back to the chat list,
        m->addFromList = false;                                  // where the new friend is now listed
        m->sel = CT_CUSTOM + m->editSlot;                        // (chat_open drops this if nothing was saved)
        chat_open(m);
        m->scroll = 0;
        return;
    }
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

// An emoji or a voice note gets a matching answer instead of going through the word rules.
static bool token_reply(Chat *m, const char *p) {
    int ids[4], secs, bars[VOICE_BARS], nb;
    bool ai = m->sel == CT_AI;
    if (parse_emoji(p, ids, 4) > 0) {
        if (ai) snprintf(m->reply, sizeof(m->reply), "NICE EMOJI! I CANT SEND ONE BACK YET, BUT I LIKE IT.");
        else snprintf(m->reply, sizeof(m->reply), "[E:%d]", rand() % EMOJI_COUNT);
        return true;
    }
    if (parse_voice(p, &secs, bars, &nb)) {
        static const char *const V[3] = { "LOVED YOUR VOICE NOTE!", "HA, I LISTENED TWICE. NICE ONE!", "GOT YOUR VOICE NOTE. YOU SOUND GREAT!" };
        snprintf(m->reply, sizeof(m->reply), "%s", ai ? "I GOT YOUR VOICE NOTE, BUT I CANT HEAR AUDIO YET. TYPE IT TO ME!" : V[rand() % 3]);
        return true;
    }
    return false;
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
    if (m->sel >= CT_ONLINE) {                   // a real friend: the app sends it to the server
        if (m->outCount >= CHAT_OUTBOX) { say(m, "TOO MANY UNSENT MESSAGES - WAIT A MOMENT"); return; }
        OutMsg *o = &m->out[m->outCount++];
        snprintf(o->user, sizeof(o->user), "%s", m->online[m->sel - CT_ONLINE].user);
        snprintf(o->text, sizeof(o->text), "%s", p);
        add_msg(&m->th[m->sel], true, p);
        m->th[m->sel].touched = true;
        m->scroll = 0;
        m->saveFailed = !chat_save(m);
        if (m->saveFailed) say(m, "CHAT NOT SAVED - CHECK SD CARD");
        return;
    }
    add_msg(&m->th[m->sel], true, p);
    m->th[m->sel].touched = true;
    if (!token_reply(m, p)) {
        if (m->sel >= CT_CUSTOM) reply_custom(m, &m->custom[m->sel - CT_CUSTOM], p);
        else brain_reply(&m->memory, m->sel == CT_AI ? BP_MYAI : BP_MIA + m->sel - CT_FRIEND, p,
                         time(NULL), m->reply, sizeof(m->reply));
    }
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

// Sends a photo to the open chat as a snap ('S', keeps the streak going) or a plain photo ('I').
// The chat shows the picture. Friends answer on their own.
static void send_media(Chat *m, const char *file, char kind, unsigned long long nowMs) {
    if (!can_chat(m, m->sel) || !file[0]) return;
    if (m->sel >= CT_ONLINE) {                   // photos cannot go online yet
        m->view = CV_THREAD;
        say(m, "PHOTOS TO ONLINE FRIENDS: NOT YET");
        return;
    }
    ChatThread *t = &m->th[m->sel];
    deliver(m);                                  // an unanswered one arrives first
    char tok[CHAT_TEXT];
    snprintf(tok, sizeof(tok), "[%c:%.60s]", kind, file);
    add_msg(t, true, tok);
    t->touched = true;
    if (t->snapCount == SNAP_KEEP) {
        memmove(t->snaps[0], t->snaps[1], sizeof(t->snaps[0]) * (SNAP_KEEP - 1));
        t->snapCount--;
    }
    snprintf(t->snaps[t->snapCount++], sizeof(t->snaps[0]), "%s", file);
    bool isFriend = m->sel >= CT_FRIEND;         // no streaks with My AI
    if (kind == 'S' && isFriend) bump_streak(t, local_day());

    static const char *const BACK[4] = {
        "LOVE THIS SNAP! SNAPPING YOU BACK.", "HA, NICE ONE! HERE IS MINE.",
        "WOW, GREAT PHOTO! SNAP BACK SENT.", "COOL! KEEP THE STREAK GOING."
    };
    static const char *const LIKE[3] = { "NICE PHOTO! LOVE IT.", "WOW, THAT LOOKS GREAT!", "COOL PICTURE! THANKS FOR SHARING." };
    const char *ai = kind == 'S' ? "THANKS FOR THE SNAP! I CANT SEE PICTURES YET, BUT I LIKE GETTING THEM."
                                 : "THANKS FOR THE PHOTO! I CANT SEE PICTURES YET, BUT I LIKE GETTING THEM.";
    snprintf(m->reply, sizeof(m->reply), "%s", !isFriend ? ai : kind == 'S' ? BACK[rand() % 4] : LIKE[rand() % 3]);
    m->replyTo = m->sel;
    m->pending = true;
    m->replyAt = nowMs + 800 + (unsigned long long)(rand() % 900);
    m->scroll = 0;
    m->view = CV_THREAD;
    char note[44];
    if (kind == 'S' && isFriend) snprintf(note, sizeof(note), "SNAP SENT - STREAK %d", t->streak);
    else snprintf(note, sizeof(note), kind == 'S' ? "SNAP SENT" : "PHOTO SENT");
    say(m, note);
    store(m);                                    // says so itself if the card is full
}

static void send_snap(Chat *m, unsigned long long nowMs) {
    if (m->gcount == 0) return;
    send_media(m, m->gname[m->gsel], m->pickKind ? m->pickKind : 'S', nowMs);
}

void chat_attach(Chat *m, const char *file, unsigned long long nowMs) {
    m->view = CV_THREAD;
    if (!safe_name(file)) return;
    send_media(m, file, 'S', nowMs);
}

const char *chat_open_name(const Chat *m) { return name_of(m, m->sel); }

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
        else { m->pickKind = 'S'; m->gridFrom = CV_OPTIONS; grid_start(m, CV_PICK); }
        break;
    case OP_HISTORY:
        m->gridFrom = CV_OPTIONS;
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
    int back = m->gridFrom == CV_THREAD ? CV_THREAD : CV_OPTIONS;    // where you came from
    if (in->keys & CK_B) { m->view = back; return CR_NONE; }
    int s = m->gsel;
    if (in->keys & CK_LEFT) s--;
    if (in->keys & CK_RIGHT) s++;
    if (in->keys & CK_UP) s -= 4;
    if (in->keys & CK_DOWN) s += 4;
    if (s >= 0 && s < m->gcount) m->gsel = s;
    bool send = m->view == CV_PICK && (in->keys & CK_A);
    if (in->down) {
        if (in->y < 38) { if (in->x < 40) m->view = back; return CR_NONE; }
        for (int i = 0; i < m->gcount; i++)
            if (inside(in->x, in->y, gx(i), gy(i), GRID_TW, GRID_TH)) m->gsel = i;
        if (m->view == CV_PICK && inside(in->x, in->y, 90, 176, 140, 24)) send = true;
    }
    if (send) send_snap(m, in->ms);
    return CR_NONE;
}

/* ---------------- emoji picker, voice recorder, games ---------------- */

static int ex(int i) { return 8 + (i % 4) * 76; }              // emoji picker: 4 x 3
static int ey(int i) { return 44 + (i / 4) * 52; }

// Puts "[E:3]" or "[V:...]" in the open chat like a typed message.
static void send_token(Chat *m, const char *tok, unsigned long long ms) {
    m->view = CV_THREAD;
    chat_send(m, tok, ms);
}

static int update_emoji(Chat *m, const ChatInput *in) {
    if (in->keys & CK_B) { m->view = CV_THREAD; return CR_NONE; }
    int s = m->emojiSel;
    if (in->keys & CK_LEFT) s--;
    if (in->keys & CK_RIGHT) s++;
    if (in->keys & CK_UP) s -= 4;
    if (in->keys & CK_DOWN) s += 4;
    if (s >= 0 && s < EMOJI_COUNT) m->emojiSel = s;
    bool send = (in->keys & CK_A) != 0;
    if (in->down) {
        if (in->y < 38) { if (in->x < 40) m->view = CV_THREAD; return CR_NONE; }
        for (int i = 0; i < EMOJI_COUNT; i++)
            if (inside(in->x, in->y, ex(i), ey(i), 72, 48)) { m->emojiSel = i; send = true; }
    }
    if (send) {
        char tok[12];
        snprintf(tok, sizeof(tok), "[E:%d]", m->emojiSel);
        send_token(m, tok, in->ms);
    }
    return CR_NONE;
}

static void start_voice(Chat *m, const ChatInput *in) {
    if (!in->micOk) { say(m, "MICROPHONE NOT AVAILABLE"); return; }
    m->view = CV_VOICE;
    m->vRec = true;
    m->vCount = 0;
    m->vPeak = 0;
    m->vNext = in->ms + 250;
    m->note[0] = 0;
}

static int update_voice(Chat *m, const ChatInput *in) {
    if ((in->keys & CK_B) || (in->down && in->y < 38 && in->x < 40)) {   // cancel: throw the recording away
        m->vRec = false;
        m->view = CV_THREAD;
        return CR_NONE;
    }
    if (m->vRec) {
        if (in->mic > m->vPeak) m->vPeak = in->mic;
        while (m->vCount < VOICE_BARS && in->ms >= m->vNext) {           // one bar every 250 ms
            int lv = m->vPeak * 9 / 40;                                  // speech is quiet: make it show
            if (lv > 9) lv = 9;
            if (lv == 0 && m->vPeak > 3) lv = 1;
            m->vBars[m->vCount++] = lv;
            m->vPeak = 0;
            m->vNext += 250;
        }
        if (m->vCount >= VOICE_BARS) m->vRec = false;                    // full: 8 seconds
    }
    bool send = (in->keys & CK_A) != 0;
    if (in->down && inside(in->x, in->y, 90, 176, 140, 24)) send = true;
    if (!send) return CR_NONE;
    int loud = 0;
    for (int i = 0; i < m->vCount; i++) if (m->vBars[i] > loud) loud = m->vBars[i];
    if (m->vCount < 4) { say(m, "TOO SHORT - KEEP TALKING"); return CR_NONE; }
    if (!loud) { say(m, "I HEARD NOTHING - CHECK THE MIC"); return CR_NONE; }
    char tok[VOICE_BARS + 16];
    int n = snprintf(tok, sizeof(tok), "[V:%d:", (m->vCount + 3) / 4);
    for (int i = 0; i < m->vCount; i++) tok[n++] = (char)('0' + m->vBars[i]);
    tok[n++] = ']';
    tok[n] = 0;
    m->vRec = false;
    send_token(m, tok, in->ms);
    return CR_NONE;
}

/* games: tic tac toe and rock paper scissors, played against the friend you are chatting with */

enum { GM_TTT, GM_RPS, GM_COUNT };
static const char *const GAME_NAME[GM_COUNT] = { "TIC TAC TOE", "ROCK PAPER SCISSORS" };
static const char *const GAME_INFO[GM_COUNT] = { "THREE IN A ROW WINS", "FIRST TO 3 WINS" };
static const char *const THROW_NAME[3] = { "ROCK", "PAPER", "SCISSORS" };
#define GM_Y 50
#define GM_H 46
#define TTT_X 88
#define TTT_Y 44
#define TTT_CELL 48

static void open_games(Chat *m) {
    if (m->sel >= CT_ONLINE) { say(m, "GAMES WITH ONLINE FRIENDS: NOT YET"); return; }
    m->view = CV_GAMES;
    m->note[0] = 0;
}

static void start_game(Chat *m, int game) {
    m->gameLogged = false;
    m->note[0] = 0;
    if (game == GM_TTT) {
        memset(m->ttt, 0, sizeof(m->ttt));
        m->tttState = 0;
        m->gameCur = 4;
        m->view = CV_TTT;
    } else {
        m->rpsMine = m->rpsTheirs = -1;
        m->rpsWins = m->rpsLosses = m->rpsRounds = 0;
        m->gameCur = 0;
        m->view = CV_RPS;
    }
}

// Writes the result into the chat and lets the friend answer. result: 1 you won, 2 you lost, 3 draw.
static void log_game(Chat *m, const char *game, int result, unsigned long long ms) {
    if (m->gameLogged) return;
    m->gameLogged = true;
    char msg[CHAT_TEXT];
    snprintf(msg, sizeof(msg), "PLAYED %s: %s", game, result == 1 ? "I WON!" : result == 2 ? "I LOST!" : "IT WAS A DRAW!");
    deliver(m);
    add_msg(&m->th[m->sel], true, msg);
    m->th[m->sel].touched = true;
    static const char *const WON[3] = { "GOOD GAME! YOU GOT ME THIS TIME.", "WOW, WELL PLAYED!", "OK OK, YOU WIN. REMATCH?" };
    static const char *const LOST[3] = { "HA! I WON. WANT A REMATCH?", "GOOD TRY! I GOT LUCKY.", "BETTER LUCK NEXT TIME!" };
    static const char *const DREW[3] = { "A DRAW! WE ARE EVEN.", "CLOSE ONE! AGAIN?", "NOBODY WON. LETS PLAY AGAIN." };
    const char *const *set = result == 1 ? WON : result == 2 ? LOST : DREW;
    snprintf(m->reply, sizeof(m->reply), "%s", set[rand() % 3]);
    m->replyTo = m->sel;
    m->pending = true;
    m->replyAt = ms + 800 + (unsigned long long)(rand() % 900);
    m->scroll = 0;
    store(m);
}

static const int TTT_LINES[8][3] = { {0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6} };

// 0 still playing, 1 or 2 = that player has three in a row, 3 = board full
static int ttt_state(const int *b) {
    for (int i = 0; i < 8; i++) {
        const int *l = TTT_LINES[i];
        if (b[l[0]] && b[l[0]] == b[l[1]] && b[l[0]] == b[l[2]]) return b[l[0]];
    }
    for (int i = 0; i < 9; i++) if (!b[i]) return 0;
    return 3;
}

// A square where "who" would complete a line, or -1.
static int ttt_find(const int *b, int who) {
    for (int i = 0; i < 8; i++) {
        const int *l = TTT_LINES[i];
        int mine = 0, empty = -1;
        for (int k = 0; k < 3; k++) {
            if (b[l[k]] == who) mine++;
            else if (!b[l[k]]) empty = l[k];
        }
        if (mine == 2 && empty >= 0) return empty;
    }
    return -1;
}

// The friend plays O: wins if it can, blocks you, likes the middle, and sometimes just guesses.
static void ttt_friend(Chat *m) {
    int *b = m->ttt, mv = -1;
    if (rand() % 5 != 0) {
        mv = ttt_find(b, 2);
        if (mv < 0) mv = ttt_find(b, 1);
        if (mv < 0 && !b[4]) mv = 4;
    }
    if (mv < 0) {
        int empty[9], n = 0;
        for (int i = 0; i < 9; i++) if (!b[i]) empty[n++] = i;
        if (!n) return;
        mv = empty[rand() % n];
    }
    b[mv] = 2;
}

static int update_ttt(Chat *m, const ChatInput *in) {
    if (in->keys & CK_B) { m->view = CV_GAMES; return CR_NONE; }
    if (in->keys & CK_LEFT && m->gameCur % 3 > 0) m->gameCur--;
    if (in->keys & CK_RIGHT && m->gameCur % 3 < 2) m->gameCur++;
    if (in->keys & CK_UP && m->gameCur >= 3) m->gameCur -= 3;
    if (in->keys & CK_DOWN && m->gameCur < 6) m->gameCur += 3;
    bool act = (in->keys & CK_A) != 0;
    int cell = m->gameCur;
    if (in->down) {
        if (in->y < 38) { if (in->x < 40) m->view = CV_GAMES; return CR_NONE; }
        if (m->tttState) { if (inside(in->x, in->y, 90, 198, 140, 18)) act = true; }
        else if (inside(in->x, in->y, TTT_X, TTT_Y, TTT_CELL * 3, TTT_CELL * 3)) {
            cell = (in->y - TTT_Y) / TTT_CELL * 3 + (in->x - TTT_X) / TTT_CELL;
            m->gameCur = cell;
            act = true;
        }
    }
    if (!act) return CR_NONE;
    if (m->tttState) { start_game(m, GM_TTT); return CR_NONE; }       // PLAY AGAIN
    if (m->ttt[cell]) return CR_NONE;
    m->ttt[cell] = 1;
    m->tttState = ttt_state(m->ttt);
    if (!m->tttState) { ttt_friend(m); m->tttState = ttt_state(m->ttt); }
    if (m->tttState) log_game(m, GAME_NAME[GM_TTT], m->tttState, in->ms);
    return CR_NONE;
}

static int update_rps(Chat *m, const ChatInput *in) {
    if (in->keys & CK_B) { m->view = CV_GAMES; return CR_NONE; }
    bool over = m->rpsWins >= 3 || m->rpsLosses >= 3;
    if (in->keys & CK_LEFT && m->gameCur > 0) m->gameCur--;
    if (in->keys & CK_RIGHT && m->gameCur < 2) m->gameCur++;
    bool act = (in->keys & CK_A) != 0;
    if (in->down) {
        if (in->y < 38) { if (in->x < 40) m->view = CV_GAMES; return CR_NONE; }
        if (over) { if (inside(in->x, in->y, 90, 192, 140, 22)) act = true; }
        else for (int i = 0; i < 3; i++)
            if (inside(in->x, in->y, 12 + i * 100, 76, 96, 70)) { m->gameCur = i; act = true; }
    }
    if (!act) return CR_NONE;
    if (over) { start_game(m, GM_RPS); return CR_NONE; }
    m->rpsMine = m->gameCur;
    m->rpsTheirs = rand() % 3;
    m->rpsRounds++;
    int d = (m->rpsMine - m->rpsTheirs + 3) % 3;                      // 1 = you win the round, 2 = you lose it
    if (d == 1) m->rpsWins++;
    else if (d == 2) m->rpsLosses++;
    if (m->rpsWins >= 3) log_game(m, GAME_NAME[GM_RPS], 1, in->ms);
    else if (m->rpsLosses >= 3) log_game(m, GAME_NAME[GM_RPS], 2, in->ms);
    return CR_NONE;
}

static int update_games(Chat *m, const ChatInput *in) {
    if (m->view == CV_TTT) return update_ttt(m, in);
    if (m->view == CV_RPS) return update_rps(m, in);
    if (in->keys & CK_B) { m->view = CV_THREAD; return CR_NONE; }
    if (in->keys & CK_UP) m->gameSel = (m->gameSel + GM_COUNT - 1) % GM_COUNT;
    if (in->keys & CK_DOWN) m->gameSel = (m->gameSel + 1) % GM_COUNT;
    bool act = (in->keys & CK_A) != 0;
    if (in->down) {
        if (in->y < 38) { if (in->x < 40) m->view = CV_THREAD; return CR_NONE; }
        for (int i = 0; i < GM_COUNT; i++)
            if (inside(in->x, in->y, 6, GM_Y + i * GM_H, CW - 12, GM_H - 4)) { m->gameSel = i; act = true; }
    }
    if (act) start_game(m, m->gameSel);
    return CR_NONE;
}

int chat_update(Chat *m, const ChatInput *in) {
    if (m->pending && in->ms >= m->replyAt) deliver(m);

    if (m->view == CV_EDIT) return update_editor(m, in);
    if (m->view == CV_OPTIONS) return update_options(m, in);
    if (m->view == CV_THEMES) return update_themes(m, in);
    if (m->view == CV_PICK || m->view == CV_HISTORY) return update_grid(m, in);
    if (m->view == CV_EMOJI) return update_emoji(m, in);
    if (m->view == CV_VOICE) return update_voice(m, in);
    if (m->view == CV_GAMES || m->view == CV_TTT || m->view == CV_RPS) return update_games(m, in);

    if (m->view == CV_FRIENDS) {
        if (in->keys & CK_B) {
            if (m->friendsFromList) { chat_open(m); return CR_NONE; }   // NEW CHAT: back to the chat list
            return CR_EXIT;
        }
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
        if (open) { m->view = CV_THREAD; m->threadFrom = CV_FRIENDS; m->scroll = 0; m->note[0] = 0; }
        return CR_NONE;
    }

    if (m->view == CV_LIST) {
        if (in->keys & CK_B) return CR_EXIT;
        int ids[CHAT_THREADS], n = list_threads(m, ids);
        int at = list_index(ids, n, m->sel);
        if (at < 0) at = 0;
        if ((in->keys & CK_UP) && at > 0) at--;
        if ((in->keys & CK_DOWN) && at < n - 1) at++;
        if (in->keys & (CK_UP | CK_DOWN)) { m->sel = ids[at]; list_fit(m); }
        else list_clamp(m);
        if (in->keys & CK_A) { m->sel = ids[at]; m->view = CV_THREAD; m->threadFrom = CV_LIST; m->scroll = 0; return CR_NONE; }
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
            chat_open_friends(m);                                // pick any friend, online or not
            m->friendsFromList = true;
            say(m, "PICK A FRIEND TO CHAT WITH");
        } else if (y < 38) {                                     // top row
            if (x < 36) say(m, "OPEN YOUR PROFILE ON THE CAMERA SCREEN");
            else if (x < 66) say(m, "SEARCH IS NOT IN THIS APP");
            else if (n > LIST_VIS && x >= 159 && x < 196) {          // scroll arrows
                if (x < 178 && m->listScroll > 0) m->listScroll--;
                else if (x >= 178 && m->listScroll < n - LIST_VIS) m->listScroll++;
            }
            else if (x >= 196 && x < 228) say(m, "NO NEW NOTIFICATIONS");
            else if (x >= 230 && x < 264) {                      // ADD FRIENDS: make a friend, it joins the chat list
                int slot = 0;
                while (slot < CHAT_CUSTOM && m->custom[slot].used) slot++;
                if (slot < CHAT_CUSTOM) { open_editor(m, slot); m->addFromList = true; }
                else say(m, "FRIEND SLOTS FULL - DELETE ONE FIRST");
            }
            else if (x >= 266) say(m, "NO MORE OPTIONS YET");
        } else if (y >= 40 && y < 68) {                          // tabs
            if (x >= 96 && x < 198) say(m, "NEAR ME NEEDS ONLINE - NOT IN APP");
            else if (x >= 198) say(m, "CHAT GROUPS NEED ONLINE - NOT IN APP");
        } else if (y >= LIST_Y && y < LIST_Y + LIST_VIS * ROW_H) {
            int r = m->listScroll + (y - LIST_Y) / ROW_H;        // open that chat
            if (r < n) {
                m->sel = ids[r];
                m->view = CV_THREAD;
                m->threadFrom = CV_LIST;
                m->scroll = 0;
            }
        }
        return CR_NONE;
    }

    /* one chat open */
    int back = m->threadFrom == CV_FRIENDS ? CV_FRIENDS : CV_LIST;   // where this chat was opened from
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
    if (y >= IN_Y - 2 && y < IN_Y + IN_H + 2) {                  // the bar: camera, text, voice, emoji, photos, games
        if ((x - 24) * (x - 24) + (y - (IN_Y + 16)) * (y - (IN_Y + 16)) <= 18 * 18) {
            if (m->sel >= CT_ONLINE) say(m, "SNAPS TO ONLINE FRIENDS: NOT YET");
            else return CR_CAMERA;                               // take a new snap for this chat
        }
        else if (x >= 42 && x < 150) return CR_KEYBOARD;
        else if (x >= 150 && x < 194) start_voice(m, in);        // voice note
        else if (x >= 196 && x < 226) { m->view = CV_EMOJI; m->note[0] = 0; }   // emoji picker
        else if (x >= 228 && x < 262) {                          // send a photo from the card
            if (m->sel >= CT_ONLINE) say(m, "PHOTOS TO ONLINE FRIENDS: NOT YET");
            else { m->pickKind = 'I'; m->gridFrom = CV_THREAD; grid_start(m, CV_PICK); }
        }
        else if (x >= 266) open_games(m);                        // games
        return CR_NONE;
    }
    if (m->sel < CT_ONLINE)
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

// Height of one message: text lines, a row of emoji, or a voice note bubble.
static int item_height(const ChatMsg *msg, char lines[][LINE_MAX]) {
    const char *text = msg->text;
    int ids[4], secs, bars[VOICE_BARS], nb;
    char mf[40];
    if (msg->mine && parse_media(text, mf, sizeof(mf))) return 11 + MEDIA_TH + 9;   // only your own snaps show as pictures
    if (parse_emoji(text, ids, 4) > 0) return 11 + 24 + 9;
    if (parse_voice(text, &secs, bars, &nb)) return 11 + 22 + 9;
    return msg_height(wrap(text, MSG_CHARS, lines));
}

// The open chat (or the highlighted one), Snapchat style: coloured name, a bar and the text.
static void draw_thread(uint16_t *c, Chat *m, unsigned long long now) {
    ChatThread *t = &m->th[m->sel];
    bool typing = m->pending && m->sel == m->replyTo;
    char lines[LINES_MAX][LINE_MAX];
    const Theme *th = &THEMES[theme_of(m, m->sel)];
    draw_rect(c, 0, 0, CW, CH, th->bg);

    int total = 0;
    for (int i = 0; i < t->count; i++) total += item_height(&t->msg[i], lines);
    if (typing) total += msg_height(1);

    int top = HEAD_H + 4, bottom = CH - 6, view = bottom - top;
    m->maxScroll = total > view ? total - view : 0;
    if (m->scroll > m->maxScroll) m->scroll = m->maxScroll;
    if (m->scroll < 0) m->scroll = 0;

    int y = bottom + m->scroll - total;                          // newest message sits at the bottom
    for (int i = 0; i < t->count; i++) {
        int h = item_height(&t->msg[i], lines);
        if (y + h > top && y < CH) {
            uint32_t col = t->msg[i].mine ? th->mine : th->theirs;
            int ids[4], secs, bars[VOICE_BARS], nb, ne = parse_emoji(t->msg[i].text, ids, 4);
            bool voice = !ne && parse_voice(t->msg[i].text, &secs, bars, &nb);
            char mf[40], mk = t->msg[i].mine ? parse_media(t->msg[i].text, mf, sizeof(mf)) : 0;
            draw_rect(c, 8, y, 2, h - 12, col);
            bold(c, MSG_X, y, 1, t->msg[i].mine ? "ME" : name_of(m, m->sel), col);
            if (mk) {
                draw_media(c, m, MSG_X + 2, y + 13, mk, mf);
            } else if (ne) {
                for (int k = 0; k < ne; k++) draw_emoji(c, ids[k], MSG_X + 11 + k * 26, y + 11 + 12, 11);
            } else if (voice) {
                draw_voice_bubble(c, MSG_X, y + 11, secs, bars, nb, col);
            } else {
                int n = wrap(t->msg[i].text, MSG_CHARS, lines);
                for (int k = 0; k < n; k++) txt(c, MSG_X, y + 11 + k * 10, 1, lines[k], WHITE);
            }
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
    if (m->sel >= CT_ONLINE) {
        char who[40];
        snprintf(who, sizeof(who), "@%s", m->online[m->sel - CT_ONLINE].user);
        txt(c, CW - 8 - text_width(who, 1), 3, 1, who, 0x5CE08C);
    }
    if (m->scroll > 0) txt(c, CW - 8 - text_width("UP", 1), 14, 1, "UP", GREY);
    int sn = (m->sel >= CT_FRIEND && m->sel < CT_ONLINE) ? streak_now(t, g_today) : 0;
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

    // chats: My AI, Team Snapchat, then every friend you made
    int ids[CHAT_THREADS], n = list_threads(m, ids);
    list_clamp(m);
    if (n > LIST_VIS) {                                          // scroll arrows
        bool up = m->listScroll > 0, dn = m->listScroll < n - LIST_VIS;
        disc(c, 168, 19, 9, CIRCLE);
        draw_line(c, 164, 21, 168, 16, 2.2f, up ? WHITE : 0x6E6E74);
        draw_line(c, 168, 16, 172, 21, 2.2f, up ? WHITE : 0x6E6E74);
        disc(c, 187, 19, 9, CIRCLE);
        draw_line(c, 183, 17, 187, 22, 2.2f, dn ? WHITE : 0x6E6E74);
        draw_line(c, 187, 22, 191, 17, 2.2f, dn ? WHITE : 0x6E6E74);
    }
    for (int v = 0; v < LIST_VIS && m->listScroll + v < n; v++) {
        int i = ids[m->listScroll + v];
        int y0 = LIST_Y + v * ROW_H;
        uint32_t bg = i == m->sel ? SELBG : BG;
        draw_rect(c, 0, y0, CW, ROW_H, bg);
        avatar(c, m, i, 26, (float)y0 + 24, 17);
        bold(c, 52, y0 + 9, 2, name_of(m, i), WHITE);

        ChatThread *t = &m->th[i];
        bool sent = t->count > 0 && t->msg[t->count - 1].mine;
        if (sent) sent_icon(c, 52, y0 + 28);
        else received_icon(c, 52, y0 + 28, bg);
        char sub[32];
        if (i >= CT_ONLINE)
            snprintf(sub, sizeof(sub), "%s - ONLINE", t->count == 0 ? "SAY HI!" : sent ? "SENT" : "RECEIVED");
        else
            snprintf(sub, sizeof(sub), "%s - %s", sent ? "SENT" : "RECEIVED", (t->touched || i >= CT_CUSTOM) ? "NOW" : "2MO");
        txt(c, 69, y0 + 29, 1, sub, GREY);

        if (i == CT_AI) { robot(c, 252, y0 + 24); scan_icon(c, 292, y0 + 24, bg); }
        else camera_icon(c, v == LIST_VIS - 1 ? 248 : 292, y0 + 24, bg);   // the New Chat button covers the last row's right edge
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
    const char *title = m->friendsFromList ? (m->friendPage ? "NEW CHAT: MINE" : "NEW CHAT")
                                           : (m->friendPage ? "MY FRIENDS" : "FRIENDS");
    bold(c, (CW - text_width(title, 2)) / 2, 8, 2, title, WHITE);
    pill_text(c, 250, 5, 64, 19, m->friendPage ? "< BACK" : "MINE >", YELLOW, BLACK);
    const char *sub = m->friendsFromList ? "TAP A FRIEND TO START CHATTING" : "AI CHARACTERS - NOT REAL PEOPLE";
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
// The Snapchat message bar: camera, "Send a chat", voice, emoji, photos, games.
static void input_bar(uint16_t *c, uint32_t bg) {
    const int cy = IN_Y + 16;
    const uint32_t line = 0xE6E6EA, hint = 0x7A7A80;
    disc(c, 24, cy, 16, WHITE);                                  // camera button
    rrect(c, 14, cy - 7, 20, 15, 3, BLACK);
    rrect(c, 19, cy - 10, 10, 5, 2, BLACK);
    disc(c, 24, cy + 1, 4.6f, WHITE);
    disc(c, 24, cy + 1, 2.6f, BLACK);

    rring(c, 44, IN_Y, 148, IN_H, 16, 1, 0x8A8A90, bg);          // the text pill
    txt(c, 58, IN_Y + 12, 1, "SEND A CHAT", hint);
    static const int bar_h[5] = { 7, 15, 21, 13, 6 };            // voice sound wave
    for (int i = 0; i < 5; i++)
        draw_rect(c, 160 + i * 5, cy - bar_h[i] / 2, 2, bar_h[i], line);

    disc(c, 210, cy, 12.5f, line);                               // emoji
    disc(c, 210, cy, 10.5f, bg);
    disc(c, 205.5f, cy - 3, 1.7f, line);
    disc(c, 214.5f, cy - 3, 1.7f, line);
    draw_line(c, 204.5f, cy + 2.5f, 207.5f, cy + 6, 1.8f, line);
    draw_line(c, 207.5f, cy + 6, 212.5f, cy + 6, 1.8f, line);
    draw_line(c, 212.5f, cy + 6, 215.5f, cy + 2.5f, 1.8f, line);

    rring(c, 235, cy - 12, 20, 24, 4, 2, line, bg);              // photos / memories
    draw_line(c, 238, cy + 7, 243, cy + 1, 1.8f, line);
    draw_line(c, 243, cy + 1, 247, cy + 5, 1.8f, line);
    draw_line(c, 247, cy + 5, 251, cy - 1, 1.8f, line);
    disc(c, 247.5f, cy - 6, 2, line);

    rring(c, 270, cy - 9, 28, 18, 9, 2, line, bg);               // games
    draw_rect(c, 278, cy - 1, 8, 2, line);
    draw_rect(c, 281, cy - 4, 2, 8, line);
    disc(c, 290, cy - 2, 1.8f, line);
    disc(c, 293, cy + 2, 1.8f, line);
}

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
        if (m->sel >= CT_ONLINE) {
            char who[40];
            snprintf(who, sizeof(who), "@%s", m->online[m->sel - CT_ONLINE].user);
            const char *a = "ONLINE CHAT - A REAL FRIEND", *b = "THEY SEE YOUR MESSAGES WHEN THEY OPEN CHAT";
            txt(c, (CW - text_width(who, 2)) / 2, 62, 2, who, 0x5CE08C);
            txt(c, (CW - text_width(a, 1)) / 2, 92, 1, a, WHITE);
            txt(c, (CW - text_width(b, 1)) / 2, 108, 1, b, GREY);
            if (m->outCount > 0) {
                char q[32];
                snprintf(q, sizeof(q), "SENDING %d...", m->outCount);
                txt(c, (CW - text_width(q, 1)) / 2, 130, 1, q, YELLOW);
            }
        } else {
            bold(c, 8, 47, 1, "QUICK CHATS", GREY);
            for (int i = 0; i < 6; i++) {
                rrect(c, chip_x(i), chip_y(i), 148, 30, 15, CIRCLE);
                txt(c, chip_x(i) + (148 - text_width(CHIPS[i], 1)) / 2, chip_y(i) + 11, 1, CHIPS[i], WHITE);
            }
        }
        input_bar(c, THEMES[theme_of(m, m->sel)].bg);
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
    if (m->view == CV_PICK) snprintf(label, sizeof(label), "SEND %s TO %s - %s", m->pickKind == 'I' ? "PHOTO" : "SNAP", name_of(m, m->sel), when);
    else snprintf(label, sizeof(label), "SENT %s", when);
    draw_rect(c, 6, 6, text_width(label, 1) + 12, 15, BLACK);
    txt(c, 12, 10, 1, label, WHITE);
}

static void draw_grid(uint16_t *c, Chat *m, unsigned long long now) {
    bool pick = m->view == CV_PICK;
    sub_header(c, m, pick ? (m->pickKind == 'I' ? "SEND A PHOTO" : "SEND A SNAP") : "PHOTO HISTORY");
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
        pill_text(c, 90, 176, 140, 24, m->pickKind == 'I' ? "SEND PHOTO" : "SEND SNAP", YELLOW, BLACK);
        txt(c, 8, 218, 1, "A: SEND   D-PAD OR TAP: CHOOSE   B: BACK", 0x6E6E74);
    } else {
        txt(c, 8, 218, 1, "D-PAD OR TAP: LOOK   B: BACK", 0x6E6E74);
    }
    draw_note(c, m, now, 200);
}

/* ---------------- drawing: emoji, voice note, games ---------------- */

static void draw_emoji_picker(uint16_t *c, Chat *m, unsigned long long now) {
    sub_header(c, m, "EMOJI");
    for (int i = 0; i < EMOJI_COUNT; i++) {
        bool lit = i == m->emojiSel;
        rrect(c, ex(i), ey(i), 72, 48, 10, lit ? YELLOW : 0x2E2E34);
        rrect(c, ex(i) + 2, ey(i) + 2, 68, 44, 8, lit ? 0x38383F : CIRCLE);
        draw_emoji(c, i, (float)ex(i) + 36, (float)ey(i) + 24, 17);
    }
    txt(c, 8, 218, 1, "A OR TAP: SEND   D-PAD: LOOK   B: BACK", 0x6E6E74);
    draw_note(c, m, now, 200);
}

static void draw_voice(uint16_t *c, Chat *m, unsigned long long now) {
    sub_header(c, m, "VOICE NOTE");
    char t[16];
    snprintf(t, sizeof(t), "0:%02d", m->vCount / 4);
    bool blink = m->vRec && (now / 400) % 2 == 0;
    disc(c, 112, 62, 7, m->vRec ? (blink ? RED : 0x7A2030) : 0x5A5A60);
    bold(c, 128, 52, 3, t, WHITE);
    txt(c, (CW - text_width(m->vRec ? "RECORDING - SPEAK NOW" : "STOPPED - SEND IT OR CANCEL", 1)) / 2, 82, 1,
        m->vRec ? "RECORDING - SPEAK NOW" : "STOPPED - SEND IT OR CANCEL", GREY);
    const Theme *th = &THEMES[theme_of(m, m->sel)];
    for (int i = 0; i < VOICE_BARS; i++) {                       // empty slots are thin grey ticks
        int x = 16 + i * 9;
        if (i < m->vCount) {
            int h = 4 + m->vBars[i] * 6;
            draw_rect(c, x, 130 - h / 2, 5, h, th->mine);
        } else {
            draw_rect(c, x, 129, 5, 2, 0x3A3A40);
        }
    }
    pill_text(c, 90, 176, 140, 24, "SEND VOICE NOTE", YELLOW, BLACK);
    txt(c, 8, 218, 1, "A: SEND   B: CANCEL   UP TO 8 SECONDS", 0x6E6E74);
    draw_note(c, m, now, 150);
}

static void draw_games_menu(uint16_t *c, Chat *m, unsigned long long now) {
    sub_header(c, m, "GAMES");
    for (int i = 0; i < GM_COUNT; i++) {
        int y = GM_Y + i * GM_H;
        bool lit = i == m->gameSel;
        rrect(c, 6, y, CW - 12, GM_H - 4, 12, lit ? 0x38383F : CIRCLE);
        bold(c, 18, y + 9, 2, GAME_NAME[i], lit ? YELLOW : WHITE);
        txt(c, 18, y + 28, 1, GAME_INFO[i], GREY);
    }
    char who[40];
    snprintf(who, sizeof(who), "YOU PLAY AGAINST %s", name_of(m, m->sel));
    txt(c, (CW - text_width(who, 1)) / 2, 152, 1, who, GREY);
    txt(c, 8, 218, 1, "A OR TAP: PLAY   D-PAD: CHOOSE   B: BACK", 0x6E6E74);
    draw_note(c, m, now, 170);
}

static void draw_ttt(uint16_t *c, Chat *m, unsigned long long now) {
    sub_header(c, m, "TIC TAC TOE");
    (void)now;
    draw_rect(c, TTT_X + TTT_CELL - 1, TTT_Y, 2, TTT_CELL * 3, 0x55555C);
    draw_rect(c, TTT_X + TTT_CELL * 2 - 1, TTT_Y, 2, TTT_CELL * 3, 0x55555C);
    draw_rect(c, TTT_X, TTT_Y + TTT_CELL - 1, TTT_CELL * 3, 2, 0x55555C);
    draw_rect(c, TTT_X, TTT_Y + TTT_CELL * 2 - 1, TTT_CELL * 3, 2, 0x55555C);
    const Theme *th = &THEMES[theme_of(m, m->sel)];
    for (int i = 0; i < 9; i++) {
        int x = TTT_X + (i % 3) * TTT_CELL, y = TTT_Y + (i / 3) * TTT_CELL;
        float cx = x + TTT_CELL / 2.0f, cy = y + TTT_CELL / 2.0f;
        if (i == m->gameCur && !m->tttState) {
            draw_rect(c, x + 3, y + 3, TTT_CELL - 6, 2, YELLOW);
            draw_rect(c, x + 3, y + TTT_CELL - 5, TTT_CELL - 6, 2, YELLOW);
            draw_rect(c, x + 3, y + 3, 2, TTT_CELL - 6, YELLOW);
            draw_rect(c, x + TTT_CELL - 5, y + 3, 2, TTT_CELL - 6, YELLOW);
        }
        if (m->ttt[i] == 1) {                                    // X = you
            draw_line(c, cx - 13, cy - 13, cx + 13, cy + 13, 5, th->mine);
            draw_line(c, cx + 13, cy - 13, cx - 13, cy + 13, 5, th->mine);
        } else if (m->ttt[i] == 2) {                             // O = the friend
            disc(c, cx, cy, 15, th->theirs);
            disc(c, cx, cy, 10, th->bg);
        }
    }
    char st[48];
    if (m->tttState == 1) snprintf(st, sizeof(st), "YOU WIN!");
    else if (m->tttState == 2) snprintf(st, sizeof(st), "%.10s WINS!", name_of(m, m->sel));
    else if (m->tttState == 3) snprintf(st, sizeof(st), "IT IS A DRAW");
    else snprintf(st, sizeof(st), "YOUR TURN - YOU ARE X");
    if (m->tttState) pill_text(c, 90, 198, 140, 18, "PLAY AGAIN", YELLOW, BLACK);
    bold(c, (CW - text_width(st, 1)) / 2, 191, 1, st, m->tttState == 1 ? YELLOW : WHITE);
    txt(c, 8, 218, 1, "A OR TAP: PLACE   D-PAD: MOVE   B: BACK", 0x6E6E74);
}

// Rock, paper or scissors, drawn with shapes.
static void draw_throw(uint16_t *c, int kind, float cx, float cy, float r) {
    if (kind == 0) {
        disc(c, cx, cy, 0.78f * r, 0x9A9AA0);
        disc(c, cx - 0.25f * r, cy - 0.25f * r, 0.28f * r, 0xC8C8CE);
    } else if (kind == 1) {
        draw_rect(c, (int)(cx - 0.6f * r), (int)(cy - 0.8f * r), (int)(1.2f * r), (int)(1.6f * r), WHITE);
        for (int i = 0; i < 3; i++)
            draw_rect(c, (int)(cx - 0.4f * r), (int)(cy - 0.4f * r + i * 0.4f * r), (int)(0.8f * r), 2, 0xB4B4BC);
    } else {
        draw_line(c, cx - 0.55f * r, cy - 0.8f * r, cx + 0.3f * r, cy + 0.35f * r, 0.22f * r, 0xE6E6EA);
        draw_line(c, cx + 0.55f * r, cy - 0.8f * r, cx - 0.3f * r, cy + 0.35f * r, 0.22f * r, 0xE6E6EA);
        disc(c, cx - 0.4f * r, cy + 0.6f * r, 0.3f * r, RED);
        disc(c, cx + 0.4f * r, cy + 0.6f * r, 0.3f * r, RED);
        disc(c, cx - 0.4f * r, cy + 0.6f * r, 0.13f * r, CIRCLE);
        disc(c, cx + 0.4f * r, cy + 0.6f * r, 0.13f * r, CIRCLE);
    }
}

static void draw_rps(uint16_t *c, Chat *m, unsigned long long now) {
    sub_header(c, m, "ROCK PAPER SCISSORS");
    (void)now;
    bool over = m->rpsWins >= 3 || m->rpsLosses >= 3;
    char line[64];
    snprintf(line, sizeof(line), "FIRST TO 3 -  YOU %d : %d %.10s", m->rpsWins, m->rpsLosses, name_of(m, m->sel));
    txt(c, (CW - text_width(line, 1)) / 2, 50, 1, line, GREY);
    for (int i = 0; i < 3; i++) {
        int x = 12 + i * 100;
        bool lit = i == m->gameCur && !over;
        rrect(c, x, 76, 96, 70, 12, lit ? YELLOW : 0x2E2E34);
        rrect(c, x + 2, 78, 92, 66, 10, lit ? 0x38383F : CIRCLE);
        draw_throw(c, i, (float)x + 48, 104, 22);
        txt(c, x + (96 - text_width(THROW_NAME[i], 1)) / 2, 130, 1, THROW_NAME[i], WHITE);
    }
    if (m->rpsMine >= 0) {
        snprintf(line, sizeof(line), "YOU: %s   %.10s: %s", THROW_NAME[m->rpsMine], name_of(m, m->sel), THROW_NAME[m->rpsTheirs]);
        txt(c, (CW - text_width(line, 1)) / 2, 156, 1, line, WHITE);
        int d = (m->rpsMine - m->rpsTheirs + 3) % 3;
        const char *r = d == 0 ? "TIE - GO AGAIN" : d == 1 ? "YOU WIN THIS ROUND" : "YOU LOSE THIS ROUND";
        bold(c, (CW - text_width(r, 1)) / 2, 170, 1, r, d == 1 ? YELLOW : WHITE);
    } else {
        txt(c, (CW - text_width("PICK ROCK, PAPER OR SCISSORS", 1)) / 2, 160, 1, "PICK ROCK, PAPER OR SCISSORS", GREY);
    }
    if (over) {
        pill_text(c, 90, 192, 140, 22, m->rpsWins >= 3 ? "YOU WON - PLAY AGAIN" : "PLAY AGAIN", YELLOW, BLACK);
    }
    txt(c, 8, 218, 1, "A OR TAP: THROW   D-PAD: CHOOSE   B: BACK", 0x6E6E74);
}

void chat_draw(Chat *m, uint16_t *top, uint16_t *bottom, unsigned long long nowMs) {
    g_today = local_day();
    g_mediaLoaded = false;
    if (m->view == CV_EDIT) { draw_editor(top, bottom, m, nowMs); return; }
    if (m->view == CV_PICK || m->view == CV_HISTORY) {
        draw_grid_top(top, m);
        draw_grid(bottom, m, nowMs);
        return;
    }
    draw_thread(top, m, nowMs);
    if (m->view == CV_EMOJI) { draw_emoji_picker(bottom, m, nowMs); return; }
    if (m->view == CV_VOICE) { draw_voice(bottom, m, nowMs); return; }
    if (m->view == CV_GAMES) { draw_games_menu(bottom, m, nowMs); return; }
    if (m->view == CV_TTT) { draw_ttt(bottom, m, nowMs); return; }
    if (m->view == CV_RPS) { draw_rps(bottom, m, nowMs); return; }
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

/* ---------------- online chat ---------------- */

void chat_note(Chat *m, const char *text) { say(m, text); }

void chat_set_online(Chat *m, const OnlineEntry *list, int n) {
    bool changed = false;
    for (int k = 0; k < CHAT_ONLINE; k++) {              // friends that are gone (removed on the server)
        if (!m->online[k].used) continue;
        bool still = false;
        for (int i = 0; i < n; i++)
            if (strcasecmp(list[i].user, m->online[k].user) == 0) { still = true; break; }
        if (still) continue;
        int thread = CT_ONLINE + k;
        if (m->sel == thread) { m->sel = CT_AI; if (m->view != CV_LIST) m->view = CV_LIST; }
        for (int i = 0; i < m->outCount; )               // nothing to send to them any more
            if (strcasecmp(m->out[i].user, m->online[k].user) == 0) {
                memmove(&m->out[i], &m->out[i + 1], sizeof(OutMsg) * (size_t)(m->outCount - i - 1));
                m->outCount--;
            } else i++;
        memset(&m->th[thread], 0, sizeof(m->th[thread]));
        memset(&m->online[k], 0, sizeof(m->online[k]));
        changed = true;
    }
    for (int i = 0; i < n; i++) {                        // new friends, and names that changed
        int slot = -1, freeSlot = -1;
        for (int k = 0; k < CHAT_ONLINE; k++) {
            if (m->online[k].used && strcasecmp(m->online[k].user, list[i].user) == 0) { slot = k; break; }
            if (!m->online[k].used && freeSlot < 0) freeSlot = k;
        }
        if (slot < 0) slot = freeSlot;
        if (slot < 0) continue;                          // all slots are taken
        char name[ONLINE_NAME + 1], user[ONLINE_USER + 1];
        clean_word(list[i].name, name, ONLINE_NAME);
        clean_word(list[i].user, user, ONLINE_USER);
        if (!user[0]) continue;
        if (!name[0]) snprintf(name, sizeof(name), "%.*s", ONLINE_NAME, user);
        OnlineFriend *o = &m->online[slot];
        if (!o->used || strcmp(o->user, user) != 0 || strcmp(o->name, name) != 0) changed = true;
        o->used = true;
        snprintf(o->user, sizeof(o->user), "%s", user);
        snprintf(o->name, sizeof(o->name), "%s", name);
    }
    list_clamp(m);
    if (changed) m->saveFailed = !chat_save(m);
}

void chat_receive(Chat *m, long long id, const char *user, const char *text) {
    if (id <= m->lastId) return;                         // seen already
    m->lastId = id;
    int slot = -1;
    for (int k = 0; k < CHAT_ONLINE; k++)
        if (m->online[k].used && strcasecmp(m->online[k].user, user) == 0) { slot = k; break; }
    if (slot >= 0) {
        char clean[CHAT_TEXT];
        size_t n = 0;
        for (; text[n] && n < sizeof(clean) - 1; n++) {
            unsigned char ch = (unsigned char)text[n];
            clean[n] = (ch >= 32 && ch < 127) ? (char)toupper(ch) : ' ';
        }
        clean[n] = 0;
        if (clean[0]) {
            int thread = CT_ONLINE + slot;
            add_msg(&m->th[thread], false, clean);
            m->th[thread].touched = true;
            if (m->view == CV_THREAD && m->sel == thread) m->scroll = 0;
            else {
                char note[44];
                snprintf(note, sizeof(note), "NEW CHAT FROM %.20s", m->online[slot].name);
                say(m, note);
            }
        }
    }
    m->saveFailed = !chat_save(m);
}

bool chat_outbox_peek(const Chat *m, char *user, size_t ulen, char *text, size_t tlen) {
    if (m->outCount <= 0) return false;
    snprintf(user, ulen, "%s", m->out[0].user);
    snprintf(text, tlen, "%s", m->out[0].text);
    return true;
}

void chat_outbox_pop(Chat *m) {
    if (m->outCount <= 0) return;
    memmove(&m->out[0], &m->out[1], sizeof(OutMsg) * (size_t)(m->outCount - 1));
    m->outCount--;
}
