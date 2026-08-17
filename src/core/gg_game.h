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

typedef enum {
    GG_MODE_TITLE,       // title screen, before a world exists
    GG_MODE_PLAY,        // walking the world
    GG_MODE_CONVERSE,    // talking to somebody
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
    GG_ACT_COUNT
} gg_action;

#define GG_LOG_LINES 5
#define GG_LOG_WIDTH 96

// Inventory is a fixed table for now. A container model - Ultima VII's bags
// inside bags - is a named item in docs/COMPLETION_PLAN.md; this is the flat
// version that the HUD and the save file can be built against meanwhile.
typedef enum {
    GG_ITEM_FOOD, GG_ITEM_GOLD, GG_ITEM_TORCH, GG_ITEM_KEY,
    GG_ITEM_GEM, GG_ITEM_POTION,
    GG_ITEM_COUNT
} gg_item_id;

extern const char *const GG_ITEM_NAME[GG_ITEM_COUNT];

typedef struct {
    gg_map   map;
    gg_actor actor[GG_ACTORS_MAX];
    int      actors;
    int      player;              // index into actor[]

    gg_rng   rng;
    gg_mode  mode;

    uint32_t turn;                // player actions taken
    uint32_t minutes;             // world clock, wrapping at GG_MINUTES_PER_DAY
    uint32_t day;

    // The avatar. Named `hp`/`hp_max` rather than a stats block because the
    // stats that matter are not settled yet - see the plan.
    int  hp, hp_max;
    int  level, exp;
    int  item[GG_ITEM_COUNT];

    // Who we are talking to, while mode == GG_MODE_CONVERSE.
    int  talking_to;

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

#endif // GG_GAME_H
