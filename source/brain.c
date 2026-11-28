// brain.c - the offline reply rules. See brain.h.
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "brain.h"
#include "extra.h"

const Persona PERSONAS[BP_COUNT] = {
    { "MY AI", "I AM MY AI, A COMPUTER HELPER IN THIS APP.", "I LIKE HELPING WITH QUESTIONS.",
      "YELLOW", "PIXELS", "ASK ME MATHS, THE TIME, UNITS OR FACTS. TYPE HELP TO SEE MORE.",
      "HEY! GOOD TO SEE YOU." },
    { "MIA", "I AM MIA, AN AI FRIEND WHO LOVES ART.", "I LOVE DRAWING AND TAKING PHOTOS.",
      "PINK", "MANGO ICE CREAM", "ART TIP: SKETCH LIGHTLY FIRST, THEN DARKEN THE LINES YOU LIKE.",
      "HIII! IT IS MIA!" },
    { "JAY", "I AM JAY, AN AI FRIEND WHO IS INTO GAMES.", "I LOVE VIDEO GAMES AND PUZZLES.",
      "BLUE", "PIZZA", "GAME TIP: IF A LEVEL IS HARD, TAKE A SHORT BREAK AND TRY A NEW PLAN.",
      "YO! JAY HERE." },
    { "LIA", "I AM LIA, AN AI FRIEND WHO LOVES FOOD AND ANIMALS.", "I LOVE COOKING AND CUTE ANIMALS.",
      "GREEN", "CHICKEN ADOBO", "COOKING TIP: READ THE WHOLE RECIPE BEFORE YOU START.",
      "HELLO! LIA HERE." },
    { "KAI", "I AM KAI, AN AI FRIEND WHO LIKES SCIENCE AND SPORTS.", "I LOVE SPACE AND BASKETBALL.",
      "ORANGE", "BURGERS", "STUDY TIP: EXPLAIN IT IN YOUR OWN WORDS. THAT SHOWS WHAT YOU KNOW.",
      "HEY, IT IS KAI!" },
};

/* ---------------- facts ---------------- */

// A fact is used when every word of its key is in the question. "A|B" means A or B.
typedef struct { const char *key, *answer; } Fact;

