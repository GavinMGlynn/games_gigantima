// gg_game.h - the simulation: world state, the turn loop and the world clock.
//
// The rule that shapes this file: **the world only advances when the player
// acts.** That is what "turn-based" means here, and it is Ultima VI's model -
// stand still and the town stands still with you. Animation is not part of it;
// see gg_common.h on why there is still a 60 Hz tick.
#ifndef GG_GAME_H
#define GG_GAME_H

#include "core/gg_common.h"
#include "core/gg_world.h"
#include "core/gg_actor.h"
#include "core/gg_path.h"
#include "core/gg_dialogue.h"

typedef enum {
    GG_MODE_TITLE,       // title screen, before a world exists
    GG_MODE_PLAY,        // walking the world
    GG_MODE_CONVERSE,    // talking to somebody
    GG_MODE_PACK,        // looking through what you carry
    GG_MODE_GAMEOVER,
} gg_mode;

// Everything the player can ask for in one turn. Kept as an enum rather than
// raw keys so that the keyboard, the gamepad and a future replay file all feed
// the simulation through one door.
typedef enum {
    GG_ACT_NONE,
    GG_ACT_N, GG_ACT_S, GG_ACT_E, GG_ACT_W,
    GG_ACT_NE, GG_ACT_NW, GG_ACT_SE, GG_ACT_SW,
    GG_ACT_WAIT,
    GG_ACT_TALK,
    GG_ACT_LOOK,
    GG_ACT_OPEN,

    // The pack. GET acts on the ground; the rest act on whichever slot the
    // pack's cursor is on, and the cursor is moved with the ordinary direction
    // actions while the pack is open. Keeping the cursor in the simulation
    // rather than in the UI is what lets a replay file drive all of this
    // through the same door as a keypress - see the deterministic-replay item.
    GG_ACT_GET,
    GG_ACT_PACK,
    GG_ACT_USE,
    GG_ACT_EQUIP,
    GG_ACT_DROP,

    GG_ACT_COUNT
} gg_action;

// How many cells one NPC's search may expand in a turn. A town crossing costs
// a few hundred; the cap is what stops a single unreachable target stalling a
// turn while every resident searches the whole map.
#define GG_PATH_BUDGET 400

#define GG_LOG_LINES 5
#define GG_LOG_WIDTH 96

// How many words the player may collect. Generous: the vocabulary is the whole
// of the story state, so running out would silently stop the plot.
#define GG_KNOWN_MAX 64

// --- the pack --------------------------------------------------------------
// `gg_item_id` and the table describing each kind are generated from the art by
// tools/make_atlas.py - see core/gg_ids.h. What lives here is what the player
// is carrying.
//
// Slots rather than a counter per kind: a slot holds one kind and a count, and
// two slots may hold the same kind once stacking is refused. That is the shape
// a container model - Ultima VII's bags inside bags, a named plan item - grows
// out of without the save file having to change meaning.
#define GG_PACK_MAX 24

// What can be carried, in the hundredths of a stone the item table uses: 30
// stone. A constant rather than a function of strength because there is no
// strength stat yet. When there is, this becomes a function of it and nothing
// else here changes.
#define GG_CARRY_MAX 3000

typedef struct {
    uint8_t kind;      // gg_item_id
    uint8_t count;     // never zero: an empty slot is removed, not kept
} gg_pack_slot;

typedef struct {
    gg_map   map;
    gg_actor actor[GG_ACTORS_MAX];
    int      actors;
    int      player;              // index into actor[]

    gg_rng   rng;
    gg_mode  mode;

    // Scratch for NPC pathfinding, reused every turn. Held here rather than
    // inside gg_path so the allocation happens once per game, not once per
    // step - a search touches thousands of cells.
    gg_pathfinder path;

    uint32_t turn;                // player actions taken
    uint32_t minutes;             // world clock, wrapping at GG_MINUTES_PER_DAY
    uint32_t day;

    // The avatar. Named `hp`/`hp_max` rather than a stats block because the
    // stats that matter are not settled yet - see the plan.
    int  hp, hp_max;
    int  level, exp;

    // What is carried, what the pack screen is looking at, and what is held.
    // `equipped` holds a pack index per slot, or -1. An index rather than an
    // item id, so that holding one of three torches is unambiguous.
    gg_pack_slot pack[GG_PACK_MAX];
    int          packn;
    int          pack_cursor;
    int          equipped[GG_SLOT_COUNT];

    // --- conversation, while mode == GG_MODE_CONVERSE ----------------------
    int  talking_to;

    // The words the player has collected. This is the whole of the branching:
    // a topic is askable when its word is known, so what somebody told you an
    // hour ago in another house is what makes this question possible now.
    char known[GG_KNOWN_MAX][GG_WORD_MAX];
    int  knownn;

    // Who is speaking, resolved from the actor's name on entry. Not saved -
    // it points into the loaded book, and a pointer in a file is a pointer
    // into the wrong process. Rebuilt the same way greetings are.
    const gg_speaker *speaker;

    // What they are saying now, and which of the words they will answer to is
    // under the cursor. The list is rebuilt whenever a word is learned, so a
    // topic that has just been unlocked appears without leaving the panel.
    char said[GG_TOPIC_LINES_MAX][GG_LINE_MAX];
    int  saids;
    char askable[GG_TOPICS_MAX][GG_WORD_MAX];
    int  askables;
    int  ask_cursor;

    // Rolling message log, newest last.
    char log[GG_LOG_LINES][GG_LOG_WIDTH];
    int  logn;

    // Set when something happened that the frontend should react to; the
    // frontend clears them. Keeps SDL out of the simulation.
    bool want_save;
    bool blocked_bump;            // walked into a wall: worth a sound

    char profile[32];             // whose game this is
} gg_game;

