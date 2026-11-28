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
#define CHAT_ONLINE 12                   // real friends (from the server) you can chat with
#define CHAT_THREADS (2 + CHAT_FRIENDS + CHAT_CUSTOM + CHAT_ONLINE)
#define ONLINE_USER 18                   // same as USER_MAX in main.c
#define ONLINE_NAME 14                   // same as PROF_NAME_MAX in main.c
#define CHAT_OUTBOX 6                    // online messages waiting to be sent
#define FRIEND_NAME 10                   // longest name and food of a friend you make
#define FRIEND_FOOD 20
#define CHAT_LISTED 2                    // rows always on the chat list: My AI and Team Snapchat (friends you make are added)
#define NICK_MAX 12                      // longest nickname
#define SNAP_KEEP 8                      // snaps remembered per chat (photo history)
#define GRID_N 8                         // photos shown in the picker / history grid
#define GRID_TW 77                       // grid thumbnail size
#define GRID_TH 58
#define N_THEMES 6                       // chat themes

/* ---- calls ---- */
enum { CALL_VOICE, CALL_VIDEO };         // m->callKind
enum { CS_RINGING, CS_ON };              // m->callState
// Where main.c puts the picture of your own camera on the top screen during a video call.
#define CALL_PIP_W 96
#define CALL_PIP_H 72
#define CALL_PIP_X (CW - 8 - CALL_PIP_W)
#define CALL_PIP_Y 8

// CT_FRIEND + 0..CHAT_FRIENDS-1 are the built-in friends, CT_CUSTOM + 0..CHAT_CUSTOM-1 yours.
// CT_ONLINE + 0..CHAT_ONLINE-1 are real friends on the server (chatted with online).
enum { CT_AI, CT_TEAM, CT_FRIEND, CT_CUSTOM = CT_FRIEND + CHAT_FRIENDS, CT_ONLINE = CT_CUSTOM + CHAT_CUSTOM };
enum { CV_LIST, CV_THREAD, CV_FRIENDS, CV_EDIT,     // chat list, one chat, friends list, friend maker
       CV_OPTIONS, CV_THEMES, CV_PICK, CV_HISTORY,   // chat options, theme chooser, "send a snap" picker, photo history
       CV_EMOJI, CV_VOICE, CV_GAMES, CV_TTT, CV_RPS,  // emoji picker, voice note recorder, games menu, tic tac toe, rock paper scissors
       CV_CALL };                                    // a voice or video call

#define VOICE_BARS 32                    // a voice note is up to 32 bars of 250 ms = 8 seconds
#define EMOJI_COUNT 12
#define MEDIA_TW 80                      // snaps and photos shown inside a chat
#define MEDIA_TH 60
#define MEDIA_CACHE 6

// The choices in the friend maker.
enum { FD_STYLE, FD_HAIR, FD_SKIN, FD_SHIRT, FD_BACK, FD_GLASSES, FD_LIKES, FD_COLOR, FD_COUNT };
typedef struct {
    bool used;
    char name[FRIEND_NAME + 1];
    char food[FRIEND_FOOD + 1];
    int v[FD_COUNT];
} FriendDef;

// Buttons, as bit flags. main.c turns KEY_B etc. into these.
enum { CK_A = 1, CK_B = 2, CK_UP = 4, CK_DOWN = 8, CK_LEFT = 16, CK_RIGHT = 32, CK_X = 64,
       CK_Y = 128, CK_L = 256 };         // Y = voice call (flip camera during a video call), L = video call

typedef struct {
    unsigned keys;                       // CK_ flags pressed on this frame
    bool down;                           // the screen was touched on this frame
    int x, y;                            // touch position
    unsigned long long ms;               // clock in milliseconds
    bool held;                           // the screen is still being touched
    bool micOk;                          // main.c has a working microphone (false = voice notes say so)
    int mic;                             // how loud it is right now, 0..100 (peak of the last few ms)
} ChatInput;

// What chat_update asks main.c to do.
enum { CR_NONE, CR_EXIT, CR_KEYBOARD, CR_MAP, CR_STORIES,      // exit = back to the camera, keyboard = ask for a message
       CR_ASK_NAME, CR_ASK_FOOD, CR_ASK_NICK,     // ask for a word, then call chat_set_text
       CR_CAMERA,                                 // open the camera; the next photo goes to the open chat (chat_attach)
       CR_CALL_FLIP,                              // video call: switch between the inner and outer camera
       CR_CALL_FILTER };                          // video call: next / previous filter (m->callFilterDir is +1 or -1)

