// chat.h - the Chat screen, drawn to look like Snapchat's. The chats are FAKE:
// "My AI" answers from a built-in list of replies, "Team Snapchat" just has
// a welcome message. Nothing goes online.
// No 3DS code in here (like memories.c), so main.c only has to pass the buttons
// and touch in, open the keyboard when asked, and show the two pictures that come out.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "art.h"

#define CHAT_MAX_MSG 64                  // messages kept per chat (the newest ones)
#define CHAT_TEXT 96                     // longest message, with the ending 0
#define CHAT_THREADS 2

enum { CT_AI, CT_TEAM };                 // the two chats in the list
enum { CV_LIST, CV_THREAD };             // the chat list, or one chat open

// Buttons, as bit flags. main.c turns KEY_B etc. into these.
enum { CK_A = 1, CK_B = 2, CK_UP = 4, CK_DOWN = 8 };

typedef struct {
    unsigned keys;                       // CK_ flags pressed on this frame
    bool down;                           // the screen was touched on this frame
    int x, y;                            // touch position
    unsigned long long ms;               // clock in milliseconds
} ChatInput;

// What chat_update asks main.c to do.
enum { CR_NONE, CR_EXIT, CR_KEYBOARD };  // exit = back to the camera, keyboard = ask for text

typedef struct {
    bool mine;
    char text[CHAT_TEXT];
} ChatMsg;

typedef struct {
    ChatMsg msg[CHAT_MAX_MSG];
    int count;
    bool touched;                        // you wrote something (the list then says "now", not "2mo")
} ChatThread;

typedef struct {
    ChatThread th[CHAT_THREADS];
    bool started;                        // the starting messages are in
    int view;                            // CV_LIST or CV_THREAD
    int sel;                             // the chat that is highlighted / open
    int scroll, maxScroll;               // how far the top screen is scrolled up, in pixels
    bool pending;                        // My AI is "typing"
    char reply[CHAT_TEXT];
    unsigned long long replyAt;
    int jokeNo, fallbackNo;
    unsigned long long noteUntil;
    char note[44];
} Chat;

void chat_open(Chat *m);
// Call once per frame. Returns CR_EXIT, CR_KEYBOARD or CR_NONE.
int chat_update(Chat *m, const ChatInput *in);
// Puts your message in the open chat and lines up My AI's answer.
void chat_send(Chat *m, const char *text, unsigned long long nowMs);
// Draws both screens (each CW*CH RGB565).
void chat_draw(Chat *m, uint16_t *top, uint16_t *bottom, unsigned long long nowMs);