// --- lifetime --------------------------------------------------------------
// Builds a fresh world from `seed` and places the avatar. Returns false only
// if the map could not be allocated.
bool gg_game_new(gg_game *g, uint32_t seed, const char *profile);

// The same, on a map loaded from a file rather than generated. This is the
// path the level editor's output takes, and the one that makes an authored
// test scene playable - which is how the shoreline corners were checked
// against a coastline the generator would never produce.
bool gg_game_new_from_map(gg_game *g, const char *path, const char *profile);

void gg_game_free(gg_game *g);

// Restores what a save file cannot carry: the greeting pointers, rebuilt from
// each actor's `def` index. Called by the save loader; harmless otherwise.
void gg_game_rebind_actors(gg_game *g);

// --- the turn loop ---------------------------------------------------------
// Applies one player action. Everything else in the world moves as a
// consequence of this call and at no other time.
void gg_game_act(gg_game *g, gg_action a);

// Advances animation only. Called once per 60 Hz tick, including while the
// simulation is idle waiting for input.
void gg_game_animate(gg_game *g);

// --- queries ---------------------------------------------------------------
static inline gg_actor *gg_player(gg_game *g) { return &g->actor[g->player]; }
static inline const gg_actor *gg_player_const(const gg_game *g) {
    return &g->actor[g->player];
}

int  gg_game_hour(const gg_game *g);
int  gg_game_minute(const gg_game *g);

// 0 at midnight, 255 at noon - the outdoor light level. The renderer turns
// this into a colour; the simulation only says how bright it is.
uint8_t gg_game_daylight(const gg_game *g);

// The actor adjacent to the player in the direction they face, or -1.
int  gg_game_facing_actor(const gg_game *g);

// Where the player is, in words: "Britain" or "the wilderness".
const char *gg_game_place(const gg_game *g);

void gg_log(gg_game *g, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
    SDL_PRINTF_VARARG_FUNC(2);

// Maps a direction action to a tile delta. Returns false for non-movement
// actions, which is how the caller tells them apart.
bool gg_action_delta(gg_action a, int *dx, int *dy);

// --- conversation ----------------------------------------------------------
// Does the player know this word? Case-insensitive: it came from a person.
bool gg_knows(const gg_game *g, const char *word);

// Adds a word to what the player knows. Returns false if it was already known
// or there is no room, so a caller can tell whether anything was learned.
bool gg_learn(gg_game *g, const char *word);

// Rebuilds the list of words the person being spoken to will answer to, out of
// the words the player knows. Called on entering a conversation and again
// whenever a word is learned, so a topic that has just been unlocked appears
// without the player having to leave and come back.
void gg_conversation_refresh(gg_game *g);

// Asks the word under the cursor. Does nothing outside a conversation.
void gg_conversation_ask(gg_game *g);

// --- the pack --------------------------------------------------------------
// How many of a kind are carried, across every slot holding it.
int gg_pack_count(const gg_game *g, gg_item_id kind);

// What is carried, in hundredths of a stone. Compare against GG_CARRY_MAX.
int gg_pack_weight(const gg_game *g);

// The first slot holding `kind`, or -1.
int gg_pack_find(const gg_game *g, gg_item_id kind);

// Puts `count` of `kind` in the pack, stacking where the kind allows it.
// Returns how many were actually taken - fewer than asked if the weight or the
// slots ran out, and zero if none would fit. Partial rather than all-or-nothing
// because a player standing on a hundred coins should get as many as they can
// carry, not none of them.
int gg_pack_add(gg_game *g, gg_item_id kind, int count);

// Takes `count` from slot `index`. Returns how many were removed. Keeps
// `equipped` correct: emptying a slot unequips it, and the indices above it
// shift down.
int gg_pack_take(gg_game *g, int index, int count);

// Whether the slot holds something, and what is in it.
static inline bool gg_pack_slot_ok(const gg_game *g, int i) {
    return i >= 0 && i < g->packn;
}

// How far the player's own light reaches: from whatever is held in the light
// slot, or a hand's breadth if nothing is. Never zero, so a player who drops
// their last torch underground is in the dark but not blind.
int gg_light_radius(const gg_game *g);

#endif // GG_GAME_H
