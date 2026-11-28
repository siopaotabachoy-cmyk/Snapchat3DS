// chat.c - the Chat screen, made to look like Snapchat's.
// The chats are FAKE. "My AI" picks an answer from a built-in list, "Team Snapchat"
// only has a welcome message. No 3DS code in here, only drawing with art.h.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chat.h"
#include "extra.h"

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

static const char *const NAMES[CHAT_THREADS] = { "MY AI", "TEAM SNAPCHAT" };

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

static void avatar(uint16_t *c, int who, float x, float y, float r) {
    if (who == CT_AI) avatar_ai(c, x, y, r);
    else avatar_ghost(c, x, y, r);
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

static void nav_bar(uint16_t *c) {
    draw_rect(c, 0, NAV_Y, CW, CH - NAV_Y, NAVBG);
    draw_rect(c, 0, NAV_Y, CW, 1, LINEC);
    const uint32_t k = 0xE6E6E6;
    // map pin
    disc(c, 32, 224, 6, k); disc(c, 32, 224, 2.6f, NAVBG);
    draw_line(c, 27.5f, 227, 32, 236, 2.2f, k);
    draw_line(c, 36.5f, 227, 32, 236, 2.2f, k);
    // chat (the one we are on, filled)
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

static bool has(const char *s, const char *part) { return strstr(s, part) != NULL; }

// True if the word is in the text on its own (so HI does not match THIS).
static bool word(const char *s, const char *w) {
    size_t n = strlen(w);
    for (const char *p = s; (p = strstr(p, w)) != NULL; p++) {
        bool left = p == s || !((p[-1] >= 'A' && p[-1] <= 'Z') || (p[-1] >= '0' && p[-1] <= '9'));
        char after = p[n];
        bool right = !((after >= 'A' && after <= 'Z') || (after >= '0' && after <= '9'));
        if (left && right) return true;
    }
    return false;
}

static const char *const JOKES[] = {
    "WHY DID THE CAMERA GO TO SCHOOL? TO GET A BETTER PICTURE!",
    "WHAT DO YOU CALL A SLEEPING DINOSAUR? A DINO-SNORE!",
    "WHY DID THE PHONE WEAR GLASSES? IT LOST ITS CONTACTS!",
    "WHAT IS A GHOST'S FAVORITE APP? SNAPCHAT, OF COURSE!",
    "WHY DON'T EGGS TELL JOKES? THEY MIGHT CRACK UP!",
    "WHAT DID THE 3DS SAY TO THE GAME? YOU ADD A NEW DIMENSION!",
};
static const char *const FALLBACKS[] = {
    "HMM, TELL ME MORE!",
    "THAT IS INTERESTING! WHAT ELSE IS NEW?",
    "HA! GOOD ONE.",
    "I AM ONLY A PRETEND AI ON A 3DS, SO I MAY MISS THAT ONE. TRY ASKING FOR A JOKE OR A FILTER!",
    "COOL! WANT ME TO PICK A FILTER FOR YOU?",
    "OH WOW! SNAP A PIC OF IT!",
    "I AM ALL EARS. WELL, I AM ALL PIXELS.",
};
#define N_JOKES ((int)(sizeof(JOKES) / sizeof(JOKES[0])))
#define N_FALLBACKS ((int)(sizeof(FALLBACKS) / sizeof(FALLBACKS[0])))

// Picks My AI's answer for what you wrote.
static void make_reply(Chat *m, const char *said, char *out, size_t len) {
    char u[CHAT_TEXT];
    size_t n = 0;
    for (; said[n] && n < sizeof(u) - 1; n++) {
        char ch = said[n];
        u[n] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch;
    }
    u[n] = 0;

    if (has(u, "JOKE") || has(u, "FUNNY")) {
        snprintf(out, len, "%s", JOKES[m->jokeNo++ % N_JOKES]);
    } else if (has(u, "FILTER") || has(u, "LENS")) {
        snprintf(out, len, "TRY THE %s FILTER! IT LOOKS GREAT ON YOU.",
                 filter_name_any(rand() % ALL_FILTERS));
    } else if (has(u, "HOW ARE YOU") || has(u, "HOW R U") || has(u, "WHATS UP") || has(u, "WHAT'S UP")) {
        snprintf(out, len, "I AM GREAT, THANKS FOR ASKING! HOW ARE YOU?");
    } else if (has(u, "LOVE")) {
        snprintf(out, len, "AWW, THAT IS SWEET! I AM JUST A PRETEND AI, BUT I LIKE CHATTING WITH YOU!");
    } else if (has(u, "NIGHT") || word(u, "BYE") || has(u, "SLEEP")) {
        snprintf(out, len, "GOOD NIGHT! SWEET DREAMS. SEE YOU LATER!");
    } else if (has(u, "THANK")) {
        snprintf(out, len, "YOU ARE WELCOME!");
    } else if (has(u, "WHO ARE YOU") || has(u, "YOUR NAME")) {
        snprintf(out, len, "I AM MY AI, YOUR PRETEND CHAT FRIEND ON THE 3DS.");
    } else if (has(u, "SNAP") || has(u, "PHOTO") || has(u, "PICTURE") || has(u, "SELFIE") || has(u, "BOOMERANG")) {
        snprintf(out, len, "TIP: PRESS X OR TAP THE BIG CIRCLE TO TAKE A SNAP. HOLD THE CIRCLE FOR A BOOMERANG!");
    } else if (word(u, "HELP")) {
        snprintf(out, len, "I CAN TELL JOKES, PICK A FILTER, OR JUST CHAT. TRY: TELL ME A JOKE");
    } else if (word(u, "HI") || word(u, "HEY") || word(u, "HELLO") || word(u, "YO") || word(u, "HOWDY")) {
        snprintf(out, len, "HEY! GOOD TO SEE YOU. WHAT ARE WE SNAPPING TODAY?");
    } else {
        snprintf(out, len, "%s", FALLBACKS[m->fallbackNo++ % N_FALLBACKS]);
    }
}

/* ---------------- open, update, send ---------------- */

static void say(Chat *m, const char *text) {
    snprintf(m->note, sizeof(m->note), "%s", text);
    m->noteUntil = (unsigned long long)-1;       // chat_draw starts the clock
}

void chat_open(Chat *m) {
    if (!m->started) {
        m->started = true;
        add_msg(&m->th[CT_AI], false, "HEY! I AM MY AI. ASK ME ANYTHING, OR TRY ONE OF THE QUICK CHATS.");
        add_msg(&m->th[CT_TEAM], false, "WELCOME TO SNAP3DS!");
        add_msg(&m->th[CT_TEAM], false, "TAP THE BIG CIRCLE TO TAKE A SNAP. SWIPE THE CIRCLES TO TRY FILTERS.");
    }
    m->view = CV_LIST;
    m->scroll = 0;
    m->note[0] = 0;
}

static void deliver(Chat *m) {
    if (!m->pending) return;
    add_msg(&m->th[CT_AI], false, m->reply);
    m->pending = false;
    m->scroll = 0;
}

void chat_send(Chat *m, const char *text, unsigned long long nowMs) {
    if (m->view != CV_THREAD || m->sel != CT_AI) return;
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
    add_msg(&m->th[CT_AI], true, p);
    m->th[CT_AI].touched = true;
    make_reply(m, p, m->reply, sizeof(m->reply));
    m->pending = true;
    m->replyAt = nowMs + 800 + (unsigned long long)(rand() % 900);
    m->scroll = 0;
}

// Quick chats shown on the bottom screen while My AI is open.
static const char *const CHIPS[6] = {
    "HI!", "TELL ME A JOKE", "BEST FILTER?", "HOW ARE YOU?", "TAKE A SNAP TIPS", "GOOD NIGHT"
};
static int chip_x(int i) { return 8 + (i % 2) * 156; }
static int chip_y(int i) { return 62 + (i / 2) * 36; }

int chat_update(Chat *m, const ChatInput *in) {
    if (m->pending && in->ms >= m->replyAt) deliver(m);

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
            if (slot == 0) say(m, "MAP IS NOT IN THIS APP");
            else if (slot == 3) say(m, "FRIENDS NEED ONLINE - NOT IN APP");
            else if (slot == 4) say(m, "STORIES ARE NOT IN THIS APP");
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
        } else if (y >= LIST_Y && y < LIST_Y + CHAT_THREADS * ROW_H) {
            m->sel = (y - LIST_Y) / ROW_H;                       // open that chat
            m->view = CV_THREAD;
            m->scroll = 0;
        }
        return CR_NONE;
    }

    /* one chat open */
    if (in->keys & CK_B) { m->view = CV_LIST; return CR_NONE; }
    if (in->keys & CK_UP) m->scroll += 24;
    if (in->keys & CK_DOWN) m->scroll -= 24;
    if (m->scroll > m->maxScroll) m->scroll = m->maxScroll;
    if (m->scroll < 0) m->scroll = 0;
    if ((in->keys & CK_A) && m->sel == CT_AI) return CR_KEYBOARD;
    if (!in->down) return CR_NONE;

    int x = in->x, y = in->y;
    if (y < 38) {
        if (x < 40) m->view = CV_LIST;                           // back
        else if (x >= 266) say(m, "NO MORE OPTIONS YET");
        return CR_NONE;
    }
    if (m->sel != CT_AI) return CR_NONE;                         // Team Snapchat cannot get replies
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
    bool typing = m->pending && m->sel == CT_AI;
    char lines[LINES_MAX][LINE_MAX];
    draw_rect(c, 0, 0, CW, CH, BG);

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
            uint32_t col = t->msg[i].mine ? RED : BLUE;
            draw_rect(c, 8, y, 2, 11 + n * 10 - 3, col);
            bold(c, MSG_X, y, 1, t->msg[i].mine ? "ME" : NAMES[m->sel], col);
            for (int k = 0; k < n; k++) txt(c, MSG_X, y + 11 + k * 10, 1, lines[k], WHITE);
        }
        y += h;
    }
    if (typing) {
        const char *dots[3] = { ".", ". .", ". . ." };
        draw_rect(c, 8, y, 2, 18, BLUE);
        bold(c, MSG_X, y, 1, NAMES[CT_AI], BLUE);
        txt(c, MSG_X, y + 11, 1, dots[(now / 300) % 3], GREY);
    }

    draw_rect(c, 0, 0, CW, HEAD_H, BAR);                         // header covers scrolled text
    draw_rect(c, 0, HEAD_H, CW, 1, LINEC);
    avatar(c, m->sel, 22, 15, 11);
    bold(c, 42, 8, 2, NAMES[m->sel], WHITE);
    if (m->scroll > 0) txt(c, CW - 8 - text_width("UP", 1), 12, 1, "UP", GREY);
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
    for (int i = 0; i < CHAT_THREADS; i++) {
        int y0 = LIST_Y + i * ROW_H;
        uint32_t bg = i == m->sel ? SELBG : BG;
        draw_rect(c, 0, y0, CW, ROW_H, bg);
        avatar(c, i, 26, (float)y0 + 24, 17);
        bold(c, 52, y0 + 9, 2, NAMES[i], WHITE);

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

    nav_bar(c);
    draw_note(c, m, now, 172);
}

