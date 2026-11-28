// chat.h - the Chat screen, drawn to look like Snapchat's. The chats are FAKE:
// My AI and the friends answer with the offline rules in brain.c, "Team
// Snapchat" just has a welcome message. Nothing goes online.
// No 3DS code in here (like memories.c), so main.c only has to pass the buttons
// and touch in, open the keyboard when asked, and show the two pictures that come out.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "art.h"
#include "brain.h"

#define CHAT_MAX_MSG 64                  // messages kept per chat (the newest ones)
#define CHAT_TEXT 128                    // longest message, with the ending 0
#define CHAT_FRIENDS (BP_COUNT - 1)       // the AI friends (everyone in PERSONAS but My AI)
#define CHAT_CUSTOM 4                    // friends you can make yourself
#define CHAT_THREADS (2 + CHAT_FRIENDS + CHAT_CUSTOM)
#define FRIEND_NAME 10                   // longest name and food of a friend you make
#define FRIEND_FOOD 20
#define CHAT_LISTED 2                    // rows always on the chat list: My AI and Team Snapchat (friends you make are added)
#define NICK_MAX 12                      // longest nickname
#define SNAP_KEEP 8                      // snaps remembered per chat (photo history)
#define GRID_N 8                         // photos shown in the picker / history grid
#define GRID_TW 77                       // grid thumbnail size
#define GRID_TH 58
#define N_THEMES 6                       // chat themes

// CT_FRIEND + 0..CHAT_FRIENDS-1 are the built-in friends, CT_CUSTOM + 0..CHAT_CUSTOM-1 yours.
enum { CT_AI, CT_TEAM, CT_FRIEND, CT_CUSTOM = CT_FRIEND + CHAT_FRIENDS };
enum { CV_LIST, CV_THREAD, CV_FRIENDS, CV_EDIT,     // chat list, one chat, friends list, friend maker
       CV_OPTIONS, CV_THEMES, CV_PICK, CV_HISTORY }; // chat options, theme chooser, "send a snap" picker, photo history

// The choices in the friend maker.
enum { FD_STYLE, FD_HAIR, FD_SKIN, FD_SHIRT, FD_BACK, FD_GLASSES, FD_LIKES, FD_COLOR, FD_COUNT };
typedef struct {
    bool used;
    char name[FRIEND_NAME + 1];
    char food[FRIEND_FOOD + 1];
    int v[FD_COUNT];
} FriendDef;

// Buttons, as bit flags. main.c turns KEY_B etc. into these.
enum { CK_A = 1, CK_B = 2, CK_UP = 4, CK_DOWN = 8, CK_LEFT = 16, CK_RIGHT = 32, CK_X = 64 };

typedef struct {
    unsigned keys;                       // CK_ flags pressed on this frame
    bool down;                           // the screen was touched on this frame
    int x, y;                            // touch position
    unsigned long long ms;               // clock in milliseconds
} ChatInput;

// What chat_update asks main.c to do.
enum { CR_NONE, CR_EXIT, CR_KEYBOARD, CR_MAP, CR_STORIES,      // exit = back to the camera, keyboard = ask for a message
       CR_ASK_NAME, CR_ASK_FOOD, CR_ASK_NICK };   // ask for a word, then call chat_set_text

typedef struct {
    bool mine;
    char text[CHAT_TEXT];
} ChatMsg;

typedef struct {
    ChatMsg msg[CHAT_MAX_MSG];
    int count;
    bool touched;                        // you wrote something (the list then says "now", not "2mo")
    char nick[NICK_MAX + 1];             // nickname you gave this chat ("" = none)
    int theme;                           // 0..N_THEMES-1
    int streak;                          // days in a row you sent a snap
    long lastDay;                        // day number of the last snap (see local_day in chat.c)
    char snaps[SNAP_KEEP][40];           // files of the snaps you sent here, newest last
    int snapCount;
} ChatThread;

typedef struct {
    ChatThread th[CHAT_THREADS];
    bool started;                        // the starting messages are in
    int view;                            // CV_LIST or CV_THREAD
    int sel;                             // the chat that is highlighted / open
    int scroll, maxScroll;               // how far the top screen is scrolled up, in pixels
    bool pending;                        // someone is "typing"
    int replyTo;                         // the chat that answer belongs to
    int friendSel;                       // highlighted row on the friends list
    int friendPage;                      // 0 = built-in friends, 1 = the ones you made
    int listScroll;                      // first chat row shown on the chat list
    int threadFrom;                      // CV_LIST or CV_FRIENDS: where B goes back to from an open chat
    bool addFromList;                    // the friend maker was opened by ADD FRIENDS on the chat list
    bool friendsFromList;                // the friends list was opened by NEW CHAT (B goes back to the chat list)
    FriendDef custom[CHAT_CUSTOM];
    FriendDef draft;                     // the friend in the maker
    int editSlot, editRow;
    bool confirmDelete;
    int opSel, themeSel;                 // highlighted row in the options / theme chooser
    char gname[GRID_N][40];              // photo grid (picker or history), newest first
    int gcount, gsel;
    uint8_t gstate[GRID_N];              // 0 = thumbnail not loaded yet, 1 = ready, 2 = could not read
    uint16_t gthumb[GRID_N][GRID_TW * GRID_TH];
    int bigIdx;                          // grid photo that is in big[], -1 = none
    bool bigOk;
    uint16_t big[CW * CH];               // the highlighted photo, for the top screen
    BrainMemory memory;                  // what the characters remember about you
    char reply[CHAT_TEXT];
    unsigned long long replyAt;
    unsigned long long noteUntil;
    char note[44];
    char dir[96];                        // where chats.txt is saved ("" = not saved)
    bool saveFailed;
} Chat;

// Call once when the app starts: brings back the saved chats from dir.
void chat_load(Chat *m, const char *dir);
void chat_open(Chat *m);
void chat_open_friends(Chat *m);         // straight to the friends list
// Call once per frame. Returns CR_EXIT, CR_KEYBOARD, CR_MAP, CR_STORIES, CR_ASK_NAME,
// CR_ASK_FOOD, CR_ASK_NICK or CR_NONE.
int chat_update(Chat *m, const ChatInput *in);
// The answer to CR_ASK_NAME, CR_ASK_FOOD or CR_ASK_NICK.
void chat_set_text(Chat *m, int what, const char *text);
// Puts your message in the open chat and lines up the answer.
void chat_send(Chat *m, const char *text, unsigned long long nowMs);
// Draws both screens (each CW*CH RGB565).
void chat_draw(Chat *m, uint16_t *top, uint16_t *bottom, unsigned long long nowMs);