typedef struct { char user[ONLINE_USER + 1]; char name[ONLINE_NAME + 1]; } OnlineEntry;
typedef struct { bool used; char user[ONLINE_USER + 1]; char name[ONLINE_NAME + 1]; } OnlineFriend;
typedef struct { char user[ONLINE_USER + 1]; char text[CHAT_TEXT]; } OutMsg;

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
    OnlineFriend online[CHAT_ONLINE];     // real friends, one chat thread each (CT_ONLINE + slot)
    OutMsg out[CHAT_OUTBOX];             // messages you wrote that the server has not got yet
    int outCount;
    long long lastId;                    // newest server message already shown
    BrainMemory memory;                  // what the characters remember about you
    char reply[CHAT_TEXT];
    unsigned long long replyAt;
    unsigned long long noteUntil;
    char note[44];
    char dir[96];                        // where chats.txt is saved ("" = not saved)
    bool saveFailed;
    int emojiSel;                        // highlighted emoji in the picker
    int gameSel;                         // highlighted row in the games menu
    int gameCur;                         // cursor in a game (tic tac toe square 0..8, or rock paper scissors 0..2)
    bool vRec;                           // the voice recorder is running
    int vBars[VOICE_BARS], vCount;       // loudness 0..9 of each 250 ms so far
    int vPeak;                           // loudest sound since the last bar
    unsigned long long vNext;            // when the next bar is due
    int ttt[9];                          // tic tac toe board: 0 empty, 1 you (X), 2 friend (O)
    int tttState;                        // 0 playing, 1 you won, 2 friend won, 3 draw
    int rpsMine, rpsTheirs;              // last throw: -1 none, 0 rock, 1 paper, 2 scissors
    int rpsWins, rpsLosses, rpsRounds;   // score of this match (first to 3)
    bool gameLogged;                     // the result of this game is already in the chat
    int gridFrom;                        // where B goes back to from the photo grid: CV_THREAD or CV_OPTIONS
    char pickKind;                       // what the picker sends: 'S' = a snap (streak), 'I' = a photo
    struct { char name[40]; uint8_t state; uint16_t px[MEDIA_TW * MEDIA_TH]; } tc[MEDIA_CACHE];   // pictures shown in the chat
    int tcNext;
    // a call (voice or video) with the open chat. The friends are offline characters, so they
    // answer in captions; the microphone and the camera are yours and really work.
    int callKind, callState;
    unsigned long long callStart;        // when they picked up
    unsigned long long callAnswerAt;     // when they will pick up
    unsigned long long callSpoke;        // last time the microphone heard you
    unsigned long long callSayAt, callSayEnd;   // the friend's caption: when it began, when they stop talking
    bool callMuted, callCamOff, callHeard, callMicOk;
    int callLevel;                       // how loud you are right now, 0..100
    int callPrompt;                      // what the friend talks about next
    int callFilterDir;                   // set together with CR_CALL_FILTER
    char callLine[CHAT_TEXT];            // the friend's caption
    char callFilter[24];                 // name of the filter on your picture (main.c fills it in)
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

/* ---- online chat (the app talks to the server; chat.c itself never touches the network) ---- */
// The accepted friends from the server. Chats of friends that are still there are kept.
void chat_set_online(Chat *m, const OnlineEntry *list, int n);
// A message from the server. Ignored when its id was seen already or the sender is not a friend.
void chat_receive(Chat *m, long long id, const char *user, const char *text);
// The oldest message that still has to be sent. false = nothing waiting.
bool chat_outbox_peek(const Chat *m, char *user, size_t ulen, char *text, size_t tlen);
void chat_outbox_pop(Chat *m);           // it was sent (or can never be sent): forget it
void chat_note(Chat *m, const char *text);   // small message on the bottom screen

/* ---- snaps ---- */
// A photo you just took for the open chat (after CR_CAMERA). file = the name inside the photo folder.
void chat_attach(Chat *m, const char *file, unsigned long long nowMs);
const char *chat_open_name(const Chat *m);   // who the open chat is with

/* ---- calls ---- */
// The microphone should be listening (a call is connected and you are not muted).
bool chat_call_wants_mic(const Chat *m);
// A video call is open and your camera is on: main.c draws your picture at CALL_PIP_X/Y/W/H on the top screen.
bool chat_call_wants_camera(const Chat *m);
// Tells the call screen which filter is on your picture (shown on the bottom screen).
void chat_call_set_filter(Chat *m, const char *name);