// The bottom screen while a chat is open: back, quick chats and the "send a chat" bar.
static void draw_thread_controls(uint16_t *c, Chat *m, unsigned long long now) {
    draw_rect(c, 0, 0, CW, CH, BG);
    draw_line(c, 21, 10, 15, 18, 2.8f, WHITE);                   // back
    draw_line(c, 15, 18, 21, 26, 2.8f, WHITE);
    avatar(c, m->sel, 56, 18, 13);
    bold(c, 76, 11, 2, NAMES[m->sel], WHITE);
    disc(c, 296, 18, 13, CIRCLE);
    dots3(c, 296, 18);
    draw_rect(c, 0, 37, CW, 1, LINEC);

    if (m->sel == CT_AI) {
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
        txt(c, 8, 218, 1, "A: TYPE   B: BACK   D-PAD: SCROLL CHAT", 0x6E6E74);
    } else {
        const char *a = "TEAM SNAPCHAT SENDS UPDATES", *b = "YOU CANT REPLY TO THIS CHAT";
        txt(c, (CW - text_width(a, 1)) / 2, 90, 1, a, WHITE);
        txt(c, (CW - text_width(b, 1)) / 2, 106, 1, b, GREY);
        rrect(c, 8, IN_Y, 304, IN_H, 16, 0x1C1C1F);
        txt(c, 26, IN_Y + 12, 1, "CANT REPLY", 0x55555A);
        txt(c, 8, 218, 1, "B: BACK   D-PAD: SCROLL CHAT", 0x6E6E74);
    }
    draw_note(c, m, now, 144);
}

void chat_draw(Chat *m, uint16_t *top, uint16_t *bottom, unsigned long long nowMs) {
    draw_thread(top, m, nowMs);
    if (m->view == CV_THREAD) draw_thread_controls(bottom, m, nowMs);
    else draw_list(bottom, m, nowMs);
}