#define CAP(country, city) { "CAPITAL " country, "THE CAPITAL IS " city "." }
static const Fact FACTS[] = {
    CAP("PHILIPPINES", "MANILA"), CAP("JAPAN", "TOKYO"), CAP("CHINA", "BEIJING"),
    CAP("SOUTH KOREA", "SEOUL"), CAP("NORTH KOREA", "PYONGYANG"), CAP("THAILAND", "BANGKOK"),
    CAP("VIETNAM", "HANOI"), CAP("INDONESIA", "JAKARTA"), CAP("MALAYSIA", "KUALA LUMPUR"),
    CAP("SINGAPORE", "SINGAPORE"), CAP("INDIA", "NEW DELHI"), CAP("PAKISTAN", "ISLAMABAD"),
    CAP("SAUDI ARABIA", "RIYADH"), CAP("TURKEY", "ANKARA"), CAP("RUSSIA", "MOSCOW"),
    CAP("USA|AMERICA|UNITED STATES", "WASHINGTON, D.C."), CAP("CANADA", "OTTAWA"),
    CAP("MEXICO", "MEXICO CITY"), CAP("BRAZIL", "BRASILIA"), CAP("ARGENTINA", "BUENOS AIRES"),
    CAP("PERU", "LIMA"), CAP("CHILE", "SANTIAGO"), CAP("UK|ENGLAND|UNITED KINGDOM|BRITAIN", "LONDON"),
    CAP("FRANCE", "PARIS"), CAP("GERMANY", "BERLIN"), CAP("ITALY", "ROME"), CAP("SPAIN", "MADRID"),
    CAP("PORTUGAL", "LISBON"), CAP("NETHERLANDS", "AMSTERDAM"), CAP("GREECE", "ATHENS"),
    CAP("SWEDEN", "STOCKHOLM"), CAP("NORWAY", "OSLO"), CAP("EGYPT", "CAIRO"),
    CAP("KENYA", "NAIROBI"), CAP("AUSTRALIA", "CANBERRA"), CAP("NEW ZEALAND", "WELLINGTON"),

    { "NATIONAL HERO PHILIPPINES", "JOSE RIZAL IS WIDELY HONORED AS THE NATIONAL HERO OF THE PHILIPPINES." },
    { "RIZAL", "JOSE RIZAL WAS A FILIPINO WRITER AND HERO. HE WROTE NOLI ME TANGERE." },
    { "PHILIPPINE|PHILIPPINES INDEPENDENCE", "PHILIPPINE INDEPENDENCE WAS DECLARED ON JUNE 12, 1898." },
    { "NATIONAL BIRD PHILIPPINES", "THE PHILIPPINE EAGLE IS THE NATIONAL BIRD." },
    { "HOW MANY ISLANDS PHILIPPINES", "THE PHILIPPINES HAS MORE THAN 7,600 ISLANDS." },
    { "HIGHEST|TALLEST MOUNTAIN PHILIPPINES", "MOUNT APO IS THE HIGHEST MOUNTAIN IN THE PHILIPPINES." },

    { "BIGGEST|LARGEST PLANET", "JUPITER IS THE BIGGEST PLANET IN OUR SOLAR SYSTEM." },
    { "SMALLEST PLANET", "MERCURY IS THE SMALLEST PLANET." },
    { "RED PLANET", "MARS IS CALLED THE RED PLANET." },
    { "HOW MANY PLANETS", "THERE ARE 8 PLANETS IN OUR SOLAR SYSTEM." },
    { "CLOSEST PLANET SUN", "MERCURY IS THE CLOSEST PLANET TO THE SUN." },
    { "HOTTEST PLANET", "VENUS IS THE HOTTEST PLANET." },
    { "=WHAT IS THE SUN|WHATS THE SUN|WHAT IS SUN", "THE SUN IS A STAR AT THE CENTER OF OUR SOLAR SYSTEM." },
    { "WHY SKY BLUE", "AIR SCATTERS BLUE LIGHT MORE THAN RED, SO BLUE COMES AT US FROM ALL OVER THE SKY." },
    { "PHOTOSYNTHESIS", "PLANTS USE SUNLIGHT, WATER AND CARBON DIOXIDE TO MAKE FOOD AND OXYGEN." },
    { "GRAVITY", "GRAVITY IS THE PULL BETWEEN THINGS WITH MASS. IT KEEPS YOUR FEET ON THE GROUND." },
    { "WATER CYCLE", "WATER EVAPORATES, FORMS CLOUDS, FALLS AS RAIN, THEN FLOWS BACK AND REPEATS." },
    { "WATER BOIL|BOILS|BOILING", "AT SEA LEVEL, WATER BOILS AT 100 C (212 F)." },
    { "WATER FREEZE|FREEZES|FREEZING", "WATER FREEZES AT 0 C (32 F)." },
    { "FORMULA|SYMBOL WATER", "WATER IS H2O." },
    { "H2O", "H2O IS WATER: 2 HYDROGEN ATOMS AND 1 OXYGEN ATOM." },
    { "LARGEST|BIGGEST OCEAN", "THE PACIFIC IS THE LARGEST OCEAN." },
    { "HOW MANY OCEANS", "PEOPLE USUALLY COUNT 5 OCEANS: PACIFIC, ATLANTIC, INDIAN, SOUTHERN AND ARCTIC." },
    { "HOW MANY CONTINENTS", "THE COMMON COUNT IS 7 CONTINENTS." },
    { "LARGEST|BIGGEST CONTINENT", "ASIA IS THE LARGEST CONTINENT." },
    { "HIGHEST|TALLEST MOUNTAIN", "MOUNT EVEREST IS THE HIGHEST MOUNTAIN ABOVE SEA LEVEL." },
    { "LARGEST|BIGGEST COUNTRY", "RUSSIA IS THE LARGEST COUNTRY BY AREA." },
    { "LARGEST|BIGGEST ANIMAL", "THE BLUE WHALE IS THE LARGEST ANIMAL ALIVE." },
    { "FASTEST ANIMAL", "THE PEREGRINE FALCON IS FASTEST IN A DIVE. ON LAND IT IS THE CHEETAH." },
    { "TALLEST ANIMAL", "THE GIRAFFE IS THE TALLEST ANIMAL." },
    { "HOW MANY LEGS SPIDER", "A SPIDER HAS 8 LEGS." },
    { "HOW MANY BONES", "AN ADULT HUMAN USUALLY HAS 206 BONES." },
    { "=DAYS IN A YEAR|DAYS ARE IN A YEAR|DAYS IN ONE YEAR", "A YEAR HAS 365 DAYS, OR 366 IN A LEAP YEAR." },
    { "=DAYS IN A WEEK|DAYS ARE IN A WEEK", "A WEEK HAS 7 DAYS." },
    { "=HOURS IN A DAY|HOURS ARE IN A DAY", "A DAY HAS 24 HOURS." },
    { "COLORS RAINBOW", "RED, ORANGE, YELLOW, GREEN, BLUE, INDIGO AND VIOLET." },
    { "WHAT NOUN", "A NOUN NAMES A PERSON, PLACE, THING OR IDEA." },
    { "WHAT VERB", "A VERB SHOWS AN ACTION OR A STATE, LIKE RUN OR IS." },
    { "WHAT ADJECTIVE", "AN ADJECTIVE DESCRIBES A NOUN, LIKE RED OR TALL." },
    { "VALUE PI|WHAT PI", "PI IS ABOUT 3.14159." },
    { "WHO MADE|CREATED|INVENTED 3DS", "NINTENDO MADE THE 3DS." },
};

