// brain.h - how the pretend chat characters answer. It is NOT a real AI: it is
// a set of rules (maths, clock, unit changes, a list of facts, small talk).
// It works offline, and when it does not know something it says so.
#pragma once
#include <stddef.h>
#include <time.h>

typedef struct {
    const char *name;
    const char *about;                   // "WHO ARE YOU?"
    const char *hobby;                   // "WHAT DO YOU LIKE?"
    const char *color, *food;
    const char *tip;                     // advice about the hobby
    const char *hello;                   // greeting
} Persona;

enum { BP_MYAI, BP_MIA, BP_JAY, BP_LIA, BP_KAI, BP_COUNT };
extern const Persona PERSONAS[BP_COUNT];

typedef struct {                         // what the characters remember until you close the app
    char name[20], color[20], food[24];
    int jokeNo, fallbackNo;
} BrainMemory;

// Writes the answer of character "who" to "said" into out. now = the current local time.
void brain_reply(BrainMemory *mem, int who, const char *said, time_t now, char *out, size_t len);
// The same, for a character that is not in PERSONAS (a friend you made).
void brain_reply_as(BrainMemory *mem, const Persona *me, const char *said, time_t now, char *out, size_t len);
