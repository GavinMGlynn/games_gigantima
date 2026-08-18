// gg_actor.h - the avatar, companions and townsfolk, and the daily schedules
// that decide where a townsperson ought to be right now.
#ifndef GG_ACTOR_H
#define GG_ACTOR_H

#include "core/gg_common.h"
#include "core/gg_world.h"

#define GG_ACTORS_MAX     256
#define GG_ACTOR_NO_DEF   255

// How many may walk with the Avatar, not counting the Avatar. Ultima VI took
// eight; four is what the follow trail and the HUD can show honestly, and
// raising it is a constant rather than a rewrite.
#define GG_PARTY_MAX      4
#define GG_NOT_IN_PARTY   0

// GG_ACTOR_NAME_MAX, GG_SCHEDULE_MAX and gg_sched_entry are in gg_common.h -
// see the note there on why.

typedef struct {
    bool    active;
    uint8_t art;          // gg_actor_id - which sprite block to draw
    uint8_t facing;       // gg_facing
    char    name[GG_ACTOR_NAME_MAX];

    // Stats. On the actor rather than on the game, because a companion needs
    // the same ones the Avatar has and a second place to keep them is the kind
    // of split that bites the first time something hits the party.
    int16_t hp, hp_max;
    uint8_t level;

    // Where in the line this one walks, 1..GG_PARTY_MAX, or GG_NOT_IN_PARTY.
    // A slot rather than a flag, because who follows whom is the formation.
    uint8_t party;

    // Fighting. `hostile` puts them on the other side; `speed` fills `energy`
    // each turn and an action costs 100 of it, which is what initiative is.
    // `damage` and `guard` are natural ability - claws, or a thick hide - and
    // anything held is added to them.
    bool    hostile;
    uint8_t speed;
    int16_t energy;
    uint8_t damage, guard;

    // Behaviour, out of the bestiary. `reach` above one is something that
    // throws; `notice` is how near you must come; `flees` is the health below
    // which it would rather be elsewhere.
    uint8_t reach, notice;
    int16_t flees;

    // Who last hurt it, as an index *plus one* - so zero is "nobody" and a
    // freshly zeroed actor is not angry with the Avatar by accident. A creature
    // turns on whoever is hurting it, which is what makes bringing somebody
    // worth more than a second sword: while it deals with them, its back is to
    // you.
    uint8_t angered_by;

    // Turns of sleep left. A sleeping thing takes no turns, gathers no
    // initiative, and has its back to everybody - and the first blow wakes it,
    // because nothing sleeps through being hit. This is what makes a spell that
    // does no damage worth a reagent: the fight becomes one fewer at a time.
    uint8_t asleep;

    // Which row of the bestiary this came from, so its loot table can be
    // rolled when it falls. A save cannot write a pointer, and an index into a
    // file that may have been edited is checked on the way back in.
    uint8_t beast;

    int16_t x, y;         // tile the actor occupies now
    int16_t from_x, from_y;   // tile it left, for drawing the slide
    uint8_t step;         // ticks left in the slide; 0 means standing still
    uint8_t anim;         // walk-cycle phase, carried across tile boundaries
    uint8_t idle;         // ticks since the last step, to settle into a stand

    gg_sched_entry sched[GG_SCHEDULE_MAX];
    uint8_t schedn;

    // Which entry of the townsfolk table this actor was made from, or
    // GG_ACTOR_NO_DEF. A save cannot write a pointer, so the greeting below is
    // rebuilt from this on load rather than serialised - and when dialogue
    // moves into map data this becomes the id it moves to.
    uint8_t def;

    // Conversation. An NPC with no topics still greets, which is better than
    // an NPC who cannot be spoken to at all. Not serialised; see `def`.
    const char *greeting;
} gg_actor;

// Interpolated position in texels, for the renderer. Returns the actor's
// current tile scaled up, pulled back toward the tile it came from by however
// much of the step remains.
void gg_actor_draw_pos(const gg_actor *a, int *out_x, int *out_y);

// Which way is (dx, dy)? Ties resolve to the vertical, matching how the LPC
// walk sheets read - a diagonal move shows a side profile only when the
// horizontal component dominates.
uint8_t gg_facing_from_delta(int dx, int dy);

// Start a move to (nx, ny). The actor arrives immediately as far as the
// simulation is concerned; `step` only governs how long the slide is drawn.
void gg_actor_move_to(gg_actor *a, int nx, int ny);

// Advance the walk animation and the slide by one tick.
void gg_actor_animate(gg_actor *a);

// Where should this actor be at `hour`? Returns false if it has no schedule,
// in which case the caller should leave it where it stands.
bool gg_actor_target_at(const gg_actor *a, int hour, int *tx, int *ty);

// One greedy step toward (tx, ty), refusing blocked cells and cells occupied
// by another actor. Greedy rather than A*: townsfolk move within a town they
// were placed in, the distances are short, and a stuck NPC that shuffles is a
// far smaller problem than a pathfinder run for 200 actors every turn.
// Proper pathing is a named item in docs/COMPLETION_PLAN.md.
void gg_actor_step_toward(gg_actor *a, const gg_map *m,
                          const gg_actor *others, int nothers,
                          int tx, int ty, gg_rng *rng);

// Is any active actor standing on this tile? `skip` is excluded so an actor
// can ask about its own destination.
bool gg_actor_occupied(const gg_actor *list, int n, int x, int y, int skip);

#endif // GG_ACTOR_H