static const char *const JOKES[] = {
    "WHY DID THE CAMERA GO TO SCHOOL? TO GET A BETTER PICTURE!",
    "WHAT DO YOU CALL A SLEEPING DINOSAUR? A DINO-SNORE!",
    "WHY DID THE PHONE WEAR GLASSES? IT LOST ITS CONTACTS!",
    "WHAT IS A GHOST'S FAVORITE DESSERT? I SCREAM!",
    "WHY DON'T EGGS TELL JOKES? THEY MIGHT CRACK UP!",
    "WHAT DID THE 3DS SAY TO THE GAME? YOU ADD A NEW DIMENSION!",
};
#define COUNT(a) ((int)(sizeof(a) / sizeof(a[0])))

/* ---------------- reading the question ---------------- */

#define Q_MAX 200

// Upper case, apostrophes dropped, punctuation turned into spaces, with a space at
// both ends so that whole words can be found.
static void tidy(const char *s, char *q) {
    size_t n = 0;
    q[n++] = ' ';
    for (size_t i = 0; s[i] && n < Q_MAX - 2; i++) {
        char c = s[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c == '\'') continue;
        if (c == ',' && i > 0 && s[i - 1] >= '0' && s[i - 1] <= '9' &&
            s[i + 1] >= '0' && s[i + 1] <= '9') continue;       // 1,000
        bool digitNext = s[i + 1] >= '0' && s[i + 1] <= '9';
        bool keep = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    strchr("+-*/()%^", c) || (c == '.' && digitNext);
        if (!keep) c = ' ';
        if (c == ' ' && q[n - 1] == ' ') continue;
        q[n++] = c;
    }
    if (q[n - 1] != ' ') q[n++] = ' ';
    q[n] = 0;
}

static bool has_word(const char *q, const char *w) {     // whole words only
    char t[210];
    snprintf(t, sizeof(t), " %s ", w);
    return strstr(q, t) != NULL;
}

