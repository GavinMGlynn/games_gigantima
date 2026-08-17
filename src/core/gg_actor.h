// gg_actor.h - the avatar, companions and townsfolk, and the daily schedules
// that decide where a townsperson ought to be right now.
#ifndef GG_ACTOR_H
#define GG_ACTOR_H

#include "core/gg_common.h"
#include "core/gg_world.h"

#define GG_ACTOR_NAME_MAX 24
#define GG_SCHEDULE_MAX   6
#define GG_ACTORS_MAX     256
#define GG_ACTOR_NO_DEF   255

// One entry of a daily routine: from `hour`, be at (x, y). Ultima VI's NPCs
// were memorable because they were somewhere for a reason at every hour, so
// this is core rather than decoration.
typedef struct {
    uint8_t hour;
    int16_t x, y;
} gg_sched_entry;

typedef struct {
    bool    active;
    uint8_t art;          // gg_actor_id - which sprite block to draw
    uint8_t facing;       // gg_facing
    char    name[GG_ACTOR_NAME_MAX];

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
