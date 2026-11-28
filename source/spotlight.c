// spotlight.c - the Spotlight screen. See spotlight.h.
//
// Top screen: the video, with the creator, caption and the heart / comment /
// share buttons drawn on it like Snapchat's Spotlight.
// Bottom screen: previous, like, next. A or D-pad right = next video,
// D-pad left = previous, X = like, B = back to the camera.
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "spotlight.h"

#define BG     0x111113
#define CARD   0x232327
#define GREY   0x9A9AA0
#define WHITE  0xFFFFFF
#define BLACK  0x000000
#define YELLOW 0xFFFC00
#define RED    0xFF2D55
#define PI_F   3.14159265f
#define LOOP_MS 6000                     // length of one video

static const char *const CREATORS[] = {
    "JACOB.H", "SOPHIE_R", "MIKA.PLAYS", "DANIEL_K", "LUNA.LOVES", "THE.REAL.TOBY",
    "ANYA_X", "CHEF.MARCO", "PIXEL.PAT", "NOVA_J", "BIG.RAY", "KIKI.CREATES",
    "SKATE.SAM", "TINA_T", "OLLIE.99", "MAYA.MOVES"
};
static const char *const CAPTIONS[] = {
    "WAIT FOR THE END!", "POV: IT IS MONDAY AGAIN", "NO WAY THIS WORKED",
    "SEND THIS TO YOUR BEST FRIEND", "WATCH TILL THE END", "TRY THIS AT HOME",
    "VIBES ONLY", "SO SATISFYING", "RATE THIS 1-10", "TURN UP THE SOUND",
    "THIS IS SO CALMING", "ONE MORE TIME!", "3DS FOREVER", "TAG SOMEONE WHO DOES THIS",
    "SEND THIS TO YOUR SNAP AI AND SEE WHAT IT SAYS"
};
#define N_CREATORS ((int)(sizeof(CREATORS) / sizeof(CREATORS[0])))
#define N_CAPTIONS ((int)(sizeof(CAPTIONS) / sizeof(CAPTIONS[0])))

/* ---------------- random numbers ---------------- */