static bool any(const char *q, const char *list) {       // "HI|HEY|GOOD MORNING"
    char t[200];
    snprintf(t, sizeof(t), "%s", list);
    for (char *p = t, *bar; p; p = bar ? bar + 1 : NULL) {
        bar = strchr(p, '|');
        if (bar) *bar = 0;
        if (has_word(q, p)) return true;
    }
    return false;
}

// Every part of the key must match. Parts are split by spaces, but phrases that are
// alternatives (with |) may hold spaces, so those are split at known places.
static bool fact_matches(const char *q, const char *key) {
    if (strncmp(key, "CAPITAL ", 8) == 0)
        return has_word(q, "CAPITAL") && any(q, key + 8);
    if (key[0] == '=') return any(q, key + 1);
    // "biggest ... in Japan" is a different question from the world record we know
    if (strstr(key, "EST") && !strstr(key, "PHILIPPINES") && has_word(q, "IN") &&
        !any(q, "IN THE WORLD|IN OUR SOLAR SYSTEM|IN THE SOLAR SYSTEM|IN SPACE")) return false;
    char t[96];
    snprintf(t, sizeof(t), "%s", key);
    for (char *p = t, *sp; p; p = sp ? sp + 1 : NULL) {
        sp = strchr(p, ' ');
        if (sp) *sp = 0;
        if (!any(q, p)) return false;
    }
    return true;
}

/* ---------------- maths ---------------- */

typedef struct { const char *p; bool bad; int depth; bool byZero; } Calc;

static double sum(Calc *c);

static void skip(Calc *c) { while (*c->p == ' ') c->p++; }

static double value(Calc *c) {
    skip(c);
    if (++c->depth > 24) { c->bad = true; return 0; }
    double v = 0;
    if (*c->p == '-') { c->p++; v = -value(c); }
    else if (*c->p == '(') {
        c->p++;
        v = sum(c);
        skip(c);
        if (*c->p == ')') c->p++; else c->bad = true;
    } else if ((*c->p >= '0' && *c->p <= '9') || *c->p == '.') {
        char *end;
        v = strtod(c->p, &end);
        c->p = end;
    } else c->bad = true;
    skip(c);
    if (!c->bad && *c->p == '^') { c->p++; v = pow(v, value(c)); }
    c->depth--;
    return v;
}

static double product(Calc *c) {
    double v = value(c);
    for (;;) {
        skip(c);
        char op = *c->p;
        if (c->bad || (op != '*' && op != '/')) return v;
        c->p++;
        double r = value(c);
        if (op == '*') v *= r;
        else if (r == 0) c->bad = c->byZero = true;
        else v /= r;
    }
}

static double sum(Calc *c) {
    double v = product(c);
    for (;;) {
        skip(c);
        char op = *c->p;
        if (c->bad || (op != '+' && op != '-')) return v;
        c->p++;
        double r = product(c);
        v = op == '+' ? v + r : v - r;
    }
}

static void swap_all(char *s, size_t cap, const char *from, const char *to) {
    size_t a = strlen(from), b = strlen(to);
    for (char *p = s; (p = strstr(p, from)) != NULL; p += b) {
        if (strlen(s) - a + b >= cap) return;
        memmove(p + b, p + a, strlen(p + a) + 1);
        memcpy(p, to, b);
    }
}

