// gg_path.h - A* over the tile grid, with a budget.
//
// Replaces greedy stepping, which could not get an NPC round a wall: it walked
// straight at its target and pressed against whatever was between them. In a
// town made of buildings that is most of the time.
//
// Deliberately independent of the rest of the simulation. It knows nothing
// about maps or actors - passability arrives as a callback - so it can be
// tested against a hand-drawn maze with no world at all, and so the caller
// decides what counts as an obstacle without this file growing a list.
#ifndef GG_PATH_H
#define GG_PATH_H

#include "core/gg_common.h"

// Integer step costs. 10 and 14 approximate 1 and sqrt(2) closely enough that
// paths look right, and keep the whole search in integers - the simulation is
// reproducible from its seed and floating point would put that at risk.
#define GG_STEP_ORTHO 10
#define GG_STEP_DIAG  14

// Can an actor stand at (x, y)? Off-map must return false.
typedef bool (*gg_passable_fn)(void *ctx, int x, int y);

// Scratch space for one search, sized to a map. Held by the caller and reused,
// because a search touches thousands of cells and allocating per call would
// dominate the cost of the search itself.
typedef struct {
    int      w, h;
    uint16_t *gcost;   // cost from the start, in GG_STEP_* units
    int32_t  *came;    // predecessor cell index, or -1
    uint32_t *stamp;   // which search last wrote this cell
    uint32_t  gen;     // current search's stamp, so nothing needs clearing
    int32_t  *heap;    // binary min-heap of cell indices
    uint32_t *fcost;   // g + h, the heap's ordering key
    int       heapn;
} gg_pathfinder;

bool gg_path_init(gg_pathfinder *pf, int w, int h);
void gg_path_free(gg_pathfinder *pf);

// The first step of a path from (sx, sy) to (tx, ty), written to (*nx, *ny).
//
// `budget` caps how many cells the search may expand. A town crossing costs a
// few hundred; the cap exists so that one unreachable target cannot stall a
// turn while every NPC searches the whole map.
//
// Returns false only when there is nowhere to go at all. If the target cannot
// be reached - walled off, or the budget ran out - the step heads toward the
// closest cell the search did reach, which is what makes a blocked NPC edge
// around an obstacle instead of standing still.
//
// The target itself is not required to be passable: an actor can path *to* a
// tile occupied by whoever it is walking up to.
bool gg_path_next_step(gg_pathfinder *pf, gg_passable_fn passable, void *ctx,
                       int sx, int sy, int tx, int ty, int budget,
                       int *nx, int *ny);

// Octile distance in GG_STEP_* units - the exact cost of an unobstructed path,
// and therefore an admissible heuristic.
static inline uint32_t gg_path_heuristic(int ax, int ay, int bx, int by) {
    const int dx = gg_absi(ax - bx), dy = gg_absi(ay - by);
    const int lo = dx < dy ? dx : dy, hi = dx < dy ? dy : dx;
    return (uint32_t)(GG_STEP_ORTHO * hi + (GG_STEP_DIAG - GG_STEP_ORTHO) * lo);
}

#endif // GG_PATH_H
