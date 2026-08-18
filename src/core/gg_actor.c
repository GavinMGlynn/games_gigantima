// gg_actor.c - actor movement, walk animation and schedule lookup.
#include "core/gg_actor.h"

// The words an order is given in. One place, so the file that reads them and
// the log that reports them cannot drift apart.
const char *const GG_STANCE_NAME[GG_STANCE_COUNT] = {
    "follow", "stand", "back",
};

void gg_actor_draw_pos(const gg_actor *a, int *out_x, int *out_y) {
    const int tx = a->x * GG_TILE, ty = a->y * GG_TILE;
    if (a->step == 0) {
        *out_x = tx;
        *out_y = ty;
        return;
    }
    // `step` counts down, so it is the distance still to travel: at step ==
    // GG_STEP_TICKS the actor is wholly on the tile it left.
    const int fx = a->from_x * GG_TILE, fy = a->from_y * GG_TILE;
    *out_x = tx + (fx - tx) * a->step / GG_STEP_TICKS;
    *out_y = ty + (fy - ty) * a->step / GG_STEP_TICKS;
}

uint8_t gg_facing_from_delta(int dx, int dy) {
    if (gg_absi(dx) > gg_absi(dy))
        return dx > 0 ? GG_FACE_RIGHT : GG_FACE_LEFT;
    if (dy != 0)
        return dy > 0 ? GG_FACE_DOWN : GG_FACE_UP;
    return GG_FACE_DOWN;
}

void gg_actor_move_to(gg_actor *a, int nx, int ny) {
    a->from_x = a->x;
    a->from_y = a->y;
    a->facing = gg_facing_from_delta(nx - a->x, ny - a->y);
    a->x = (int16_t)nx;
    a->y = (int16_t)ny;
    a->step = GG_STEP_TICKS;
}

void gg_actor_animate(gg_actor *a) {
    if (a->step > 0) {
        a->step--;
        // The phase runs on across tile boundaries rather than restarting at
        // each one. Resetting it every step played the first half of the walk
        // cycle over and over, which looks like a limp - the legs have to keep
        // time with the feet across a whole run of tiles, not within one.
        if ((a->step & 1) == 0)
            a->anim = (uint8_t)((a->anim + 1) % GG_ACTOR_FRAMES);
        a->idle = 0;
    } else if (a->idle < 255) {
        a->idle++;
        // Settle to the standing pose only after a moment. Snapping to it the
        // instant a step ends makes a chain of steps flicker through frame 0
        // between every tile, because held movement re-issues on the next tick.
        if (a->idle > 3) a->anim = 0;
    }
}

bool gg_actor_target_at(const gg_actor *a, int hour, int *tx, int *ty) {
    if (a->schedn == 0) return false;

    // Entries are kept in ascending hour order; the active one is the last
    // whose hour has passed. Before the first entry the day wraps, so the
    // final entry is still in force - which is what makes an NPC who goes to
    // bed at 22:00 still be in bed at 02:00.
    const gg_sched_entry *best = &a->sched[a->schedn - 1];
    for (int i = 0; i < a->schedn; i++)
        if (a->sched[i].hour <= hour) best = &a->sched[i];

    *tx = best->x;
    *ty = best->y;
    return true;
}

bool gg_actor_occupied(const gg_actor *list, int n, int x, int y, int skip) {
    for (int i = 0; i < n; i++) {
        if (i == skip || !list[i].active) continue;
        if (list[i].x == x && list[i].y == y) return true;
    }
    return false;
}

void gg_actor_step_toward(gg_actor *a, const gg_map *m,
                          const gg_actor *others, int nothers,
                          int tx, int ty, gg_rng *rng) {
    if (a->x == tx && a->y == ty) return;

    const int dx = (tx > a->x) - (tx < a->x);
    const int dy = (ty > a->y) - (ty < a->y);

    // Try the diagonal first, then each axis alone. The axis order is decided
    // by which one has further to run, so an NPC crossing a square moves along
    // the diagonal rather than tracing two sides of a triangle.
    int cand[3][2];
    int n = 0;
    cand[n][0] = dx; cand[n][1] = dy; n++;
    if (gg_absi(tx - a->x) >= gg_absi(ty - a->y)) {
        cand[n][0] = dx; cand[n][1] = 0; n++;
        cand[n][0] = 0;  cand[n][1] = dy; n++;
    } else {
        cand[n][0] = 0;  cand[n][1] = dy; n++;
        cand[n][0] = dx; cand[n][1] = 0; n++;
    }

    for (int i = 0; i < n; i++) {
        if (cand[i][0] == 0 && cand[i][1] == 0) continue;
        const int nx = a->x + cand[i][0], ny = a->y + cand[i][1];
        if (!gg_map_walkable(m, nx, ny)) continue;
        if (gg_actor_occupied(others, nothers, nx, ny, -1)) continue;
        gg_actor_move_to(a, nx, ny);
        return;
    }

    // Boxed in. Take a random legal step rather than standing still, so a
    // knot of NPCs in a doorway unpicks itself instead of deadlocking.
    static const int DIRS[8][2] = {
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
        { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 },
    };
    const int start = (int)gg_rand_below(rng, 8);
    for (int k = 0; k < 8; k++) {
        const int *d = DIRS[(start + k) % 8];
        const int nx = a->x + d[0], ny = a->y + d[1];
        if (!gg_map_walkable(m, nx, ny)) continue;
        if (gg_actor_occupied(others, nothers, nx, ny, -1)) continue;
        gg_actor_move_to(a, nx, ny);
        return;
    }
}