static void number(double v, char *out, size_t len) {
    if (fabs(v) < 1e12 && fabs(v - floor(v + 0.5)) < 1e-9) snprintf(out, len, "%.0f", floor(v + 0.5) + 0.0);
    else snprintf(out, len, "%.6g", v);
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static bool try_maths(const char *q, char *out, size_t len) {
    char s[Q_MAX * 2];
    snprintf(s, sizeof(s), "%s", q);
    swap_all(s, sizeof(s), " PLUS ", " + ");
    swap_all(s, sizeof(s), " MINUS ", " - ");
    swap_all(s, sizeof(s), " MULTIPLIED BY ", " * ");
    swap_all(s, sizeof(s), " TIMES ", " * ");
    swap_all(s, sizeof(s), " DIVIDED BY ", " / ");
    swap_all(s, sizeof(s), " TO THE POWER OF ", " ^ ");
    swap_all(s, sizeof(s), " SQUARED ", " ^ 2 ");
    swap_all(s, sizeof(s), "% OF ", "/100 * ");
    swap_all(s, sizeof(s), " PERCENT OF ", " /100 * ");
    for (size_t i = 1; s[i] && s[i + 1]; i++) {          // 6 X 7 or 6X7
        if (s[i] != 'X') continue;
        size_t a = i - 1, b = i + 1;
        while (a > 0 && s[a] == ' ') a--;
        while (s[b] == ' ') b++;
        if ((is_digit(s[a]) || s[a] == ')') && (is_digit(s[b]) || s[b] == '(')) s[i] = '*';
    }

    double v;
    const char *root = strstr(s, "SQUARE ROOT OF ");
    char n[32];
    if (root) {
        char *end;
        v = strtod(root + 15, &end);
        if (end == root + 15 || v < 0) return false;
        number(sqrt(v), n, sizeof(n));
        snprintf(out, len, "THE SQUARE ROOT IS %s.", n);
        return true;
    }
    // the first run of maths letters that has a digit and an operator
    for (const char *p = s; *p; ) {
        if (!strchr("0123456789.(-", *p)) { p++; continue; }
        size_t span = strspn(p, "0123456789.+-*/()^ ");
        char e[Q_MAX];
        if (span >= sizeof(e)) return false;
        memcpy(e, p, span);
        e[span] = 0;
        bool digit = strpbrk(e, "0123456789") != NULL;
        bool op = strpbrk(e + 1, "+-*/^") != NULL;
        if (digit && op) {
            Calc c = { e, false, 0, false };
            v = sum(&c);
            skip(&c);
            if (c.byZero) { snprintf(out, len, "YOU CANNOT DIVIDE BY ZERO!"); return true; }
            if (c.bad || *c.p || !isfinite(v)) {
                snprintf(out, len, "I COULD NOT WORK THAT OUT. TRY IT LIKE: 12 X (3 + 4)");
                return true;
            }
            number(v, n, sizeof(n));
            snprintf(out, len, "THE ANSWER IS %s.", n);
            return true;
        }
        p += span ? span : 1;
    }
    return false;
}

/* ---------------- unit changes ---------------- */

typedef struct { const char *from, *to, *fromName, *toName; double mul, add; } Unit;
#define KM "KM|KILOMETER|KILOMETERS"
#define MI "MILE|MILES"
#define KG "KG|KILO|KILOS|KILOGRAM|KILOGRAMS"
#define LB "LB|LBS|POUND|POUNDS"
#define CM "CM|CENTIMETER|CENTIMETERS"
#define IN "INCH|INCHES"
#define DC "C|CELSIUS"
#define DF "F|FAHRENHEIT"
static const Unit UNITS[] = {
    { KM, MI, "KM", "MILES", 0.621371, 0 },  { MI, KM, "MILES", "KM", 1.609344, 0 },
    { KG, LB, "KG", "POUNDS", 2.204623, 0 }, { LB, KG, "POUNDS", "KG", 0.453592, 0 },
    { CM, IN, "CM", "INCHES", 0.393701, 0 }, { IN, CM, "INCHES", "CM", 2.54, 0 },
    { DC, DF, "C", "F", 1.8, 32 },           { DF, DC, "F", "C", 5.0 / 9.0, -160.0 / 9.0 },
};

// "10 KM TO MILES" or "HOW MANY MILES IS 10 KM": the unit right after the number is the "from" one.
static bool try_units(const char *q, char *out, size_t len) {
    const char *p = q;
    while (*p && !is_digit(*p)) p++;
    if (!*p) return false;
    const char *start = (p > q && p[-1] == '-') ? p - 1 : p;
    char *end;
    double v = strtod(start, &end);
    char after[Q_MAX];
    snprintf(after, sizeof(after), " %s", *end == ' ' ? end + 1 : end);
    swap_all(after, sizeof(after), " DEGREES ", " ");
    char first[24] = " ";
    size_t n = strcspn(after + 1, " ");
    if (n == 0 || n > 20) return false;
    snprintf(first, sizeof(first), " %.*s ", (int)n, after + 1);
    for (int i = 0; i < COUNT(UNITS); i++)
        if (any(first, UNITS[i].from) && any(q, UNITS[i].to)) {
            char a[32];
            number(v, a, sizeof(a));
            snprintf(out, len, "%s %s IS ABOUT %.2f %s.", a, UNITS[i].fromName,
                     v * UNITS[i].mul + UNITS[i].add, UNITS[i].toName);
            return true;
        }
    return false;
}

/* ---------------- clock ---------------- */

static bool try_clock(const char *q, time_t now, char *out, size_t len) {
    static const char *const DAY[7] = { "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY" };
    static const char *const MON[12] = { "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY",
                                         "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER" };
    struct tm *lt = localtime(&now);
    if (!lt) return false;
    struct tm t = *lt;
    if (any(q, "WHAT TIME IS IT|WHATS THE TIME|WHAT IS THE TIME|TIME NOW|CURRENT TIME")) {
        snprintf(out, len, "IT IS %d:%02d %s ON YOUR 3DS CLOCK.", t.tm_hour % 12 ? t.tm_hour % 12 : 12,
                 t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
    } else if (has_word(q, "CHRISTMAS") && any(q, "DAYS|HOW LONG|WHEN")) {
        struct tm x = t;
        x.tm_mon = 11; x.tm_mday = 25; x.tm_hour = 12; x.tm_isdst = -1;
        struct tm today = t;
        today.tm_hour = 12; today.tm_isdst = -1;
        time_t a = mktime(&today), b = mktime(&x);
        if (b < a) { x.tm_year++; b = mktime(&x); }
        int days = (int)(difftime(b, a) / 86400.0 + 0.5);
        if (days) snprintf(out, len, "CHRISTMAS IS DEC 25. THAT IS %d DAY%s FROM NOW!", days, days == 1 ? "" : "S");
        else snprintf(out, len, "IT IS CHRISTMAS TODAY! MERRY CHRISTMAS!");
    } else if (any(q, "WHAT DAY IS IT|WHAT DAY IS TODAY|WHATS THE DATE|WHAT IS THE DATE|DATE TODAY|TODAYS DATE|WHAT DATE")) {
        snprintf(out, len, "TODAY IS %s, %s %d, %d.", DAY[t.tm_wday], MON[t.tm_mon], t.tm_mday, t.tm_year + 1900);
    } else if (any(q, "WHAT YEAR IS IT|WHAT YEAR NOW|CURRENT YEAR")) {
        snprintf(out, len, "IT IS %d.", t.tm_year + 1900);
    } else return false;
    return true;
}

/* ---------------- remembering you ---------------- */

// Copies up to maxWords words that follow "lead".
static bool after_words(const char *q, const char *lead, int maxWords, char *out, size_t len) {
    const char *p = strstr(q, lead);
    if (!p) return false;
    p += strlen(lead);
    size_t n = 0;
    int words = 0;
    for (; *p && n < len - 1; p++) {
        if (*p == ' ' && ++words >= maxWords) break;
        out[n++] = *p;
    }
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
    return n > 0;
}

static bool try_memory(BrainMemory *m, const char *q, char *out, size_t len) {
    if (any(q, "WHAT IS MY NAME|WHATS MY NAME|DO YOU KNOW MY NAME|WHO AM I")) {
        if (m->name[0]) snprintf(out, len, "YOUR NAME IS %s!", m->name);
        else snprintf(out, len, "YOU HAVE NOT TOLD ME YET. SAY: MY NAME IS ...");
    } else if (any(q, "WHAT IS MY FAVORITE COLOR|WHATS MY FAVORITE COLOR")) {
        if (m->color[0]) snprintf(out, len, "YOUR FAVORITE COLOR IS %s.", m->color);
        else snprintf(out, len, "TELL ME: MY FAVORITE COLOR IS ...");
    } else if (any(q, "WHAT IS MY FAVORITE FOOD|WHATS MY FAVORITE FOOD")) {
        if (m->food[0]) snprintf(out, len, "YOUR FAVORITE FOOD IS %s.", m->food);
        else snprintf(out, len, "TELL ME: MY FAVORITE FOOD IS ...");
    } else if (after_words(q, " MY NAME IS ", 2, m->name, sizeof(m->name)) ||
               after_words(q, " CALL ME ", 1, m->name, sizeof(m->name))) {
        snprintf(out, len, "NICE TO MEET YOU, %s! I WILL REMEMBER IT WHILE THE APP IS OPEN.", m->name);
    } else if (after_words(q, " MY FAVORITE COLOR IS ", 2, m->color, sizeof(m->color))) {
        snprintf(out, len, "%s IS A NICE COLOR! I WILL REMEMBER.", m->color);
    } else if (after_words(q, " MY FAVORITE FOOD IS ", 3, m->food, sizeof(m->food))) {
        snprintf(out, len, "YUM, %s! I WILL REMEMBER.", m->food);
    } else return false;
    return true;
}

/* ---------------- the answer ---------------- */

void brain_reply(BrainMemory *m, int who, const char *said, time_t now, char *out, size_t len) {
    if (who < 0 || who >= BP_COUNT) who = BP_MYAI;
    brain_reply_as(m, &PERSONAS[who], said, now, out, len);
}

void brain_reply_as(BrainMemory *m, const Persona *me, const char *said, time_t now, char *out, size_t len) {
    char q[Q_MAX];
    tidy(said, q);

    // safety comes before everything else
    if (any(q, "KILL MYSELF|SUICIDE|SUICIDAL|WANT TO DIE|WANNA DIE|END MY LIFE|HURT MYSELF|HURTING MYSELF|SELF HARM|CUT MYSELF")) {
        snprintf(out, len, "I'M ONLY A PROGRAM, BUT YOU MATTER. PLEASE TELL SOMEONE YOU TRUST NOW. IN DANGER? CALL 911.");
        return;
    }
    if (try_memory(m, q, out, len) || try_clock(q, now, out, len) ||
        try_units(q, out, len) || try_maths(q, out, len)) return;

    for (int i = 0; i < COUNT(FACTS); i++)
        if (fact_matches(q, FACTS[i].key)) { snprintf(out, len, "%s", FACTS[i].answer); return; }

    if (any(q, "ARE YOU REAL|ARE YOU HUMAN|ARE YOU A PERSON|ARE YOU A ROBOT|ARE YOU AN AI|ARE YOU AI|ARE YOU A BOT"))
        snprintf(out, len, "I AM NOT A REAL PERSON. I AM A COMPUTER CHARACTER IN THIS APP.");
    else if (any(q, "WHO ARE YOU|YOUR NAME|WHO R U|ABOUT YOURSELF"))
        snprintf(out, len, "%s", me->about);
    else if (any(q, "FAVORITE COLOR")) snprintf(out, len, "MY PICK IS %s! WHAT IS YOURS?", me->color);
    else if (any(q, "FAVORITE FOOD")) snprintf(out, len, "I PICK %s! I CAN'T REALLY EAT, THOUGH.", me->food);
    else if (any(q, "WHAT DO YOU LIKE|WHAT DO YOU DO|HOBBY|HOBBIES|FOR FUN"))
        snprintf(out, len, "%s", me->hobby);
    else if (any(q, "TIP|TIPS|ADVICE") && !any(q, "SNAP|PHOTO|SELFIE|BOOMERANG"))
        snprintf(out, len, "%s", me->tip);
    else if (any(q, "JOKE|JOKES|FUNNY|MAKE ME LAUGH"))
        snprintf(out, len, "%s", JOKES[m->jokeNo++ % COUNT(JOKES)]);
    else if (any(q, "FILTER|FILTERS|LENS"))
        snprintf(out, len, "TRY THE %s FILTER!", filter_name_any(rand() % ALL_FILTERS));
    else if (any(q, "FLIP A COIN|COIN FLIP|TOSS A COIN"))
        snprintf(out, len, "I FLIPPED A COIN: %s!", rand() % 2 ? "HEADS" : "TAILS");
    else if (any(q, "ROLL A DIE|ROLL A DICE|ROLL DICE|ROLL THE DICE"))
        snprintf(out, len, "YOU ROLLED A %d!", rand() % 6 + 1);
    else if (any(q, "SNAP|SNAPS|PHOTO|PICTURE|SELFIE|BOOMERANG"))
        snprintf(out, len, "PRESS X OR TAP THE BIG CIRCLE TO SNAP. HOLD THE CIRCLE FOR A BOOMERANG!");
    else if (any(q, "MAP"))
        snprintf(out, len, "ON THE MAP, MOVE THE CROSS AND PRESS X. NEW SNAPS GET PINNED THERE.");
    else if (any(q, "HOW ARE YOU|HOW R U|HOW ARE U|WHATS UP|KUMUSTA|KAMUSTA"))
        snprintf(out, len, "I AM DOING GREAT, THANKS! HOW ARE YOU?");
    else if (any(q, "NOT HAPPY|NOT OK|NOT OKAY|NOT GOOD|NOT FINE|SAD|LONELY|UPSET|STRESSED|SCARED|WORRIED|ANGRY|CRYING"))
        snprintf(out, len, "I'M SORRY YOU FEEL THAT WAY. TALKING TO SOMEONE YOU TRUST CAN REALLY HELP.");
    else if (any(q, "BORED|BORING"))
        snprintf(out, len, "LET'S FIX THAT! ASK FOR A JOKE, FLIP A COIN, OR GO TRY A NEW FILTER.");
    else if (any(q, "HAPPY|IM GOOD|I AM GOOD|IM FINE|I AM FINE|IM GREAT|I AM GREAT|IM OK|IM OKAY"))
        snprintf(out, len, "YAY, I'M GLAD TO HEAR THAT!");
    else if (any(q, "THANK|THANKS|SALAMAT"))
        snprintf(out, len, "YOU ARE WELCOME!");
    else if (any(q, "BYE|GOODBYE|GOOD NIGHT|GOODNIGHT|SEE YOU"))
        snprintf(out, len, "BYE%s%s! TALK SOON.", m->name[0] ? " " : "", m->name);
    else if (any(q, "GOOD MORNING|GOOD AFTERNOON|GOOD EVENING|HI|HII|HIII|HEY|HELLO|YO|HOWDY"))
        snprintf(out, len, "%s%s%s", me->hello, m->name[0] ? " HI " : "", m->name);
    else if (any(q, "LOVE YOU|LIKE YOU"))
        snprintf(out, len, "THAT'S KIND! I'M A COMPUTER CHARACTER, BUT I'M HAPPY TO CHAT.");
    else if (any(q, "HELP|WHAT CAN YOU DO"))
        snprintf(out, len, "ASK ME MATHS, TIME, DATE, UNITS, CAPITALS, SCIENCE FACTS, A JOKE OR A COIN FLIP.");
    else if (strchr(said, '?') || any(q, "WHAT|WHO|WHY|HOW|WHEN|WHERE|WHICH|CAN YOU|DO YOU|CAPITAL|HIGHEST|TALLEST|BIGGEST|LARGEST"))
        snprintf(out, len, "SORRY, I DON'T KNOW THAT ONE. I'M AN OFFLINE BOT. TYPE HELP TO SEE WHAT I KNOW.");
    else {
        static const char *const SMALL[] = {
            "OH, I SEE! WHAT ELSE IS NEW?", "COOL! ASK ME A QUESTION IF YOU LIKE.",
            "GOT IT! WANT TO HEAR A JOKE?",
        };
        snprintf(out, len, "%s", SMALL[m->fallbackNo++ % COUNT(SMALL)]);
    }
}
