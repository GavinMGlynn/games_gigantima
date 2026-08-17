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
#include "core/gg_magic.h"
#include "core/gg_quest.h"

typedef enum {
    GG_MODE_TITLE,       // title screen, before a world exists
    GG_MODE_PLAY,        // walking the world
    GG_MODE_CONVERSE,    // talking to somebody
    GG_MODE_PACK,        // looking through what you carry
    GG_MODE_SPELL,       // choosing a spell to speak
    GG_MODE_JOURNAL,     // reading what has happened so far
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
    GG_ACT_FIGHT,
    GG_ACT_CAST,
    GG_ACT_JOURNAL,
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

// Things worth hearing, emitted by the simulation and drained by the frontend.
// The simulation does not know that audio exists - the same arrangement the
// bump rumble already had, generalised, so that adding a sound is adding a case
// to a switch in one file rather than a hook in the middle of a rule.
typedef enum {
    GG_EV_STEP,     // a footfall
    GG_EV_BUMP,     // walked into something
    GG_EV_BLOW,     // a blow landed
    GG_EV_HURT,     // the Avatar took one
    GG_EV_DIE,      // something fell
    GG_EV_TAKE,     // picked up
    GG_EV_DROP,     // set down
    GG_EV_COIN,     // picked up money
    GG_EV_DOOR,     // a door
    GG_EV_CAST,     // a spell went off
    GG_EV_LEARN,    // a word, or a rune
    GG_EV_COUNT
} gg_event;

// A turn cannot produce many of these; the bound is generous and overflow
// drops the newest rather than growing, because a lost footstep is nothing and
// an unbounded queue is a leak.
#define GG_EVENTS_MAX 16

// How far back the Avatar's footsteps are remembered. One per party slot, plus
// a couple so the tail of the line is following a real path rather than the
// leader's current tile.
#define GG_TRAIL_MAX (GG_PARTY_MAX + 3)

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

    // This world was made by the generator rather than read from a file. It
    // decides who the town is peopled with: the book's residents belong to a
    // generated town, and an authored map is the whole of who lives in it -
    // including when the answer is nobody.
    bool generated;

    // Scratch for NPC pathfinding, reused every turn. Held here rather than
    // inside gg_path so the allocation happens once per game, not once per
    // step - a search touches thousands of cells.
    gg_pathfinder path;

    uint32_t turn;                // player actions taken
    uint32_t minutes;             // world clock, wrapping at GG_MINUTES_PER_DAY
    uint32_t day;

    // Experience is the party's, not one person's. Health and level live on
    // the actor - see gg_actor.h - because a companion needs the same ones.
    int  exp;

    // Where the Avatar has just been, newest first. A companion in slot N walks
    // to trail[N], which is what makes the party file through a doorway one at
    // a time instead of shouldering each other at it. Saved, or a resumed party
    // would bunch up on the first step.
    int16_t trail_x[GG_TRAIL_MAX], trail_y[GG_TRAIL_MAX];
    int     trailn;

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

    // --- magic -------------------------------------------------------------
    // Which spell the book is open at. Which spells are *known* is not stored:
    // it is derived from the runes in `known` above, because a spell you can
    // speak is a spell whose words you have. That is the runic system, and it
    // means the spellbook needs no state of its own.
    int spell_cursor;

    // A light of your own making, and how many turns of it are left. Kept apart
    // from a held torch so that letting a spell lapse cannot put out a torch.
    int light_turns, light_power;

    // --- the story ---------------------------------------------------------
    // How far along each quest is: 0 for not begun, otherwise the number of
    // stages entered. The whole of a quest's state is this one number, which
    // is what makes it a state machine rather than a pile of bookkeeping.
    uint8_t quest[GG_QUESTS_MAX];

    // Flags raised by stages, so quests can interlock without any of them
    // knowing about the others.
    char flag[GG_FLAGS_MAX][GG_FLAG_MAX];
    int  flags;

    // How many have fallen to the party. A quest condition, and the only new
    // number the story needed.
    uint32_t slain;

    // Which entry the journal is open at.
    int journal_cursor;

    // Rolling message log, newest last.
    char log[GG_LOG_LINES][GG_LOG_WIDTH];
    int  logn;

    // Set when something happened that the frontend should react to; the
    // frontend clears them. Keeps SDL out of the simulation.
    bool want_save;

    // The Avatar stepped onto a way out. The simulation says which map and
    // where in it; the frontend says where that map lives on disk, because
    // src/core must not know where content is kept. Cleared by gg_game_travel.
    bool want_travel;
    char travel_to[GG_MAP_NAME_MAX];
    int  travel_x, travel_y;
    bool blocked_bump;            // walked into a wall: worth a rumble

    // What just happened that is worth hearing. Drained by the frontend every
    // frame; the simulation never learns what becomes of them.
    uint8_t event[GG_EVENTS_MAX];
    int     events;

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

// Walks out of this map and into the one at `path`, arriving at (x, y).
//
// Everything about the party goes with them - who they are, what they carry,
// what they know, how far along every quest is, the hour and the day and the
// RNG. What is replaced is the world: the map, the people who live in it, and
// what is lying about in it.
//
// **A map you leave forgets what you did there.** It is re-read from disk on
// the way back, so an item taken off the floor of one map and a brigand killed
// in it are both undone by leaving and returning. Carrying every visited map in
// the save is a named item in docs/COMPLETION_PLAN.md.
//
// Returns false and changes nothing if the map cannot be read.
bool gg_game_travel(gg_game *g, const char *path, int x, int y);

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

// --- things worth hearing --------------------------------------------------
// Notes that something happened. Silently drops the newest when the queue is
// full: a lost footstep is nothing, and a queue that grows is a leak.
void gg_emit(gg_game *g, gg_event e);

// Takes up to `max` of them, oldest first, and empties the queue. Returns how
// many were taken.
int gg_events_drain(gg_game *g, gg_event *out, int max);

// Maps a direction action to a tile delta. Returns false for non-movement
// actions, which is how the caller tells them apart.
bool gg_action_delta(gg_action a, int *dx, int *dy);

// --- the party -------------------------------------------------------------
// How many walk with the Avatar, not counting the Avatar.
int gg_party_size(const gg_game *g);

// The actor index in party slot `slot` (1-based), or -1.
int gg_party_at(const gg_game *g, int slot);

// Takes `who` into the party, giving them the next free slot and stopping them
// keeping their daily schedule. Returns false if the party is full or they are
// already in it.
bool gg_party_join(gg_game *g, int who);

// Puts `who` back to their own life, and closes the gap in the line so nobody
// is left following an empty slot.
void gg_party_leave(gg_game *g, int who);

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

// --- the story -------------------------------------------------------------
// Has this flag been raised? Case-insensitive, like every other word.
bool gg_flag(const gg_game *g, const char *name);

// Raises one. Returns false if it was already up or there is no room.
bool gg_raise_flag(gg_game *g, const char *name);

// Advances every quest as far as its conditions allow, writing journal lines
// and raising flags as stages are entered. Called once a turn; safe to call
// more often, because a stage is only ever entered once.
void gg_quests_tick(gg_game *g);

// How many journal lines there are, and the i'th of them. Built from the
// quests and how far along each is, rather than stored - the state is the
// stage number, and everything else is read out of the book.
int gg_journal_lines(const gg_game *g);
bool gg_journal_line(const gg_game *g, int i, const char **quest_name,
                     const char **text, bool *done);

// --- magic -----------------------------------------------------------------
// Does the player know every rune this spell is made of? That is the whole of
// knowing a spell: there is no spellbook to be given, only words to collect.
bool gg_spell_known(const gg_game *g, int spell);

// Are the reagents in the pack? Separate from knowing it, because "thou hast
// no ginseng" and "thou knowest no such words" are different problems and a
// player should be told which one they have.
bool gg_spell_afford(const gg_game *g, int spell);

// Speaks it: spends the reagents and does the thing. Returns false, having
// spent nothing, if it is not known, not affordable, or has nothing to aim at.
bool gg_cast(gg_game *g, int spell);

// The next spell in the book that is worth showing, from `from` inclusive, in
// the direction `step`. Returns -1 when the book holds nothing known.
int gg_spell_next(const gg_game *g, int from, int step);

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
