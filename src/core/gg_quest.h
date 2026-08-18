// gg_quest.h - the story, as a state machine in a file.
//
// A quest is a list of stages. Stage k is entered when stage k's condition
// holds, and entering it writes its journal line and may set a flag. Only the
// next stage is ever tested, so a quest cannot skip ahead, and the whole of a
// quest's state is one number: how far along it is.
//
// The conditions are things the world already knows how to answer - a word you
// have learned, something you are carrying, a flag another stage set, how many
// you have killed, how many walk with you. Nothing here reaches into the
// simulation to invent new state, because a condition that needs new
// bookkeeping is a condition that can go out of step with the world.
//
// **This is data.** `assets/quests.txt` holds every quest, stage, condition and
// journal line. Adding one is an edit to a text file.
#ifndef GG_QUEST_H
#define GG_QUEST_H

#include "core/gg_common.h"
#include "core/gg_ids.h"

#define GG_QUESTS_MAX        12
// Twelve, because the main storyline is one quest with a beginning, a
// journey, a confrontation and a way home in it.
#define GG_STAGES_MAX        12
#define GG_QUEST_NAME_MAX    48
#define GG_JOURNAL_LINE_MAX 132
#define GG_FLAGS_MAX         32

// What has to be true for a stage to be entered.
typedef enum {
    GG_WHEN_ALWAYS,     // nothing: the stage follows the one before it at once
    GG_WHEN_KNOWS,      // the player has learned a word
    GG_WHEN_HAS,        // the player carries N of a thing
    GG_WHEN_FLAG,       // another stage set it
    GG_WHEN_SLAIN,      // N have fallen to the party
    GG_WHEN_PARTY,      // N walk with the Avatar
    GG_WHEN_AT,         // the Avatar is in a map, or near a tile in one
    GG_WHEN_COUNT
} gg_when;

typedef struct {
    uint8_t what;                        // gg_when
    char    word[GG_FLAG_MAX];           // for KNOWS and FLAG
    uint8_t item;                        // for HAS
    int     count;                       // for HAS, SLAIN, PARTY

    // For AT: which map, and optionally where in it. `radius` of zero means
    // anywhere in the map at all.
    char    where[GG_MAP_NAME_MAX];
    int16_t wx, wy;
    uint8_t radius;

    char journal[GG_JOURNAL_LINE_MAX];
    char sets[GG_FLAG_MAX];              // a flag raised on entering, or ""

    // What entering it teaches the party, in experience. Working a story out
    // is worth as much as killing what is at the end of it - and a player who
    // talks their way through should not arrive weaker than one who did not.
    int  worth;

    // Entering this stage is the end of the story. The journal line is what
    // the ending says, so the closing words of the game are a line in a text
    // file like every other line the game says.
    bool ends;
} gg_stage;

typedef struct {
    char id[GG_QUEST_NAME_MAX];          // CARAVAN - what a data file calls it
    char name[GG_QUEST_NAME_MAX];        // "The Missing Caravan"
    gg_stage stage[GG_STAGES_MAX];
    int  stages;
} gg_quest;

bool gg_quests_load(const char *path);
void gg_quests_clear(void);

int gg_quests_count(void);
const gg_quest *gg_quest_at(int i);
int gg_quest_find(const char *id);

#endif // GG_QUEST_H