static uint32_t hash(uint32_t x) {       // the same seed always gives the same video
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static float rnd(uint32_t seed, uint32_t k) {   // 0..1
    return (hash(seed * 2654435761u + k * 97u + 13u) & 0xFFFF) / 65535.0f;
}

static uint32_t next_rng(Spotlight *s) {
    s->rng ^= s->rng << 13; s->rng ^= s->rng >> 17; s->rng ^= s->rng << 5;
    return s->rng;
}

// 0xRRGGBB from hue (any number, 1.0 = one full turn), saturation and brightness
static uint32_t hsv(float h, float sat, float val) {
    h -= floorf(h);
    float r = 0, g = 0, b = 0, f = h * 6.0f;
    int i = (int)f;
    float fr = f - i, p = val * (1 - sat), q = val * (1 - sat * fr), t = val * (1 - sat * (1 - fr));
    switch (i % 6) {
    case 0: r = val; g = t; b = p; break;
    case 1: r = q; g = val; b = p; break;
    case 2: r = p; g = val; b = t; break;
    case 3: r = p; g = q; b = val; break;
    case 4: r = t; g = p; b = val; break;
    default: r = val; g = p; b = q; break;
    }
    return ((uint32_t)(r * 255) << 16) | ((uint32_t)(g * 255) << 8) | (uint32_t)(b * 255);
}

static void heart(uint16_t *c, float x, float y, float size, uint32_t rgb);

static float tri(float x) {              // goes 0..1..0 as x grows
    x -= floorf(x);
    return x < 0.5f ? x * 2 : 2 - x * 2;
}

/* ---------------- the videos ---------------- */

static void clip_plasma(uint16_t *c, uint32_t seed, float t) {
    float hue = rnd(seed, 1), fx = 0.2f + rnd(seed, 2) * 0.2f, fy = 0.2f + rnd(seed, 3) * 0.2f;
    for (int by = 0; by < CH; by += 8)
        for (int bx = 0; bx < CW; bx += 8) {
            float x = bx / 8.0f, y = by / 8.0f;
            float v = sinf(x * fx + t * 2.0f) + sinf(y * fy + t * 1.5f) + sinf((x + y) * 0.18f + t);
            draw_rect(c, bx, by, 8, 8, hsv(hue + v * 0.11f, 0.75f, 0.55f + 0.2f * sinf(v * 2)));
        }
}

static void clip_bounce(uint16_t *c, uint32_t seed, float t) {
    float hue = rnd(seed, 1);
    for (int y = 0; y < CH; y += 6)      // dark gradient behind
        draw_rect(c, 0, y, CW, 6, hsv(hue + 0.5f, 0.6f, 0.12f + 0.22f * y / CH));
    for (int i = 0; i < 7; i++) {
        float r = 12 + rnd(seed, 10 + i) * 22;
        float sx = 0.25f + rnd(seed, 20 + i) * 0.5f, sy = 0.3f + rnd(seed, 30 + i) * 0.6f;
        float x = r + tri(t * sx + rnd(seed, 40 + i)) * (CW - 2 * r);
        float y = r + tri(t * sy + rnd(seed, 50 + i)) * (CH - 2 * r);
        draw_ellipse(c, x + 4, y + 5, r, r, 0, BLACK, 90);      // shadow
        draw_ellipse(c, x, y, r, r, 0, hsv(hue + i * 0.13f, 0.8f, 1.0f), 255);
        draw_ellipse(c, x - r * 0.3f, y - r * 0.3f, r * 0.3f, r * 0.3f, 0, WHITE, 140);
    }
}

static void clip_waves(uint16_t *c, uint32_t seed, float t) {
    float hue = rnd(seed, 1), speed = 0.6f + rnd(seed, 2);
    for (int y = 0; y < CH; y += 4) {
        float w = sinf(y * 0.045f + t * 2.0f * speed) * 40.0f + sinf(y * 0.11f - t * speed) * 14.0f;
        draw_rect(c, 0, y, CW, 4, hsv(hue + y * 0.0025f + t * 0.05f, 0.7f, 0.9f));
        draw_rect(c, (int)(CW / 2 + w - 38), y, 76, 4, hsv(hue + 0.5f + y * 0.0025f, 0.5f, 1.0f));
    }
}

static void clip_stars(uint16_t *c, uint32_t seed, float t) {
    float hue = rnd(seed, 1);
    draw_rect(c, 0, 0, CW, CH, hsv(hue, 0.7f, 0.07f));
    for (int i = 0; i < 70; i++) {
        float z = (t * (0.18f + 0.1f * rnd(seed, 2)) + rnd(seed, 100 + i)) ;
        z -= floorf(z);                  // 0 = far, 1 = close
        float a = rnd(seed, 200 + i) * 2 * PI_F;
        float d = z * z * 230.0f;
        float x = CW / 2 + cosf(a) * d, y = CH / 2 + sinf(a) * d;
        int sz = 2 + (int)(z * 5);
        draw_rect(c, (int)x, (int)y, sz, sz, hsv(hue + rnd(seed, 300 + i) * 0.25f, 0.25f, 0.8f + 0.2f * z));
    }
}

static void clip_tunnel(uint16_t *c, uint32_t seed, float t) {
    float hue = rnd(seed, 1);
    float sway = sinf(t * 1.3f) * 22.0f, swayY = cosf(t * 1.7f) * 14.0f;
    float shift = t * 0.9f;
    int base = (int)floorf(shift);
    float frac = shift - floorf(shift);
    for (int k = 0; k <= 11; k++) {      // nearest (biggest) square first, the far ones on top
        float d = (k - frac);            // 0 = closest
        float size = 460.0f / (1.0f + d * 0.55f) ;
        float k2 = d / 11.0f;
        float cx = CW / 2 + sway * k2, cy = CH / 2 + swayY * k2;
        int w = (int)size, h = (int)(size * 0.75f);
        draw_rect(c, (int)(cx - w / 2), (int)(cy - h / 2), w, h,
                  hsv(hue + ((k + base) & 1) * 0.5f, 0.75f, (k + base) & 1 ? 0.95f : 0.35f));
    }
}


static void clip_fireworks(uint16_t *c, uint32_t seed, float t) {
    for (int y = 0; y < CH; y += 8) draw_rect(c, 0, y, CW, 8, 0x050818 + ((y * 20 / CH) << 8));
    for (int b = 0; b < 4; b++) {
        float cyc = t * 0.45f + rnd(seed, 10 + b);
        int n = (int)floorf(cyc);
        float ph = cyc - floorf(cyc);
        uint32_t sd = seed + (uint32_t)n * 131u;
        float bx = 50 + rnd(sd, 20 + b) * (CW - 100), by = 40 + rnd(sd, 30 + b) * 90, hue = rnd(sd, 40 + b);
        if (ph < 0.15f) {                // the rocket goes up
            draw_rect(c, (int)bx, (int)(CH - (CH - by) * (ph / 0.15f)), 2, 6, WHITE);
            continue;
        }
        float p = (ph - 0.15f) / 0.85f, r = 62 * (1 - (1 - p) * (1 - p));
        if (p > 0.92f) continue;
        for (int i = 0; i < 18; i++) {
            float a = i * 2 * PI_F / 18 + rnd(sd, 50 + b) * 3;
            draw_rect(c, (int)(bx + cosf(a) * r), (int)(by + sinf(a) * r + 26 * p * p), p < 0.7f ? 3 : 2, p < 0.7f ? 3 : 2,
                      hsv(hue + (i % 3) * 0.08f, 0.8f, 1.0f - p * 0.6f));
        }
    }
}

static void clip_hearts(uint16_t *c, uint32_t seed, float t) {
    for (int y = 0; y < CH; y += 8) draw_rect(c, 0, y, CW, 8, hsv(0.96f, 0.28f + 0.3f * y / CH, 1.0f));
    for (int i = 0; i < 16; i++) {
        float size = 12 + rnd(seed, 10 + i) * 22, sp = 30 + rnd(seed, 30 + i) * 40;
        float x = rnd(seed, 50 + i) * CW + sinf(t * 1.5f + i) * 10;
        float y = fmodf(t * sp + rnd(seed, 70 + i) * CH, CH + 40) - 20;
        heart(c, x, y, size, hsv(0.93f + rnd(seed, 90 + i) * 0.07f, 0.7f, 1.0f));
    }
}

static void clip_disco(uint16_t *c, uint32_t seed, float t) {
    float hue = rnd(seed, 1);
    for (int gy = 0; gy < CH / 20; gy++)
        for (int gx = 0; gx < CW / 20; gx++) {
            float v = sinf(t * 4.0f + gx * 0.8f + gy * 0.6f);
            draw_rect(c, gx * 20, gy * 20, 20, 20, hsv(hue + (gx + gy) * 0.03f + t * 0.1f, 0.8f, v > 0 ? 1.0f : 0.25f));
        }
}

static void clip_bubbles(uint16_t *c, uint32_t seed, float t) {
    float hue = 0.52f + rnd(seed, 1) * 0.15f;
    for (int y = 0; y < CH; y += 8) draw_rect(c, 0, y, CW, 8, hsv(hue, 0.7f, 0.9f - 0.55f * y / CH));
    for (int i = 0; i < 18; i++) {
        float r = 6 + rnd(seed, 10 + i) * 14, sp = 20 + rnd(seed, 30 + i) * 35;
        float x = rnd(seed, 50 + i) * CW + sinf(t * 2 + i) * 8;
        float y = CH + 20 - fmodf(t * sp + rnd(seed, 70 + i) * CH, CH + 40);
        draw_ellipse(c, x, y, r + 1, r + 1, 0, WHITE, 45);
        draw_ellipse(c, x, y, r - 1, r - 1, 0, hsv(hue, 0.3f, 1.0f), 70);
        draw_ellipse(c, x - r * 0.35f, y - r * 0.35f, r * 0.22f, r * 0.22f, 0, WHITE, 200);
    }
}

static void draw_clip(uint16_t *c, uint32_t seed, float t) {
    switch (hash(seed) % SP_KINDS) {
    case 0: clip_plasma(c, seed, t); break;
    case 1: clip_bounce(c, seed, t); break;
    case 2: clip_waves(c, seed, t); break;
    case 3: clip_stars(c, seed, t); break;
    case 4: clip_tunnel(c, seed, t); break;
    case 5: clip_fireworks(c, seed, t); break;
    case 6: clip_hearts(c, seed, t); break;
    case 7: clip_disco(c, seed, t); break;
    default: clip_bubbles(c, seed, t); break;
    }
}


static void label(uint16_t *c, int x, int y, int scale, const char *t, uint32_t rgb);

/* ---------------- fruit stories ----------------
   Two cartoon fruits act out a four-line story. They blink, talk (the mouth
   moves while the words type in), hop, and react at the end. */

#define LINE_MS 2800                     // time for each of the four lines
#define STORY_MS (4 * LINE_MS)
#define N_FRUITS 12
#define N_SCENES 8
#define DANCE_MS 8000

static const char *const FRUIT_NAMES[N_FRUITS] = {
    "APPLE", "BANANA", "STRAWBERRY", "ORANGE", "WATERMELON", "LEMON",
    "PEACH", "PEAR", "PINEAPPLE", "MANGO", "BLUEBERRY", "COCONUT"
};
static const char *const FRUIT_CREATORS[] = {
    "FRUIT.TALES", "JUICY.STORIES", "BERRY.NEWS", "FRUIT.DRAMA.TV", "PEACH.PLAYS", "SLICE.OF.LIFE"
};
#define N_FRUIT_CREATORS ((int)(sizeof(FRUIT_CREATORS) / sizeof(FRUIT_CREATORS[0])))

typedef struct { const char *title; bool shock; const char *line[4]; } Story;
// '#' in a line becomes the name of the fruit being spoken to.
static const Story STORIES[] = {
    { "FRUIT LOVE STORY", true, { "HEY #. I HAVE A SECRET.", "WHAT IS IT. TELL ME!",
        "I THINK I AM IN LOVE WITH YOU.", "OH NO. I AM A SMOOTHIE FAN!" } },
    { "FRUIT BIRTHDAY", false, { "HAPPY BIRTHDAY #!", "YOU REMEMBERED!",
        "I MADE YOU A CAKE.", "BEST FRIEND EVER!" } },
    { "FRUIT RACE", false, { "I WILL BEAT YOU IN A RACE #.", "NO WAY. I AM WAY FASTER.",
        "READY. SET. GO!", "WAIT. I FORGOT MY SHOES!" } },
    { "FRUIT DRAMA", true, { "# I HAVE BAD NEWS.", "WHAT HAPPENED.",
        "THE JUICE BAR OPENS TODAY.", "NOOOOO! RUN!" } },
    { "FRUIT GOSSIP", true, { "DID YOU HEAR ABOUT THE GRAPES.", "NO. TELL ME EVERYTHING!",
        "THEY ARE BECOMING WINE.", "WOW. THE DRAMA!" } },
    { "FRUIT SCHOOL", false, { "# WE HAVE A TEST TODAY.", "WHAT. I DID NOT STUDY.",
        "ME NEITHER. LETS COPY THE PEAR.", "GOOD IDEA. PEAR IS SMART!" } },
    { "FRUIT COOKING SHOW", true, { "WELCOME TO COOKING WITH FRUIT!", "TODAY WE MAKE A SALAD.",
        "FIRST WE NEED TO CHOP #.", "WAIT WHAT. I AM THE GUEST!" } },
    { "FRUIT DETECTIVE", true, { "SOMEONE ATE THE LAST COOKIE.", "IT WAS NOT ME #.",
        "THERE ARE CRUMBS ON YOUR FACE.", "OK FINE. IT WAS ME!" } },
    { "FRUIT GYM", false, { "TODAY IS LEG DAY #.", "BUT I DO NOT HAVE LEGS.",
        "YOU HAVE LITTLE ONES. SQUAT!", "I CAN FEEL THE BURN!" } },
    { "FRUIT SPACE TRIP", true, { "WE ARE ON THE MOON #!", "THIS IS SO COOL!",
        "WAIT. WHERE IS OUR ROCKET.", "OH NO. WE ARE STUCK HERE!" } },
    { "FRUIT TALENT SHOW", false, { "I WILL SING FOR THE JUDGES.", "GO FOR IT #!",
        "LA LA LA LA LA!", "TEN OUT OF TEN. WOW!" } },
    { "FRUIT DATE NIGHT", false, { "YOU LOOK GREAT TONIGHT #.", "THANK YOU. YOU TOO!",
        "SHALL WE SHARE A MILKSHAKE.", "YES. WITH TWO STRAWS!" } },
    { "FRUIT HORROR", true, { "DID YOU HEAR A NOISE #.", "NO. IT IS PROBABLY NOTHING.",
        "IT IS COMING FROM THE FRIDGE.", "AAAAH! IT IS THE BLENDER!" } },
    { "FRUIT PRANK", true, { "I PUT A FAKE SPIDER ON YOU.", "WHERE. GET IT OFF!",
        "JUST KIDDING. IT IS A RAISIN.", "NOT FUNNY #. REVENGE TIME!" } },
    { "FRUIT NEW KID", false, { "HI #. I AM NEW HERE.", "WELCOME. WANT TO PLAY.",
        "YES PLEASE. I LOVE GAMES.", "BEST FRIENDS FOREVER!" } },
    { "FRUIT TAXI", false, { "WHERE TO #.", "TAKE ME TO THE BEACH.",
        "THAT IS TWO HOURS AWAY.", "PERFECT. I BROUGHT SNACKS!" } },
    { "FRUIT MAGIC SHOW", false, { "WATCH THIS MAGIC TRICK #.", "OOH. DO IT!",
        "ABRACADABRA. I AM INVISIBLE!", "BUT I CAN STILL SEE YOU!" } },
    { "FRUIT MOVIE NIGHT", false, { "I PICKED A SCARY MOVIE #.", "I AM NOT SCARED AT ALL.",
        "THE MOVIE HAS NOT STARTED.", "I AM STILL NOT SCARED!" } },
    { "FRUIT WEATHER NEWS", true, { "BREAKING NEWS #.", "WHAT IS HAPPENING.",
        "TODAY IT WILL RAIN SMOOTHIES.", "RUN FOR COVER!" } },
    { "FRUIT PET SHOP", false, { "I WANT TO BUY A PET.", "HOW ABOUT A GOLDFISH #.",
        "I WANT SOMETHING FLUFFY.", "THEN GET A COCONUT!" } },
    { "PRUTAS TSISMIS", true, { "ALAM MO BA # MAY CRUSH SIYA.", "TALAGA. SINO. SINO!",
        "ANG PINYA DAW. SHHH.", "HALA! NARINIG NIYA TAYO!" } },
    { "KAARAWAN NG PRUTAS", false, { "MALIGAYANG KAARAWAN #!", "SALAMAT. NAALALA MO PA!",
        "MAY PANSIT AT CAKE TAYO.", "SOBRANG SAYA NG ARAW NA TO!" } },
    { "KARERA NG PRUTAS", false, { "MAGKARERA TAYO #.", "SIGE. AKO ANG MANANALO!",
        "HANDA NA. 1. 2. 3. TAKBO!", "TEKA. NAKALIMUTAN KO SAPATOS KO!" } },
    { "BALITA NG PRUTAS", true, { "# MAY MASAMANG BALITA.", "ANO YON. SABIHIN MO NA!",
        "MAGBUBUKAS NA ANG JUICE BAR.", "HALA. TAKBO NA!" } }
};
#define N_STORIES ((int)(sizeof(STORIES) / sizeof(STORIES[0])))

// What kind of video a seed makes. Out of 100 videos: 18 fruit stories, 6 fruit dances, 8 abstract
// clips, 18 block stories, 9 slow-mo runs, 8 block dances, 12 "in this town" puppet stories,
// 12 horror stories and 9 horror clips.
enum { VK_FRUIT, VK_DANCE, VK_CLIP, VK_BLOCK, VK_RUN, VK_BDANCE, VK_TOWN, VK_HSTORY, VK_HCLIP };
static int vkind(uint32_t seed) {
    static const unsigned char UPTO[] = { 18, 24, 32, 50, 59, 67, 79, 91, 100 };
    unsigned v = hash(seed + 77) % 100;
    int k = 0;
    while (k < 8 && v >= UPTO[k]) k++;
    return k;
}

// face of a fruit. look: -1 left, +1 right. shock: wide eyes and a round mouth.
static void fruit_face(uint16_t *c, float cx, float cy, float r, float rx, float t,
                       bool talk, int look, bool shock, float blinkPhase) {
    bool blink = fmodf(t * 0.37f + blinkPhase, 1.0f) < 0.06f;
    for (int sd = -1; sd <= 1; sd += 2) {
        float ex = cx + sd * r * 0.33f, ey = cy - r * 0.1f;
        float er = r * (shock ? 0.27f : 0.2f), ery = blink ? er * 0.15f : er;
        draw_ellipse(c, ex, ey, er, ery, 0, WHITE, 255);
        float pr = r * 0.1f;
        draw_ellipse(c, ex + look * er * 0.35f, ey, pr, pr < ery ? pr : ery, 0, BLACK, 255);
        float bx = cx + sd * (r * 0.6f < rx * 0.85f ? r * 0.6f : rx * 0.85f);
        draw_ellipse(c, bx, cy + r * 0.25f, r * 0.14f, r * 0.09f, 0, 0xFF7A8A, 150);   // blush
    }
    float my = cy + r * 0.42f;
    if (talk) {
        float open = 0.35f + 0.65f * fabsf(sinf(t * 16.0f));
        draw_ellipse(c, cx, my, r * 0.2f, r * 0.17f * open + 1, 0, 0x3A0A10, 255);
        draw_ellipse(c, cx, my + r * 0.05f * open, r * 0.11f, r * 0.05f * open + 0.6f, 0, 0xFF6F80, 255);
    } else if (shock) {
        draw_ellipse(c, cx, my + 2, r * 0.17f, r * 0.2f, 0, 0x3A0A10, 255);
    } else {
        draw_line(c, cx - r * 0.2f, my - r * 0.06f, cx, my + r * 0.05f, 2.0f, BLACK);   // smile
        draw_line(c, cx, my + r * 0.05f, cx + r * 0.2f, my - r * 0.06f, 2.0f, BLACK);
    }
}

// kind: 0 apple, 1 banana, 2 strawberry, 3 orange, 4 watermelon, 5 lemon,
//       6 peach, 7 pear, 8 pineapple, 9 mango, 10 blueberry, 11 coconut
static void fruit(uint16_t *c, int kind, float cx, float cy, float r, float t,
                  bool talk, int look, bool shock, float blinkPhase) {
    float rx = r, ry = r;
    uint32_t col = 0;
    switch (kind) {
    case 0: ry = r * 0.95f; col = 0xE5283A; break;
    case 1: rx = r * 0.68f; ry = r * 1.12f; col = 0xFFE135; break;
    case 2: rx = r * 0.9f; col = 0xFF3B4E; break;
    case 3: col = 0xFF9F1C; break;
    case 4: rx = r * 1.05f; ry = r * 0.95f; col = 0x2E9E4B; break;
    case 5: rx = r * 1.1f; ry = r * 0.88f; col = 0xFFF03A; break;
    case 6: ry = r * 0.97f; col = 0xFFB38A; break;
    case 7: rx = r * 0.95f; col = 0xA8D83F; break;
    case 8: rx = r * 0.85f; ry = r * 1.05f; col = 0xF5C518; break;
    case 9: ry = r * 0.9f; col = 0xFF9A3C; break;
    case 10: rx = r * 0.92f; ry = r * 0.92f; col = 0x4B5BD8; break;
    default: col = 0x7A4B2A; break;
    }
    float wave = talk ? sinf(t * 10.0f) * 7.0f : 0.0f;
    for (int sd = -1; sd <= 1; sd += 2) {
        float lx = cx + sd * rx * 0.38f, ly = cy + ry * 0.85f;                 // legs and shoes
        draw_line(c, lx, ly, lx, ly + 12, 3.0f, BLACK);
        draw_ellipse(c, lx + sd * 2, ly + 13, 7, 4, 0, BLACK, 255);
        draw_ellipse(c, lx + sd * 2, ly + 13, 5.5f, 2.8f, 0, WHITE, 255);
        draw_line(c, cx + sd * rx * 0.9f, cy + ry * 0.2f,                      // arms
                  cx + sd * (rx + 13), cy + ry * 0.2f + 6 - (sd > 0 ? wave : 0), 3.0f, BLACK);
    }
    if (kind == 8)                                                             // pineapple crown (behind)
        for (int k = -2; k <= 2; k++) draw_line(c, cx + k * 4, cy - ry + 3, cx + k * 7, cy - ry - 14 + abs(k) * 2, 3.0f, 0x2E9E4B);
    if (kind == 7) draw_ellipse(c, cx, cy - ry * 0.7f, rx * 0.62f + 2, ry * 0.5f + 2, 0, BLACK, 255);   // pear top
    draw_ellipse(c, cx, cy, rx + 2, ry + 2, 0, BLACK, 255);                    // outline
    draw_ellipse(c, cx, cy, rx, ry, 0, col, 255);
    if (kind == 7) draw_ellipse(c, cx, cy - ry * 0.7f, rx * 0.62f, ry * 0.5f, 0, col, 255);
    switch (kind) {
    case 0:
        draw_line(c, cx, cy - ry, cx + 3, cy - ry - 9, 3.0f, 0x6B3A1E);
        draw_ellipse(c, cx + 10, cy - ry - 5, 8, 4, -0.5f, 0x3DBB4D, 255);
        draw_ellipse(c, cx - rx * 0.55f, cy - ry * 0.5f, 4, 6, 0.6f, WHITE, 100);
        break;
    case 1:
        draw_ellipse(c, cx + 1, cy - ry, 4, 5, 0, 0x6B4A1E, 255);
        draw_line(c, cx - rx * 0.85f, cy - ry * 0.4f, cx - rx * 0.85f, cy + ry * 0.4f, 2.0f, 0xE0B800);
        break;
    case 2:
        draw_ellipse(c, cx - 9, cy - ry + 2, 9, 4, -0.4f, 0x3DBB4D, 255);
        draw_ellipse(c, cx + 9, cy - ry + 2, 9, 4, 0.4f, 0x3DBB4D, 255);
        draw_ellipse(c, cx, cy - ry - 2, 6, 5, 0, 0x3DBB4D, 255);
        {
            static const signed char sd[][2] = { {-70,-50},{70,-50},{-82,10},{82,10},{-52,66},{52,66},{0,82} };
            for (int i = 0; i < 7; i++)
                draw_rect(c, (int)(cx + sd[i][0] * rx / 100.0f), (int)(cy + sd[i][1] * ry / 100.0f), 2, 3, 0xFFE9A0);
        }
        break;
    case 3:
        draw_ellipse(c, cx + 5, cy - ry - 1, 8, 4, -0.4f, 0x3DBB4D, 255);
        for (int i = 0; i < 6; i++)
            draw_rect(c, (int)(cx + (i % 2 ? 0.7f : -0.7f) * rx * (0.55f + 0.1f * i)),
                      (int)(cy - ry * 0.6f + i * ry * 0.25f), 2, 2, 0xE08500);
        break;
    case 4:
        draw_ellipse(c, cx - rx * 0.82f, cy, rx * 0.1f, ry * 0.8f, 0, 0x1B6B33, 255);
        draw_ellipse(c, cx + rx * 0.82f, cy, rx * 0.1f, ry * 0.8f, 0, 0x1B6B33, 255);
        draw_ellipse(c, cx, cy - ry * 0.9f, rx * 0.4f, ry * 0.08f, 0, 0x1B6B33, 255);
        break;
    case 5:
        draw_ellipse(c, cx + rx, cy, 5, 4, 0, col, 255);
        draw_ellipse(c, cx + 4, cy - ry - 1, 8, 4, -0.4f, 0x3DBB4D, 255);
        break;
    case 6:                              // peach: a little dent and a leaf
        draw_line(c, cx, cy - ry, cx + 3, cy - ry * 0.45f, 1.5f, 0xE88F6A);
        draw_ellipse(c, cx + 7, cy - ry - 2, 8, 4, -0.4f, 0x3DBB4D, 255);
        break;
    case 7:                              // pear: stem
        draw_line(c, cx, cy - ry * 1.2f, cx + 3, cy - ry * 1.2f - 8, 3.0f, 0x6B3A1E);
        break;
    case 8:                              // pineapple: diamond pattern
        draw_line(c, cx - rx * 0.85f, cy + ry * 0.3f, cx - rx * 0.3f, cy + ry * 0.85f, 1.2f, 0xD9A400);
        draw_line(c, cx + rx * 0.85f, cy + ry * 0.3f, cx + rx * 0.3f, cy + ry * 0.85f, 1.2f, 0xD9A400);
        draw_line(c, cx - rx * 0.85f, cy + ry * 0.7f, cx - rx * 0.5f, cy + ry * 0.95f, 1.2f, 0xD9A400);
        draw_line(c, cx + rx * 0.85f, cy + ry * 0.7f, cx + rx * 0.5f, cy + ry * 0.95f, 1.2f, 0xD9A400);
        break;
    case 9:                              // mango: red blush and a leaf
        draw_ellipse(c, cx - rx * 0.55f, cy - ry * 0.3f, 8, 12, 0.5f, 0xFF4D3D, 90);
        draw_ellipse(c, cx + 8, cy - ry - 2, 9, 4, -0.5f, 0x3DBB4D, 255);
        break;
    case 10:                             // blueberry: the star on top
        draw_ellipse(c, cx, cy - ry * 0.85f, 6, 3, 0, 0x2A3590, 255);
        draw_ellipse(c, cx - rx * 0.55f, cy - ry * 0.45f, 3, 5, 0.6f, WHITE, 90);
        break;
    default:                             // coconut: a straw
        draw_line(c, cx + 6, cy - ry + 2, cx + 15, cy - ry - 14, 3.0f, 0xFF4D6D);
        draw_ellipse(c, cx - rx * 0.55f, cy - ry * 0.45f, 3, 5, 0.6f, WHITE, 70);
        break;
    }
    fruit_face(c, cx, cy, r, rx, t, talk, look, shock, blinkPhase);
}

static void story_background(uint16_t *c, int scene, uint32_t seed, float t) {
    const int ground = 122;
    switch (scene) {
    case 0:                              // kitchen
        for (int y = 0; y < ground; y += 20)
            for (int x = 0; x < CW; x += 20)
                draw_rect(c, x, y, 20, 20, ((x / 20 + y / 20) & 1) ? 0xF0D9A8 : 0xF6E3C0);
        draw_rect(c, 226, 50, 42, 40, 0xFFFFFF);
        draw_rect(c, 229, 53, 36, 34, 0x9AD7F5);
        draw_rect(c, 245, 53, 4, 34, 0xFFFFFF);
        draw_rect(c, 0, ground, CW, CH - ground, 0x8B5A2B);
        draw_rect(c, 0, ground, CW, 6, 0xA9743C);
        break;
    case 1:                              // park
        for (int y = 0; y < ground; y += 6)
            draw_rect(c, 0, y, CW, 6, 0x6EC6F5 + ((y * 40 / ground) << 8));
        draw_ellipse(c, 256, 60, 15 + sinf(t * 3) * 1.5f, 15 + sinf(t * 3) * 1.5f, 0, 0xFFE94A, 255);
        for (int i = 0; i < 3; i++) {
            float x = fmodf(t * (6 + i * 3) + i * 120, CW + 80) - 40, y = 60 + i * 22;
            draw_ellipse(c, x, y, 26, 8, 0, WHITE, 230);
            draw_ellipse(c, x + 12, y - 5, 16, 8, 0, WHITE, 230);
        }
        draw_rect(c, 0, ground, CW, CH - ground, 0x58B947);
        for (int x = 0; x < CW; x += 24) draw_rect(c, x, ground, 12, CH - ground, 0x62C250);
        break;
    case 2:                              // night
        for (int y = 0; y < ground; y += 6)
            draw_rect(c, 0, y, CW, 6, 0x0B1030 + ((y * 30 / ground) << 8) + ((y * 40 / ground) << 0));
        for (int i = 0; i < 26; i++) {
            float tw = 0.5f + 0.5f * sinf(t * 2.5f + i * 1.7f);
            int v = 120 + (int)(135 * tw);
            draw_rect(c, (int)(rnd(seed, 600 + i) * (CW - 40)), 12 + (int)(rnd(seed, 650 + i) * 100), 2, 2,
                      (v << 16) | (v << 8) | v);
        }
        draw_ellipse(c, 250, 62, 15, 15, 0, 0xFFF6C8, 255);
        draw_ellipse(c, 257, 58, 13, 13, 0, 0x0F1838, 255);
        draw_rect(c, 0, ground, CW, CH - ground, 0x1C2B3F);
        break;
    case 4:                              // beach
        for (int y = 0; y < 90; y += 6) draw_rect(c, 0, y, CW, 6, 0x7FD0F7 + ((y * 30 / 90) << 8));
        draw_ellipse(c, 60, 55, 16, 16, 0, 0xFFE94A, 255);
        draw_rect(c, 0, 90, CW, ground - 90, 0x2F8FD8);
        for (int i = 0; i < 6; i++)
            draw_rect(c, (int)fmodf(i * 60 + t * 20, CW + 30) - 30, 96 + (i % 3) * 8, 26, 2, 0xBFE8FF);
        draw_rect(c, 0, ground, CW, CH - ground, 0xF2D6A0);
        for (int i = 0; i < 18; i++) draw_rect(c, (int)(rnd(seed, 800 + i) * CW), ground + 6 + (int)(rnd(seed, 830 + i) * 100), 2, 2, 0xD9B77A);
        break;
    case 5:                              // classroom
        draw_rect(c, 0, 0, CW, ground, 0xE9E2CF);
        draw_rect(c, 24, 46, 200, 66, 0x6B4A2B);
        draw_rect(c, 28, 50, 192, 58, 0x2F5D3A);
        draw_text(c, 52, 62, 3, "ABC 123", 0xF2F2F2);
        draw_line(c, 52, 92, 190, 92, 2.0f, 0xF2F2F2);
        draw_rect(c, 0, ground, CW, CH - ground, 0xA5763F);
        draw_rect(c, 0, ground, CW, 5, 0x7F5A2E);
        break;
    case 6:                              // space
        for (int y = 0; y < ground; y += 6) draw_rect(c, 0, y, CW, 6, 0x0A0720 + ((y * 20 / ground) << 16));
        for (int i = 0; i < 40; i++) {
            int v = 110 + (int)(145 * (0.5f + 0.5f * sinf(t * 2.2f + i * 1.3f)));
            draw_rect(c, (int)(rnd(seed, 600 + i) * CW), (int)(rnd(seed, 650 + i) * ground), 2, 2, (v << 16) | (v << 8) | v);
        }
        draw_ellipse(c, 250, 70, 20, 20, 0, 0xC95F3B, 255);
        draw_ellipse(c, 250, 70, 32, 6, -0.4f, 0xE8C27A, 160);
        draw_rect(c, 0, ground, CW, CH - ground, 0x8A8A94);
        for (int i = 0; i < 5; i++) draw_ellipse(c, 30 + i * 70, ground + 24 + (i % 2) * 20, 18, 6, 0, 0x6E6E78, 255);
        break;
    case 7:                              // snow
        for (int y = 0; y < ground; y += 6) draw_rect(c, 0, y, CW, 6, 0xA9CBEE + ((y * 40 / ground) << 8) + (y * 20 / ground));
        draw_rect(c, 0, ground, CW, CH - ground, 0xF4F8FF);
        draw_rect(c, 0, ground, CW, 4, 0xD5E3F5);
        for (int i = 0; i < 44; i++) {
            float y = fmodf(t * (20 + rnd(seed, 700 + i) * 30) + rnd(seed, 740 + i) * CH, CH + 8) - 4;
            draw_rect(c, (int)(rnd(seed, 780 + i) * CW + sinf(t * 1.5f + i) * 5), (int)y, 3, 3, WHITE);
        }
        break;
    default:                             // party
        for (int y = 0; y < ground; y += 6)
            draw_rect(c, 0, y, CW, 6, 0x5B2A86 + ((y * 30 / ground) << 8));
        for (int i = 0; i < 3; i++)
            draw_ellipse(c, CW / 2 + sinf(t * 1.2f + i * 2.1f) * 110, 90, 42, 42, 0,
                         hsv(i * 0.33f + t * 0.1f, 0.7f, 1.0f), 70);
        draw_rect(c, 0, ground, CW, CH - ground, 0x3A1A5A);
        for (int i = 0; i < 30; i++) {
            float sp = 25 + rnd(seed, 700 + i) * 50;
            float y = fmodf(t * sp + rnd(seed, 730 + i) * CH, CH + 10) - 5;
            float x = rnd(seed, 760 + i) * CW + sinf(t * 2 + i) * 6;
            draw_rect(c, (int)x, (int)y, 3, 3, hsv(rnd(seed, 790 + i), 0.8f, 1.0f));
        }
        break;
    }
}

static void story_subtitle(uint16_t *c, const char *text, int shown) {
    char line[3][32] = { "", "", "" };
    int start[3] = { 0, 0, 0 }, n = 0, len = (int)strlen(text), pos = 0;
    while (pos < len && n < 3) {         // wrap at spaces, 21 letters per row
        int end = pos + 21 < len ? pos + 21 : len;
        if (end < len) while (end > pos && text[end] != ' ') end--;
        if (end == pos) end = pos + 21 < len ? pos + 21 : len;
        memcpy(line[n], text + pos, end - pos);
        line[n][end - pos] = 0;
        start[n++] = pos;
        pos = end;
        while (pos < len && text[pos] == ' ') pos++;
    }
    for (int i = 0; i < n; i++) {
        int x = 146 - text_width(line[i], 2) / 2, y = 134 + i * 18;
        int visible = shown - start[i];
        if (visible <= 0) continue;
        if (visible > (int)strlen(line[i])) visible = (int)strlen(line[i]);
        char part[32];
        memcpy(part, line[i], visible);
        part[visible] = 0;
        label(c, x, y, 2, part, YELLOW);
    }
}

static void draw_story(uint16_t *c, uint32_t seed, unsigned long long age) {
    const Story *st = &STORIES[hash(seed + 4) % N_STORIES];
    int fa = hash(seed + 5) % N_FRUITS, fb = (fa + 1 + hash(seed + 6) % (N_FRUITS - 1)) % N_FRUITS;
    float t = age / 1000.0f;
    int line = (int)((age / LINE_MS) % 4);
    unsigned lt = (unsigned)(age % LINE_MS);
    story_background(c, hash(seed + 3) % N_SCENES, seed, t);

    char text[96] = "";                  // the line, with # replaced by who is listening
    const char *src = st->line[line], *name = FRUIT_NAMES[line % 2 == 0 ? fb : fa];
    for (const char *p = src; *p && strlen(text) < sizeof(text) - 14; p++) {
        if (*p == '#') strcat(text, name);
        else { size_t k = strlen(text); text[k] = *p; text[k + 1] = 0; }
    }
    int shown = (int)(lt * 0.022f), total = (int)strlen(text);
    bool speaking = shown < total + 6 && lt < LINE_MS - 300;
    bool aTalks = speaking && line % 2 == 0, bTalks = speaking && line % 2 == 1;
    bool react = st->shock && line == 3;
    bool happy = !st->shock && line == 3;

    float hopA = aTalks ? -fabsf(sinf(t * 9.0f)) * 3 : 0, hopB = bTalks ? -fabsf(sinf(t * 9.0f)) * 3 : 0;
    if (react || happy) { hopA -= fabsf(sinf(t * 12.0f)) * 7; hopB -= fabsf(sinf(t * 12.0f + 1)) * 7; }
    float shake = react ? sinf(t * 40.0f) * 2.0f : 0;
    fruit(c, fa, 88 + shake, 82 + hopA + sinf(t * 2.0f) * 1.5f, aTalks ? 33 : 29, t, aTalks, 1, react,
          rnd(seed, 11));
    fruit(c, fb, 196 - shake, 82 + hopB + sinf(t * 2.0f + 1) * 1.5f, bTalks ? 33 : 29, t, bTalks, -1, react,
          rnd(seed, 12));
    story_subtitle(c, text, shown);
}


static const char *const DANCE_CAPS[] = {
    "FRUIT DANCE PARTY", "FRUIT DANCE BATTLE", "FRUITS ON THE DANCE FLOOR", "SUMMER FRUIT DISCO"
};

// Three fruits dance and sing while music notes float up.
static void draw_dance(uint16_t *c, uint32_t seed, unsigned long long age) {
    float t = age / 1000.0f;
    int f[3];
    f[0] = hash(seed + 5) % N_FRUITS;
    f[1] = (f[0] + 1 + hash(seed + 6) % (N_FRUITS - 1)) % N_FRUITS;
    f[2] = (f[1] + 1 + hash(seed + 7) % (N_FRUITS - 1)) % N_FRUITS;
    while (f[2] == f[0] || f[2] == f[1]) f[2] = (f[2] + 1) % N_FRUITS;
    story_background(c, hash(seed + 3) % N_SCENES, seed, t);
    for (int i = 0; i < 6; i++) {        // notes
        float y = 112 - fmodf(t * 28 + i * 22, 100), x = 40 + i * 40 + sinf(t * 2 + i) * 8;
        uint32_t col = hsv(i * 0.17f + t * 0.2f, 0.7f, 1.0f);
        draw_ellipse(c, x, y, 4, 3, -0.4f, col, 255);
        draw_line(c, x + 3, y, x + 3, y - 12, 2.0f, col);
        draw_line(c, x + 3, y - 12, x + 8, y - 9, 2.0f, col);
    }
    for (int i = 0; i < 3; i++) {
        float hop = -fabsf(sinf(t * 6.0f + i * 1.1f)) * 12, sway = sinf(t * 3.0f + i) * 6;
        bool sing = sinf(t * 2.0f + i * 2.0f) > -0.3f;
        fruit(c, f[i], 70 + i * 80 + sway, 88 + hop, 27, t + i, sing, sinf(t * 3.0f + i) > 0 ? 1 : -1, false, rnd(seed, 20 + i));
    }
    const char *tag = "DANCE PARTY!";
    label(c, 146 - text_width(tag, 2) / 2, 142, 2, tag, fabsf(sinf(t * 3.0f)) > 0.5f ? YELLOW : WHITE);
}

/* ---------------- block people, town puppets and horror ----------------
   More kinds of video. Everything is drawn on the spot and made up here:
   - BLOCK STORIES: two chunky block people act out a four-line story
   - SLOW-MO RUN: a block person runs in slow motion, then does a jump kick
   - BLOCK DANCE: three block people dance
   - IN THIS TOWN: paper puppets on a little stage tell a story about a town
   - HORROR STORIES and HORROR CLIPS: spooky cartoon style, nothing gory */

#define RUN_MS 6000
#define HCLIP_MS 8000
#define AL(x) ((int)(sizeof(x) / sizeof((x)[0])))

typedef struct { uint32_t skin, hair, shirt, pants; int style; } Blocky;   // style: 0 bald, 1 hair, 2 cap, 3 hair + sunglasses

static const uint32_t SKINS[]  = { 0xFFD9A8, 0xF2B880, 0xC68642, 0x8D5524, 0xFFE0BD, 0xF7C873 };
static const uint32_t SHIRTS[] = { 0x2E86DE, 0xE74C3C, 0x27AE60, 0x9B59B6, 0xF39C12, 0x16A085, 0xE84393, 0x34495E };
static const uint32_t PANTSS[] = { 0x2C3E50, 0x1E8449, 0x6C3483, 0x784212, 0x1B4F72, 0x555555 };
static const uint32_t HAIRS[]  = { 0x222222, 0x6B3A1E, 0xF4D03F, 0xC0392B, 0x7F8C8D, 0x2E86DE };

static Blocky make_blocky(uint32_t seed, uint32_t k) {
    Blocky b;
    b.skin = SKINS[hash(seed + k * 11 + 1) % AL(SKINS)];
    b.shirt = SHIRTS[hash(seed + k * 11 + 2) % AL(SHIRTS)];
    b.pants = PANTSS[hash(seed + k * 11 + 3) % AL(PANTSS)];
    b.hair = HAIRS[hash(seed + k * 11 + 4) % AL(HAIRS)];
    b.style = (int)(hash(seed + k * 11 + 5) % 3);
    return b;
}

// a rectangle with float numbers
static void rf(uint16_t *c, float x, float y, float w, float h, uint32_t rgb) {
    int iw = (int)(w + 0.5f), ih = (int)(h + 0.5f);
    draw_rect(c, (int)floorf(x + 0.5f), (int)floorf(y + 0.5f), iw < 1 ? 1 : iw, ih < 1 ? 1 : ih, rgb);
}

// a thick straight bar from (x0,y0) to (x1,y1) with square ends: arms and legs
static void limb(uint16_t *c, float x0, float y0, float x1, float y1, float w, uint32_t rgb) {
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    float ux = dx / len, uy = dy / len, h = w * 0.5f;
    int xa = (int)(fminf(x0, x1) - h - 1), xb = (int)(fmaxf(x0, x1) + h + 1);
    int ya = (int)(fminf(y0, y1) - h - 1), yb = (int)(fmaxf(y0, y1) + h + 1);
    if (xa < 0) xa = 0;
    if (ya < 0) ya = 0;
    if (xb > CW - 1) xb = CW - 1;
    if (yb > CH - 1) yb = CH - 1;
    for (int y = ya; y <= yb; y++)
        for (int x = xa; x <= xb; x++) {
            float px = x - x0, py = y - y0;
            float u = px * ux + py * uy, v = -px * uy + py * ux;
            if (u >= 0 && u <= len && v >= -h && v <= h) draw_rect(c, x, y, 1, 1, rgb);
        }
}

// Angles are measured from "straight down"; a positive angle swings towards the right.
// face: 0 smile, 1 talking, 2 scared. u = size of one block unit (a person is 27u tall).
static void blocky(uint16_t *c, const Blocky *b, float cx, float foot, float u,
                   float aL, float aR, float lL, float lR, int face, float t) {
    float hip = foot - 9 * u, sh = hip - 10 * u;
    draw_ellipse(c, cx, 124, 6 * u, 1.5f * u, 0, BLACK, 70);
    limb(c, cx - 2.2f * u, hip, cx - 2.2f * u + sinf(lL) * 9 * u, hip + cosf(lL) * 9 * u, 4 * u, b->pants);
    limb(c, cx + 2.2f * u, hip, cx + 2.2f * u + sinf(lR) * 9 * u, hip + cosf(lR) * 9 * u, 4 * u, b->pants);
    rf(c, cx - 4.5f * u, sh, 9 * u, 10 * u, b->shirt);
    rf(c, cx - 4.5f * u, hip - 1.2f * u, 9 * u, 1.2f * u, b->pants);
    limb(c, cx - 5.8f * u, sh + 1.5f * u, cx - 5.8f * u + sinf(aL) * 9 * u, sh + 1.5f * u + cosf(aL) * 9 * u, 3.2f * u, b->skin);
    limb(c, cx + 5.8f * u, sh + 1.5f * u, cx + 5.8f * u + sinf(aR) * 9 * u, sh + 1.5f * u + cosf(aR) * 9 * u, 3.2f * u, b->skin);

    float hx = cx - 4 * u, hy = sh - 8.4f * u;
    rf(c, hx, hy, 8 * u, 8 * u, b->skin);
    if (b->style == 1 || b->style == 3) {
        rf(c, hx - 0.3f * u, hy - 0.8f * u, 8.6f * u, 2.6f * u, b->hair);
        rf(c, hx - 0.3f * u, hy, 1.1f * u, 3.2f * u, b->hair);
        rf(c, hx + 7.2f * u, hy, 1.1f * u, 3.2f * u, b->hair);
    } else if (b->style == 2) {
        rf(c, hx - 0.3f * u, hy - 1.2f * u, 8.6f * u, 3.2f * u, b->shirt);
        rf(c, hx + 4.0f * u, hy + 1.4f * u, 5.0f * u, 1.0f * u, b->shirt);
    }
    float ey = hy + 3.4f * u;
    if (b->style == 3) {
        rf(c, hx + 0.6f * u, ey - 0.6f * u, 6.8f * u, 1.9f * u, 0x101010);
    } else {
        for (int s = -1; s <= 1; s += 2) {
            float ex = cx + s * 1.9f * u;
            if (face == 2) {
                rf(c, ex - 1.0f * u, ey - 1.1f * u, 2.0f * u, 2.6f * u, WHITE);
                rf(c, ex - 0.4f * u, ey - 0.2f * u, 0.9f * u, 1.2f * u, BLACK);
            } else rf(c, ex - 0.55f * u, ey - 0.7f * u, 1.1f * u, 1.5f * u, BLACK);
        }
    }
    float my = hy + 5.8f * u;
    if (face == 2) rf(c, cx - 1.1f * u, my - 0.2f * u, 2.2f * u, 2.2f * u, 0x3A0A10);
    else if (face == 1) rf(c, cx - 1.5f * u, my, 3.0f * u, (0.7f + 1.3f * fabsf(sinf(t * 14.0f))) * u, 0x3A0A10);
    else {
        rf(c, cx - 1.6f * u, my, 3.2f * u, 0.7f * u, BLACK);
        rf(c, cx - 2.3f * u, my - 0.6f * u, 0.7f * u, 0.7f * u, BLACK);
        rf(c, cx + 1.6f * u, my - 0.6f * u, 0.7f * u, 0.7f * u, BLACK);
    }
}

static void ghost(uint16_t *c, float x, float y, float r, float t, bool cute) {
    uint32_t col = 0xDCE8F5;
    draw_ellipse(c, x, y, r * 1.5f, r * 1.5f, 0, 0x8FB0D8, 40);
    draw_ellipse(c, x, y, r, r, 0, col, 255);
    rf(c, x - r, y, 2 * r, r * 0.95f, col);
    for (int i = 0; i < 4; i++)
        draw_ellipse(c, x - r * 0.75f + i * r * 0.5f, y + r * 0.95f + sinf(t * 5 + i * 1.6f) * r * 0.1f,
                     r * 0.27f, r * 0.3f, 0, col, 255);
    draw_ellipse(c, x - r * 0.35f, y - r * 0.15f, r * 0.15f, r * 0.22f, 0, BLACK, 255);
    draw_ellipse(c, x + r * 0.35f, y - r * 0.15f, r * 0.15f, r * 0.22f, 0, BLACK, 255);
    if (cute) {
        draw_line(c, x - r * 0.25f, y + r * 0.3f, x, y + r * 0.45f, 2.0f, BLACK);
        draw_line(c, x, y + r * 0.45f, x + r * 0.25f, y + r * 0.3f, 2.0f, BLACK);
    } else draw_ellipse(c, x, y + r * 0.45f, r * 0.14f, r * 0.2f + sinf(t * 6) * r * 0.04f, 0, BLACK, 255);
}

static void triangle(uint16_t *c, float cx, float top, float w, float h, bool up, uint32_t rgb) {
    int n = (int)h;
    for (int r = 0; r < n; r++) {
        float k = (r + 1.0f) / n;
        rf(c, cx - w * (up ? k : 1.0f - k + 1.0f / n) / 2, top + r, w * (up ? k : 1.0f - k + 1.0f / n), 1, rgb);
    }
}

static void fog(uint16_t *c, float t, int y, uint32_t col, int alpha) {
    for (int i = 0; i < 4; i++) {
        float x = fmodf(i * 110 + t * (8 + i * 3), CW + 160) - 80;
        draw_ellipse(c, x, y + (i % 2) * 8, 80, 12, 0, col, alpha);
    }
}

// Now and then the picture breaks into dark lines, like a bad old tape.
static void glitch(uint16_t *c, uint32_t seed, float t) {
    uint32_t k = (uint32_t)(t * 12.0f);
    if (hash(seed + k * 31u) % 100 > 9) return;
    for (int y = 0; y < CH; y += 6)
        if (hash(seed + k * 7u + (uint32_t)y) % 3 == 0) draw_rect(c, 0, y, CW, 2 + (int)(hash(seed + (uint32_t)y) % 3), BLACK);
}

static void horror_bg(uint16_t *c, uint32_t seed, float t) {
    const int ground = 122;
    for (int y = 0; y < ground; y += 6)
        draw_rect(c, 0, y, CW, 6, 0x03040C + ((y * 26 / ground) << 8) + (y * 36 / ground));
    for (int i = 0; i < 20; i++) {
        int v = 90 + (int)(120 * (0.5f + 0.5f * sinf(t * 2.0f + i * 1.9f)));
        draw_rect(c, (int)(rnd(seed, 600 + i) * 260), 8 + (int)(rnd(seed, 650 + i) * 80), 2, 2, (v << 16) | (v << 8) | v);
    }
    draw_ellipse(c, 70, 40, 30, 30, 0, 0xB8B6A0, 30);
    draw_ellipse(c, 70, 40, 18, 18, 0, 0xEFEBC8, 255);
    draw_ellipse(c, 64, 36, 4, 4, 0, 0xD2CEA6, 255);
    draw_rect(c, 0, ground, CW, CH - ground, 0x0B0E14);
    draw_line(c, 252, ground, 254, 52, 7.0f, 0x06070B);       // a bare tree
    draw_line(c, 253, 80, 232, 62, 4.0f, 0x06070B);
    draw_line(c, 253, 68, 276, 50, 4.0f, 0x06070B);
    draw_line(c, 233, 62, 224, 52, 3.0f, 0x06070B);
    fog(c, t, 118, 0x9AA8C0, 38);
}

/* ---- block stories ---- */

static const Story BSTORIES[] = {
    { "NOOB VS PRO", false, { "I AM THE BEST PLAYER HERE.", "YOU JUST FELL OFF THE MAP.",
        "THE MAP IS TOO SMALL.", "LOOK. I AM FLYING!" } },
    { "BLOCK SCHOOL", true, { "DID YOU DO THE HOMEWORK.", "WHAT HOMEWORK.",
        "THE ONE DUE TODAY.", "WE ARE DOOMED. RUN!" } },
    { "BLOCK PIZZA", false, { "I ORDERED PIZZA FOR US.", "YAY. WHAT TOPPINGS.",
        "ONLY PINEAPPLE.", "THAT IS WHY I LOVE YOU!" } },
    { "OBBY RAGE", true, { "I AM ALMOST AT THE END.", "DO NOT LOOK DOWN.",
        "I LOOKED DOWN.", "AND I RESPAWNED AGAIN!" } },
    { "BLOCK BFF", false, { "WILL YOU BE MY FRIEND.", "WE ARE ALREADY FRIENDS.",
        "SINCE WHEN.", "SINCE YOU SHARED YOUR PIZZA!" } },
    { "NEW MAYOR", false, { "I AM THE NEW MAYOR OF THIS TOWN.", "WHAT WILL YOU CHANGE.",
        "MORE SNACKS FOR EVERYONE.", "BEST MAYOR EVER!" } },
    { "SPEEDRUN", true, { "I WILL FINISH IN ONE MINUTE.", "THAT IS IMPOSSIBLE.",
        "WATCH ME. 3. 2. 1. GO!", "I FORGOT TO PRESS START!" } },
    { "BLOCK LAG", true, { "WHY IS EVERYTHING SO SLOW.", "MY WIFI IS BAD.",
        "MINE TOO. WE ARE ONE PING.", "FROZEN TOGETHER FOREVER!" } },
    { "SNACK THIEF", true, { "WHO TOOK MY COOKIES.", "NOT ME. I WAS SLEEPING.",
        "YOUR FACE HAS CRUMBS.", "OKAY. I NEED A HUG!" } },
    { "BLOCK TALENT SHOW", false, { "I WILL DO A MAGIC TRICK.", "OOH. SHOW ME!",
        "NOW YOU SEE MY LUNCH.", "NOW YOU DO NOT. OH NO!" } },
    { "LARO TAYO", true, { "ALAM MO BA MAY BAGONG LARO.", "TALAGA. ANONG LARO.",
        "SUBUKAN NATIN NGAYON.", "HALA. LOW BATTERY NA!" } },
    { "OBBY NG BAYAN", true, { "PAGOD NA AKO SA OBBY.", "KAYA MO YAN. KONTI NA LANG.",
        "TEKA. NAHULOG AKO ULIT.", "SIGE. ULIT TAYO!" } }
};
#define N_BSTORIES AL(BSTORIES)

static const Story HSTORIES[] = {
    { "THE HAUNTED HOUSE", true, { "THIS HOUSE IS SUPPOSED TO BE HAUNTED.", "THAT IS JUST A STORY.",
        "THEN WHO IS WAVING AT US.", "RUN RUN RUN!" } },
    { "THE NIGHT SHIFT", true, { "THE LIGHTS KEEP FLICKERING.", "IT IS PROBABLY THE WIRES.",
        "WIRES DO NOT WHISPER MY NAME.", "WE ARE LEAVING NOW!" } },
    { "DO NOT LOOK BEHIND", false, { "DO NOT LOOK BEHIND YOU.", "WHY. WHAT IS THERE.",
        "A GHOST WITH A SMALL HAT.", "OH. HE IS ACTUALLY CUTE." } },
    { "THE DARK FOREST", false, { "WHY IS THE FOREST SO QUIET.", "I SEE TWO GLOWING EYES.",
        "THEY ARE GETTING CLOSER.", "IT IS ONLY AN OWL!" } },
    { "SCARY SLEEPOVER", true, { "LET US TELL SCARY STORIES.", "I KNOW ONE ABOUT A ZOMBIE.",
        "THE ZOMBIE IS BEHIND YOU.", "AAAH! OH. IT IS MY BROTHER!" } },
    { "THE BASEMENT", false, { "I HEARD A NOISE IN THE BASEMENT.", "DO NOT GO DOWN THERE.",
        "I AM GOING ANYWAY.", "NEVER MIND. IT IS THE CAT!" } },
    { "THE MIRROR", true, { "THE MIRROR IS MOVING.", "STOP. YOU ARE SCARING ME.",
        "I AM JUST WAVING.", "THAT WAS NOT YOUR REFLECTION!" } },
    { "MULTO SA BAHAY", true, { "MAY MULTO DAW SA BAHAY NA ITO.", "HUWAG MO AKONG TAKUTIN.",
        "MAY NAGBUBULONG SA LIKOD MO.", "TAKBO NA. ALIS TAYO!" } }
};
#define N_HSTORIES AL(HSTORIES)

static void draw_block_story(uint16_t *c, uint32_t seed, unsigned long long age, bool horror) {
    const Story *st = horror ? &HSTORIES[hash(seed + 4) % N_HSTORIES] : &BSTORIES[hash(seed + 4) % N_BSTORIES];
    float t = age / 1000.0f;
    int line = (int)((age / LINE_MS) % 4);
    unsigned lt = (unsigned)(age % LINE_MS);
    if (horror) horror_bg(c, seed, t);
    else story_background(c, (int)(hash(seed + 3) % N_SCENES), seed, t);

    const char *text = st->line[line];
    int shown = (int)(lt * 0.022f), total = (int)strlen(text);
    bool speaking = shown < total + 6 && lt < LINE_MS - 300;
    bool react = st->shock && line == 3, happy = !st->shock && line == 3;

    if (horror) {                        // a ghost drifts about behind them and comes closer
        float near = line >= 2 ? (line - 1 + lt / (float)LINE_MS) * 0.5f : 0.0f;
        ghost(c, 150 + sinf(t * 0.9f) * 70, 62 + sinf(t * 1.7f) * 8, 14 + near * 16, t, happy);
    }
    Blocky p[2] = { make_blocky(seed, 1), make_blocky(seed, 2) };
    for (int i = 0; i < 2; i++) {
        bool talks = speaking && line % 2 == i;
        float x = i ? 205.0f : 85.0f, foot = 124 - (talks ? fabsf(sinf(t * 9.0f)) * 3 : 0);
        float aL = -0.12f + sinf(t * 2 + i) * 0.05f, aR = 0.12f - sinf(t * 2 + i) * 0.05f, lL = -0.05f, lR = 0.05f;
        int face = talks ? 1 : 0;
        if (talks) { if (i) aL = -2.1f - sinf(t * 8.0f) * 0.4f; else aR = 2.1f + sinf(t * 8.0f) * 0.4f; }
        if (horror && !react && !happy) { x += sinf(t * 25.0f + i) * 0.8f; if (!talks && line > 0) face = 2; }
        if (react) { x += sinf(t * 40.0f) * 2; aL = -2.7f; aR = 2.7f; lL = -0.3f; lR = 0.3f; face = 2; }
        else if (happy) { foot -= fabsf(sinf(t * 12.0f + i)) * 9; aL = -2.5f; aR = 2.5f; face = 0; }
        blocky(c, &p[i], x, foot, 2.5f, aL, aR, lL, lR, face, t);
    }
    if (horror) glitch(c, seed, t);
    story_subtitle(c, text, shown);
}

/* ---- slow-mo run and block dance ---- */

static const char *const RUN_TAGS[] = { "MAIN CHARACTER ENERGY", "SLOW MO RUN", "NOTHING CAN STOP ME", "WATCH THIS JUMP" };

static void draw_run(uint16_t *c, uint32_t seed, unsigned long long age) {
    float t = age / 1000.0f, p = (age % RUN_MS) / (float)RUN_MS;
    story_background(c, hash(seed + 3) % 2 ? 1 : 2, seed, t);
    Blocky b = make_blocky(seed, 1);
    b.style = 3;                         // sunglasses
    float x, foot = 124, aL, aR, lL, lR, ph = t * 3.2f;
    if (p < 0.62f) {                     // running, in slow motion
        x = 30 + p / 0.62f * 150;
        lL = sinf(ph) * 0.9f; lR = -lL;
        aL = lL * 0.9f; aR = lR * 0.9f;
        foot -= fabsf(sinf(ph)) * 4;
        for (int i = 0; i < 5; i++) {    // speed lines
            float ly = 70 + i * 11, lx = x - 38 - fmodf(t * 70 + i * 23, 40);
            draw_line(c, lx, ly, lx + 22, ly, 2.0f, 0xFFFFFF);
        }
    } else if (p < 0.82f) {              // the jump kick
        float q = (p - 0.62f) / 0.20f;
        x = 180 + q * 55; foot = 124 - sinf(q * PI_F) * 52;
        lL = 0.25f; lR = 1.45f; aL = -1.3f; aR = -0.6f;
        for (int i = 0; i < 4; i++) draw_line(c, x - 30 - i * 6, foot - 10 - i * 9, x - 56 - i * 6, foot - 10 - i * 9, 2.0f, 0xFFFFFF);
    } else {                             // a cool pose
        x = 235; lL = -0.1f; lR = 0.1f; aL = -0.2f; aR = 2.4f + sinf(t * 8.0f) * 0.3f;
    }
    blocky(c, &b, x, foot, 2.6f, aL, aR, lL, lR, 0, t);
    const char *tag = RUN_TAGS[hash(seed + 12) % AL(RUN_TAGS)];
    label(c, 146 - text_width(tag, 2) / 2, 142, 2, tag, fmodf(t, 1.0f) < 0.5f ? YELLOW : WHITE);
}

static const char *const BDANCE_TAGS[] = { "BLOCK PARTY!", "EMOTE TIME!", "DANCE BREAK!", "EVERYBODY MOVE!" };

static void draw_block_dance(uint16_t *c, uint32_t seed, unsigned long long age) {
    float t = age / 1000.0f;
    story_background(c, hash(seed + 3) % 2 ? 8 : 2, seed, t);
    for (int i = 0; i < 6; i++) {        // notes
        float y = 112 - fmodf(t * 28 + i * 22, 100), x = 40 + i * 40 + sinf(t * 2 + i) * 8;
        uint32_t col = hsv(i * 0.17f + t * 0.2f, 0.7f, 1.0f);
        draw_ellipse(c, x, y, 4, 3, -0.4f, col, 255);
        draw_line(c, x + 3, y, x + 3, y - 12, 2.0f, col);
    }
    for (int i = 0; i < 3; i++) {
        Blocky b = make_blocky(seed, (uint32_t)i + 1);
        float ph = t * 6.0f + i * 1.3f, foot = 124 - fabsf(sinf(ph)) * 10;
        float aL = -1.6f + sinf(ph) * 1.1f, aR = 1.6f - sinf(ph) * 1.1f;
        float lL = -0.35f - sinf(ph) * 0.25f, lR = 0.35f + sinf(ph) * 0.25f;
        blocky(c, &b, 70 + i * 80, foot, 2.3f, aL, aR, lL, lR, 0, t);
    }
    const char *tag = BDANCE_TAGS[hash(seed + 13) % AL(BDANCE_TAGS)];
    label(c, 146 - text_width(tag, 2) / 2, 142, 2, tag, fabsf(sinf(t * 3.0f)) > 0.5f ? YELLOW : WHITE);
}

/* ---- in this town: paper puppets on a stage ---- */

static const Story TSTORIES[] = {
    { "IN THIS TOWN", false, { "IN THIS TOWN THE MAILMAN IS A CAT.", "HE IS NEVER ON TIME.",
        "HE NAPS ON EVERY PORCH.", "NOBODY CARES. HE IS TOO CUTE!" } },
    { "IN THIS TOWN", false, { "IN THIS TOWN THERE IS A BAKERY.", "IT SELLS ONLY ONE COOKIE.",
        "IT IS THE SIZE OF A TABLE.", "EVERYONE SHARES. SO NICE!" } },
    { "IN THIS TOWN", false, { "IN THIS TOWN IT RAINS CONFETTI.", "THE KIDS ALWAYS CHEER.",
        "THE MAYOR HAS TO SWEEP IT UP.", "SHE LOVES HER JOB ANYWAY!" } },
    { "IN THIS TOWN", true, { "IN THIS TOWN THE LIGHTS GO OUT AT NIGHT.", "SOMEONE KEEPS KNOCKING ON MY DOOR.",
        "IT IS ONLY THE WIND.", "OR IS IT. JUST KIDDING!" } },
    { "IN THIS TOWN", true, { "IN THIS TOWN EVERYONE HAS A SECRET.", "MINE IS THAT I CANNOT SWIM.",
        "I LIVE NEXT TO THE LAKE.", "OOPS. NOW YOU ALL KNOW!" } },
    { "IN THIS TOWN", false, { "IN THIS TOWN THE BUS IS ALWAYS LATE.", "SO WE BUILT OUR OWN.",
        "IT IS A SHOPPING CART.", "BEST RIDE EVER!" } },
    { "IN THIS TOWN", true, { "IN THIS TOWN THE DOGS RUN THE SCHOOL.", "THE TEACHER IS A POODLE.",
        "HOMEWORK IS TO FETCH STICKS.", "I GOT AN A. WOOF!" } },
    { "SA BAYAN NAMIN", false, { "SA BAYAN NAMIN MAY MANOK NA KUMAKANTA.", "TALAGA. ANONG KANTA.",
        "ANG KANTA NG UMAGA.", "KAYA GISING NA LAHAT!" } }
};
#define N_TSTORIES AL(TSTORIES)

static void paper_puppet(uint16_t *c, float x, float foot, float sway, uint32_t col, int hat, bool talk, float t) {
    float bx = x + sway * 0.5f, hx = x + sway * 1.3f, hy = foot - 48;
    limb(c, x, foot, bx, foot - 34, 21, BLACK);                   // black paper edge
    limb(c, x, foot, bx, foot - 34, 17, col);
    limb(c, bx - 8, foot - 28, bx - 17, foot - 18, 7, BLACK);     // arms
    limb(c, bx - 8, foot - 28, bx - 17, foot - 18, 4, col);
    limb(c, bx + 8, foot - 28, bx + 17, foot - 18, 7, BLACK);
    limb(c, bx + 8, foot - 28, bx + 17, foot - 18, 4, col);
    draw_ellipse(c, hx, hy, 13, 13, 0, BLACK, 255);
    draw_ellipse(c, hx, hy, 11, 11, 0, 0xFFE3C0, 255);
    if (hat == 1) triangle(c, hx, hy - 24, 18, 14, true, 0xE8503A);
    else if (hat == 2) { rf(c, hx - 11, hy - 14, 22, 5, 0x333333); rf(c, hx - 7, hy - 22, 14, 9, 0x333333); }
    rf(c, hx - 5, hy - 3, 2, 3, BLACK);
    rf(c, hx + 3, hy - 3, 2, 3, BLACK);
    if (talk) draw_ellipse(c, hx, hy + 5, 3, 1.5f + fabsf(sinf(t * 14.0f)) * 2.5f, 0, 0x7A1020, 255);
    else rf(c, hx - 3, hy + 5, 6, 1, BLACK);
}

static void draw_town(uint16_t *c, uint32_t seed, unsigned long long age) {
    const Story *st = &TSTORIES[hash(seed + 4) % N_TSTORIES];
    float t = age / 1000.0f, hue = rnd(seed, 1);
    int line = (int)((age / LINE_MS) % 4);
    unsigned lt = (unsigned)(age % LINE_MS);
    bool spooky = st->shock && line >= 1 && line < 3;
    for (int y = 0; y < 122; y += 6)
        draw_rect(c, 0, y, CW, 6, spooky ? hsv(0.72f, 0.5f, 0.35f - 0.12f * y / 122) : hsv(0.05f + hue * 0.08f, 0.45f, 0.98f - 0.35f * y / 122));
    draw_ellipse(c, 60 + (float)(hash(seed + 2) % 160), 40, 16, 16, 0, spooky ? 0xE8E4C8 : 0xFFF2B0, 255);
    for (int layer = 0; layer < 2; layer++)                       // two rows of paper houses
        for (int i = 0; i < 7; i++) {
            uint32_t sd = seed + (uint32_t)(i + layer * 10);
            float w = 34 + rnd(sd, 40) * 20, x = i * 48 + layer * 24 + sinf(t * 0.8f + i) * (2 + layer * 2) - 10;
            float h = 26 + rnd(sd, 60) * 26 + layer * 10, base = 112 + layer * 6;
            rf(c, x, base - h, w, h, layer ? 0xC98B5B : 0x9C7A5C);
            triangle(c, x + w / 2, base - h - 14, w + 8, 14, true, layer ? 0xA14B3A : 0x7A3E34);
            bool lit = hash(sd + (uint32_t)(t * 0.7f)) % 100 < (spooky ? 25 : 70);
            rf(c, x + w * 0.3f, base - h * 0.6f, 7, 9, lit ? 0xFFE08A : 0x4A3A30);
        }
    rf(c, 0, 112, CW, 12, 0x8B5A2B);                              // the stage floor
    for (int x = 0; x < CW; x += 16) draw_rect(c, x, 124, 16, CH - 124, (x / 16) & 1 ? 0x7A1020 : 0x8F1830);
    rf(c, 0, 0, CW, 9, 0x8F1830);                                 // curtain on top and at the sides
    for (int x = 6; x < CW + 12; x += 24) draw_ellipse(c, x, 9, 12, 6, 0, 0x8F1830, 255);
    rf(c, 0, 0, 12, 124, 0x7A1020);
    rf(c, 0, 0, 12, 124, 0x7A1020);

    int active = line % 3;
    static const uint32_t PCOL[3] = { 0x3E7CD8, 0xE0A030, 0x58B060 };
    for (int i = 0; i < 3; i++) {
        float sway = sinf(t * 2.2f + i * 1.7f) * 5, foot = 118;
        bool talk = i == active && lt < LINE_MS - 300;
        if (talk) { foot -= fabsf(sinf(t * 9.0f)) * 4; sway *= 1.6f; }
        if (st->shock && line == 3) sway = sinf(t * 30.0f + i) * 3;
        paper_puppet(c, 62 + i * 78, foot, sway, PCOL[i], (int)(hash(seed + 20 + (uint32_t)i) % 3), talk, t);
    }
    story_subtitle(c, st->line[line], (int)(lt * 0.022f));
}

/* ---- horror clips ---- */

static void hclip_house(uint16_t *c, uint32_t seed, float t) {
    for (int y = 0; y < 175; y += 6) draw_rect(c, 0, y, CW, 6, 0x10061C + ((y * 20 / 175) << 16) + ((y * 14 / 175) << 8));
    float ph = fmodf(t, 4.5f);
    if (ph < 0.12f) {                    // lightning
        for (int y = 0; y < 175; y += 6) draw_rect(c, 0, y, CW, 6, 0x9AA0C8);
        draw_line(c, 60, 0, 52, 40, 3.0f, WHITE);
        draw_line(c, 52, 40, 64, 44, 3.0f, WHITE);
        draw_line(c, 64, 44, 50, 90, 3.0f, WHITE);
    }
    draw_ellipse(c, 250, 44, 40, 40, 0, 0xB8B6A0, 30);
    draw_ellipse(c, 250, 44, 24, 24, 0, 0xEFEBC8, 255);
    draw_rect(c, 0, 175, CW, CH - 175, 0x08090E);
    rf(c, 80, 96, 150, 80, 0x0A0A12);                             // the house
    triangle(c, 155, 62, 170, 34, true, 0x0A0A12);
    rf(c, 200, 70, 24, 40, 0x0A0A12);                             // tower
    triangle(c, 212, 46, 32, 24, true, 0x0A0A12);
    static const int WX[4] = { 98, 138, 178, 212 }, WY[4] = { 112, 112, 112, 84 };
    for (int i = 0; i < 4; i++) {
        bool on = hash(seed + (uint32_t)((int)(t * 6) * 13 + i)) % 100 < 58;
        rf(c, (float)WX[i], (float)WY[i], 14, 20, on ? 0xFFD060 : 0x15151C);
        if (on && i == 2) { rf(c, 183, 120, 3, 8, 0x0A0A12); rf(c, 188, 118, 3, 10, 0x0A0A12); }   // a shape in the window
    }
    rf(c, 148, 142, 18, 34, 0x14110E);
    ghost(c, 150 + sinf(t * 0.9f) * 100, 120 + sinf(t * 1.7f) * 14 - 30, 15, t, false);
    for (int i = 0; i < 5; i++) {        // bats
        float x = fmodf(t * (34 + i * 7) + i * 70, CW + 40) - 20, y = 24 + i * 14 + sinf(t * 3 + i) * 7;
        float fl = sinf(t * 18.0f + i) * 5;
        draw_line(c, x - 9, y - fl, x, y, 2.0f, 0x050508);
        draw_line(c, x + 9, y - fl, x, y, 2.0f, 0x050508);
        draw_ellipse(c, x, y + 1, 3, 3, 0, 0x050508, 255);
    }
    fog(c, t, 170, 0x8090B0, 40);
}

static void hclip_forest(uint16_t *c, uint32_t seed, float t) {
    draw_rect(c, 0, 0, CW, CH, 0x020305);
    draw_ellipse(c, 160, 110, 150, 100, 0, 0x101A18, 120);
    for (int i = 0; i < 8; i++) {
        float x = i * 42 + rnd(seed, 10 + i) * 20, w = 10 + rnd(seed, 20 + i) * 14;
        rf(c, x, 0, w, CH, 0x07090A);
        rf(c, x, 0, 2, CH, 0x10181A);
    }
    for (int i = 0; i < 6; i++) {        // eyes in the dark
        float cyc = t * 0.28f + rnd(seed, 30 + i), ph = cyc - floorf(cyc);
        uint32_t sd = seed + (uint32_t)floorf(cyc) * 17u + (uint32_t)i;
        if (ph > 0.65f) continue;
        float x = 30 + rnd(sd, 40) * 230, y = 50 + rnd(sd, 41) * 100, s = 1.0f + ph * 1.6f;
        bool blink = fmodf(t * 1.3f + i, 3.0f) < 0.15f;
        uint32_t col = hash(sd) & 1 ? 0xFF3A2A : 0xD8FF5A;
        for (int e = -1; e <= 1; e += 2) {
            draw_ellipse(c, x + e * 6 * s, y, 8 * s, 6 * s, 0, col, 40);
            draw_ellipse(c, x + e * 6 * s, y, 3.2f * s, blink ? 0.6f : 2.4f * s, 0, col, 255);
        }
    }
    float a = sinf(t * 0.8f) * 0.55f;    // flashlight beam from the bottom
    float dx = sinf(a), dy = -cosf(a);
    draw_ellipse(c, 160 + dx * 100, 240 + dy * 100, 120, 36, atan2f(dy, dx), 0xFFF3B0, 38);
    draw_ellipse(c, 160 + dx * 100, 240 + dy * 100, 120, 20, atan2f(dy, dx), 0xFFF3B0, 38);
    fog(c, t, 200, 0x8AA0A0, 36);
    glitch(c, seed, t);
}

static void hclip_grave(uint16_t *c, uint32_t seed, float t) {
    (void)seed;
    for (int y = 0; y < 150; y += 6) draw_rect(c, 0, y, CW, 6, 0x120A2A + ((y * 24 / 150) << 8) + ((y * 30 / 150) << 16));
    draw_ellipse(c, 220, 56, 46, 46, 0, 0xC8C0A0, 30);
    draw_ellipse(c, 220, 56, 30, 30, 0, 0xF2EDC8, 255);
    draw_rect(c, 0, 150, CW, CH - 150, 0x0A0F0C);
    draw_ellipse(c, 60, 156, 120, 22, 0, 0x0A0F0C, 255);
    for (int i = 0; i < 5; i++) {
        float x = 36 + i * 58, y = 170 + (i % 2) * 18, w = 28;
        rf(c, x - w / 2, y - 26, w, 26, 0x4A4F58);
        draw_ellipse(c, x, y - 26, w / 2, 11, 0, 0x4A4F58, 255);
        rf(c, x - 1, y - 30, 2, 14, 0x2A2D33);
        rf(c, x - 6, y - 25, 12, 2, 0x2A2D33);
        float up = fmaxf(0.0f, sinf(t * 1.1f + i * 1.7f));        // ghosts rise out of the ground
        if (up > 0.2f) ghost(c, x + sinf(t * 2 + i) * 6, y - 20 - up * 46, 10 + up * 4, t + i, i % 2 == 0);
    }
    fog(c, t, 190, 0x9AB0A8, 42);
    for (int i = 0; i < 4; i++) {
        float x = fmodf(t * (30 + i * 9) + i * 90, CW + 40) - 20, y = 30 + i * 16 + sinf(t * 3 + i) * 6, fl = sinf(t * 18.0f + i) * 5;
        draw_line(c, x - 9, y - fl, x, y, 2.0f, 0x050508);
        draw_line(c, x + 9, y - fl, x, y, 2.0f, 0x050508);
    }
}

static void hclip_pumpkin(uint16_t *c, uint32_t seed, float t) {
    float fl = 0.8f + 0.2f * sinf(t * 17.0f) + (hash(seed + (uint32_t)(t * 10)) % 3) * 0.04f;
    for (int y = 0; y < CH; y += 8) draw_rect(c, 0, y, CW, 8, 0x0A0508 + ((y * 12 / CH) << 16));
    draw_ellipse(c, 146, 120, 190 * fl, 130 * fl, 0, 0xFF8A1E, 22);
    draw_ellipse(c, 146, 120, 140 * fl, 100 * fl, 0, 0xFF8A1E, 26);
    draw_ellipse(c, 146, 126, 82, 68, 0, 0xB8480A, 255);
    for (int s = -1; s <= 1; s += 2) draw_ellipse(c, 146 + s * 48, 126, 40, 66, 0, 0xD05C0E, 255);
    draw_ellipse(c, 146, 126, 40, 68, 0, 0xE8710A, 255);
    rf(c, 140, 52, 12, 18, 0x4A6B1E);
    triangle(c, 112, 100, 26, 24, true, 0xFFE45E);                // eyes
    triangle(c, 180, 100, 26, 24, true, 0xFFE45E);
    triangle(c, 146, 126, 12, 12, true, 0xFFE45E);                // nose
    draw_ellipse(c, 146, 156, 44, 18, 0, 0xFFE45E, 255);          // grin
    draw_rect(c, 100, 136, 92, 12, 0xE8710A);
    for (int i = 0; i < 5; i++) {
        rf(c, 112 + i * 16, 148, 8, 7, 0xE8710A);
        rf(c, 120 + i * 16 - ((i + 1) % 2) * 0, 162, 8, 7, 0xE8710A);
    }
    if (fl < 0.72f) draw_ellipse(c, 146, 126, 82, 68, 0, BLACK, 90);   // the candle flickers
    for (int i = 0; i < 4; i++) {
        float x = fmodf(t * (26 + i * 8) + i * 80, CW + 40) - 20, y = 24 + i * 18 + sinf(t * 3 + i) * 6, f2 = sinf(t * 18.0f + i) * 5;
        draw_line(c, x - 9, y - f2, x, y, 2.0f, 0x050508);
        draw_line(c, x + 9, y - f2, x, y, 2.0f, 0x050508);
    }
}

static void draw_hclip(uint16_t *c, uint32_t seed, float t) {
    switch (hash(seed + 21) % 4) {
    case 0: hclip_house(c, seed, t); break;
    case 1: hclip_forest(c, seed, t); break;
    case 2: hclip_grave(c, seed, t); break;
    default: hclip_pumpkin(c, seed, t); break;
    }
}

/* ---- who made it, what it says, how long it lasts ---- */

static const char *const BLOCK_CREATORS[] = {
    "BLOCK.TALES", "NOOB.NATION", "OBBY.KING", "PIXEL.MAYOR", "BLOXY.BEE", "CUBE.CLUB"
};
static const char *const TOWN_CREATORS[] = {
    "TOWN.TALES.TV", "PAPER.PUPPETS", "LITTLE.TOWN", "STAGE.STORIES"
};
static const char *const SPOOKY_CREATORS[] = {
    "SPOOKY.NIGHTS", "GHOST.CLUB", "CREEPY.CORNER", "MIDNIGHT.TALES", "HAUNT.HQ", "BOO.CREW"
};
static const char *const RUN_CAPS[] = { "SLOW MO RUN", "MAIN CHARACTER MOMENT", "THE EPIC JUMP KICK", "WHEN YOUR SONG COMES ON" };
static const char *const BDANCE_CAPS[] = { "BLOCK DANCE PARTY", "DANCE BATTLE", "EMOTE PARTY", "LAST ONE DANCING WINS" };
static const char *const HCLIP_CAPS[4] = { "WHAT IS IN THE WINDOW", "DO NOT GO IN THE FOREST", "NIGHT AT THE GRAVEYARD", "SPOOKY PUMPKIN SEASON" };

static const char *creator_of(uint32_t seed, int kind) {
    switch (kind) {
    case VK_FRUIT: case VK_DANCE: return FRUIT_CREATORS[hash(seed + 8) % N_FRUIT_CREATORS];
    case VK_BLOCK: case VK_RUN: case VK_BDANCE: return BLOCK_CREATORS[hash(seed + 8) % AL(BLOCK_CREATORS)];
    case VK_TOWN: return TOWN_CREATORS[hash(seed + 8) % AL(TOWN_CREATORS)];
    case VK_HSTORY: case VK_HCLIP: return SPOOKY_CREATORS[hash(seed + 8) % AL(SPOOKY_CREATORS)];
    default: return CREATORS[hash(seed + 5) % N_CREATORS];
    }
}

static void caption_of(char *out, size_t len, uint32_t seed, int kind) {
    int part = 1 + (int)(hash(seed + 10) % 9);
    switch (kind) {
    case VK_FRUIT: snprintf(out, len, "%s PART %d", STORIES[hash(seed + 4) % N_STORIES].title, part); break;
    case VK_DANCE: snprintf(out, len, "%s", DANCE_CAPS[hash(seed + 9) % 4]); break;
    case VK_BLOCK: snprintf(out, len, "%s PART %d", BSTORIES[hash(seed + 4) % N_BSTORIES].title, part); break;
    case VK_RUN: snprintf(out, len, "%s", RUN_CAPS[hash(seed + 9) % AL(RUN_CAPS)]); break;
    case VK_BDANCE: snprintf(out, len, "%s", BDANCE_CAPS[hash(seed + 9) % AL(BDANCE_CAPS)]); break;
    case VK_TOWN: snprintf(out, len, "%s PART %d", TSTORIES[hash(seed + 4) % N_TSTORIES].title, part); break;
    case VK_HSTORY: snprintf(out, len, "%s PART %d", HSTORIES[hash(seed + 4) % N_HSTORIES].title, part); break;
    case VK_HCLIP: snprintf(out, len, "%s", HCLIP_CAPS[hash(seed + 21) % 4]); break;
    default: snprintf(out, len, "%s", CAPTIONS[hash(seed + 9) % N_CAPTIONS]); break;
    }
}

static unsigned video_len(int kind) {
    switch (kind) {
    case VK_FRUIT: case VK_BLOCK: case VK_TOWN: case VK_HSTORY: return STORY_MS;
    case VK_DANCE: case VK_BDANCE: return DANCE_MS;
    case VK_RUN: return RUN_MS;
    case VK_HCLIP: return HCLIP_MS;
    default: return LOOP_MS;
    }
}

static void draw_video(uint16_t *c, uint32_t seed, int kind, unsigned long long a) {
    switch (kind) {
    case VK_FRUIT: draw_story(c, seed, a); break;
    case VK_DANCE: draw_dance(c, seed, a); break;
    case VK_BLOCK: draw_block_story(c, seed, a, false); break;
    case VK_RUN: draw_run(c, seed, a); break;
    case VK_BDANCE: draw_block_dance(c, seed, a); break;
    case VK_TOWN: draw_town(c, seed, a); break;
    case VK_HSTORY: draw_block_story(c, seed, a, true); break;
    case VK_HCLIP: draw_hclip(c, seed, a / 1000.0f); break;
    default: draw_clip(c, seed, a / 1000.0f); break;
    }
}

static int video_sig(uint32_t seed) {    // which exact video: used to avoid two of the same in a row
    switch (vkind(seed)) {
    case VK_FRUIT: return 100 + (int)(hash(seed + 4) % N_STORIES);
    case VK_BLOCK: return 300 + (int)(hash(seed + 4) % N_BSTORIES);
    case VK_TOWN: return 400 + (int)(hash(seed + 4) % N_TSTORIES);
    case VK_HSTORY: return 500 + (int)(hash(seed + 4) % N_HSTORIES);
    case VK_HCLIP: return 600 + (int)(hash(seed + 21) % 4);
    case VK_CLIP: return (int)(hash(seed) % SP_KINDS);
    default: return 50 + vkind(seed);
    }
}

/* ---------------- history and input ---------------- */

static void add_video(Spotlight *s, unsigned long long nowMs) {
    if (s->count == SP_HISTORY) {        // forget the oldest
        memmove(s->seed, s->seed + 1, sizeof(s->seed) - sizeof(s->seed[0]));
        memmove(s->liked, s->liked + 1, sizeof(s->liked) - sizeof(s->liked[0]));
        s->count--;
    }
    uint32_t seed = next_rng(s) | 1;
    for (int tries = 0; tries < 16 && s->count > 0 &&
                         (vkind(seed) == vkind(s->seed[s->count - 1]) || video_sig(seed) == video_sig(s->seed[s->count - 1])); tries++)
        seed = next_rng(s) | 1;
    s->seed[s->count] = seed;
    s->liked[s->count] = false;
    s->pos = s->count++;
    s->startMs = nowMs;
}

void spotlight_open(Spotlight *s, unsigned long long nowMs) {
    memset(s, 0, sizeof(*s));
    s->rng = (uint32_t)(nowMs * 2654435761ull) ^ 0x9E3779B9u;
    if (!s->rng) s->rng = 1;
    for (int i = 0; i < 4; i++) next_rng(s);
    add_video(s, nowMs);
    s->lastMs = nowMs;
}

static void go_next(Spotlight *s, unsigned long long now) {
    if (s->pos + 1 < s->count) { s->pos++; s->startMs = now; }
    else add_video(s, now);
}

static void go_prev(Spotlight *s, unsigned long long now) {
    if (s->pos > 0) { s->pos--; s->startMs = now; }
}

static void like(Spotlight *s, unsigned long long now) {
    s->liked[s->pos] = !s->liked[s->pos];
    if (s->liked[s->pos]) s->heartUntil = now + 700;
}

bool spotlight_update(Spotlight *s, const SpotInput *in) {
    int x = in->x, y = in->y;
    s->lastMs = in->ms;
    if ((in->keys & SP_KEY_B) || (in->down && y < 38 && x < 48) || (in->down && y >= 180)) return true;
    if (in->keys & (SP_KEY_A | SP_KEY_RIGHT)) go_next(s, in->ms);
    else if (in->keys & SP_KEY_LEFT) go_prev(s, in->ms);
    if (in->keys & SP_KEY_X) like(s, in->ms);
    if (in->down && y >= 60 && y < 140) {
        if (x < 107) go_prev(s, in->ms);
        else if (x >= 213) go_next(s, in->ms);
        else like(s, in->ms);
    }
    return false;
}

/* ---------------- drawing ---------------- */

static void disc(uint16_t *c, float x, float y, float r, uint32_t rgb) {
    draw_ellipse(c, x, y, r, r, 0, rgb, 255);
}

static void centred(uint16_t *c, int cx, int y, int scale, const char *t, uint32_t rgb) {
    draw_text(c, cx - text_width(t, scale) / 2, y, scale, t, rgb);
}

// text on a black label so it can be read on any picture
static void label(uint16_t *c, int x, int y, int scale, const char *t, uint32_t rgb) {
    draw_rect(c, x - 4, y - 3, text_width(t, scale) + 8, 7 * scale + 6, BLACK);
    draw_text(c, x, y, scale, t, rgb);
}

static void heart(uint16_t *c, float x, float y, float size, uint32_t rgb) {
    float r = size * 0.28f;
    disc(c, x - r * 0.95f, y - size * 0.12f, r, rgb);
    disc(c, x + r * 0.95f, y - size * 0.12f, r, rgb);
    float top = y - size * 0.1f, h = size * 0.62f;               // the point, row by row
    for (int j = 0; j <= (int)h; j++) {
        float half = size * 0.5f * (1.0f - j / h);
        draw_rect(c, (int)(x - half), (int)(top + j), (int)(half * 2) + 1, 1, rgb);
    }
}

static void count_text(char *out, size_t len, int n) {
    if (n >= 1000) snprintf(out, len, "%d.%dK", n / 1000, (n % 1000) / 100);
    else snprintf(out, len, "%d", n);
}

static void draw_top(const Spotlight *s, uint16_t *top, unsigned long long now) {
    uint32_t seed = s->seed[s->pos];
    unsigned long long age = now > s->startMs ? now - s->startMs : 0;
    int kind = vkind(seed);
    unsigned loop = video_len(kind);
    draw_video(top, seed, kind, age % loop);

    // header
    centred(top, CW / 2, 8, 2, "SPOTLIGHT", WHITE);

    // right side buttons: heart, comment, share, each with its count
    int likes = 800 + (int)(rnd(seed, 900) * 88000), coms = 30 + (int)(rnd(seed, 901) * 3000),
        shares = 100 + (int)(rnd(seed, 902) * 40000);
    if (s->liked[s->pos]) likes++;
    char n[12];
    if (s->liked[s->pos]) heart(top, 296, 98, 22, RED);
    else heart(top, 296, 98, 22, WHITE);
    count_text(n, sizeof(n), likes);
    label(top, 296 - text_width(n, 1) / 2, 118, 1, n, WHITE);

    draw_rect(top, 287, 141, 18, 13, WHITE);                      // comment bubble
    draw_line(top, 291, 154, 289, 161, 3.0f, WHITE);
    draw_rect(top, 291, 145, 10, 2, BLACK);
    draw_rect(top, 291, 149, 6, 2, BLACK);
    count_text(n, sizeof(n), coms);
    label(top, 296 - text_width(n, 1) / 2, 166, 1, n, WHITE);

    draw_line(top, 287, 196, 296, 186, 3.0f, WHITE);              // share arrow
    draw_line(top, 296, 186, 305, 196, 3.0f, WHITE);
    draw_line(top, 296, 186, 296, 203, 3.0f, WHITE);
    count_text(n, sizeof(n), shares);
    label(top, 296 - text_width(n, 1) / 2, 214, 1, n, WHITE);

    // creator, caption and sound
    const char *who = creator_of(seed, kind);
    disc(top, 22, 36, 14, hsv(rnd(seed, 7), 0.6f, 0.9f));
    char initial[2] = { who[0], 0 };
    draw_text(top, 22 - text_width(initial, 2) / 2, 30, 2, initial, WHITE);
    label(top, 44, 28, 2, who, WHITE);
    char cap[48];
    caption_of(cap, sizeof(cap), seed, kind);
    label(top, 8, 188, 1, cap, WHITE);
    char snd[64];
    snprintf(snd, sizeof(snd), "ORIGINAL SOUND - %s", who);
    label(top, 8, 208, 1, snd, 0xD0D0D0);

    // how far the video is, along the bottom
    draw_rect(top, 0, CH - 3, CW, 3, 0x44444A);
    draw_rect(top, 0, CH - 3, (int)(CW * (age % loop) / loop), 3, WHITE);

    if (now < s->heartUntil) {           // big heart pops up after a like
        float k = 1.0f - (s->heartUntil - now) / 700.0f;
        heart(top, 160, 120, 50 + 40 * k, RED);
    }
}

static void draw_bottom(const Spotlight *s, uint16_t *bottom) {
    draw_rect(bottom, 0, 0, CW, CH, BG);
    draw_line(bottom, 21, 10, 15, 18, 2.8f, WHITE);              // back
    draw_line(bottom, 15, 18, 21, 26, 2.8f, WHITE);
    centred(bottom, CW / 2, 11, 2, "SPOTLIGHT", WHITE);

    char t[32];
    snprintf(t, sizeof(t), "VIDEO %d", s->pos + 1);
    centred(bottom, CW / 2, 40, 1, t, GREY);

    disc(bottom, 54, 100, 30, CARD);                             // previous
    draw_line(bottom, 60, 86, 46, 100, 4.0f, s->pos > 0 ? WHITE : 0x55555B);
    draw_line(bottom, 46, 100, 60, 114, 4.0f, s->pos > 0 ? WHITE : 0x55555B);
    disc(bottom, 266, 100, 30, CARD);                            // next
    draw_line(bottom, 260, 86, 274, 100, 4.0f, WHITE);
    draw_line(bottom, 274, 100, 260, 114, 4.0f, WHITE);
    disc(bottom, 160, 100, 36, s->liked[s->pos] ? RED : YELLOW); // like
    heart(bottom, 160, 98, 30, s->liked[s->pos] ? WHITE : BLACK);

    centred(bottom, CW / 2, 150, 1, "A: NEXT   X: LIKE   D-PAD: PREV / NEXT", GREY);
    centred(bottom, CW / 2, 164, 1, "THESE VIDEOS ARE MADE UP ON THE 3DS", 0x66666C);
    draw_rect(bottom, 24, 190, CW - 48, 28, CARD);
    centred(bottom, CW / 2, 197, 2, "B: BACK", WHITE);
}

void spotlight_draw(const Spotlight *s, uint16_t *top, uint16_t *bottom, unsigned long long nowMs) {
    draw_top(s, top, nowMs);
    draw_bottom(s, bottom);
}
