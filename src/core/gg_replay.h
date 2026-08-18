// gg_replay.h - a session as a list of the actions that made it.
//
// The simulation is integer-only and seeded, and the world advances only inside
// `gg_game_act`. Those two facts together mean a session is fully described by
// what it started from and which actions it was given: play the same actions
// into the same starting world and you get the same world back, to the bit.
//
// That is worth having written down. **A bug report becomes a file**: the seed,
// the actions, and the state hash the reporter saw. Anyone can replay it and
// either see the same hash - in which case the bug is in what the game did with
// that state - or a different one, in which case the divergence is the bug and
// the replay says which turn it happened on.
//
// **The file is text**, in the same shape as every other content file here:
// `keyword rest-of-line`, `#` comments, blank lines ignored. A replay can be
// read, diffed, trimmed by hand to the shortest sequence that still breaks, and
// pasted into an issue. A binary stream would have been smaller and useless.
//
//     # gigantima replay
//     seed 4242
//     map vale.ggmap        (or `generated 80 64` for a world with no file -
//     profile Avatar         either way the seed decides what is *in* it)
//     act N
//     act TALK
//     travel stones.ggmap 24 44
//     ...
//     hash 3F2A91C4D8E70B15
//
// The one thing that is not an action is a crossing: the simulation names the
// map it wants and the frontend says where that map lives, so a replay records
// the crossing that was actually made and the driver performs it. Everything
// else - walking, talking, fighting, the pack, spells - is an action.
#ifndef GG_REPLAY_H
#define GG_REPLAY_H

#include "core/gg_common.h"
#include "core/gg_game.h"

// A number that is different if the world is different: the clock, the RNG, the
// story, every actor, everything carried, everything on the floor, and every
// map held in mind. Not a checksum of memory - padding and pointers are not
// state, and a hash of them would differ between two builds of the same world.
//
// FNV-1a, because the requirement is "differs when the world differs", not
// "resists an adversary".
uint64_t gg_state_hash(const gg_game *g);

// --- writing ---------------------------------------------------------------
typedef struct {
    SDL_IOStream *io;
    uint32_t      acts;
    bool          open;
} gg_recorder;

// Starts a recording. `map_leaf` is the map a game was built from, or nullptr
// for a generated world - in which case the seed and size are written instead,
// which is everything gg_game_new needs.
bool gg_record_begin(gg_recorder *r, const char *path, const gg_game *g,
                     const char *map_leaf);

// One action, as it is given to the world. Call it with exactly what is passed
// to gg_game_act, and call it for every one: a replay missing an action that
// changed nothing is still a replay that diverges, because "changed nothing"
// is a property of the world at that moment and not of the action.
void gg_record_act(gg_recorder *r, gg_action a);

// A crossing that was performed, once it has happened.
void gg_record_travel(gg_recorder *r, const char *leaf, int x, int y);

// Writes the final state hash and closes. Returns the hash it wrote.
uint64_t gg_record_end(gg_recorder *r, const gg_game *g);

// --- reading ---------------------------------------------------------------
#define GG_REPLAY_MAX 200000

typedef enum {
    GG_STEP_ACT,        // give this action to the world
    GG_STEP_TRAVEL,     // perform this crossing
} gg_step_kind;

typedef struct {
    uint8_t  kind;                     // gg_step_kind
    uint8_t  act;                      // gg_action, for ACT
    char     leaf[GG_MAP_NAME_MAX];    // for TRAVEL
    int16_t  x, y;
} gg_replay_step;

typedef struct {
    uint32_t seed;
    char     map[GG_MAP_NAME_MAX];     // "" for a generated world
    int      w, h;                     // for a generated world
    char     profile[32];
    uint64_t hash;                     // what the recorder ended on
    bool     has_hash;

    gg_replay_step *step;
    int             steps;
} gg_replay;

// Reads a whole replay. Every complaint names the file and the line.
bool gg_replay_load(gg_replay *r, const char *path);
void gg_replay_free(gg_replay *r);

// The name of an action, and the action of a name. The same table both ways, so
// a replay file cannot be written with one spelling and read with another.
const char *gg_action_name(gg_action a);
gg_action   gg_action_from_name(const char *name);

#endif // GG_REPLAY_H
