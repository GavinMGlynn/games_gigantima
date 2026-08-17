// gg_common.h - shared types, world constants, and the deterministic RNG.
#ifndef GG_COMMON_H
#define GG_COMMON_H

#include <SDL3/SDL.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// MSVC's C mode has no C23 `nullptr` keyword, and Windows x64 is a supported
// target. CMake probes for the keyword and defines GG_NO_NULLPTR only when the
// compiler genuinely lacks it, so this never shadows a real keyword - a future
// MSVC that implements it simply stops failing the probe and this disappears.
#ifdef GG_NO_NULLPTR
#  define nullptr ((void *)0)
#endif

#define GG_COUNTOF(a) (sizeof(a) / sizeof((a)[0]))

// ---------------------------------------------------------------------------
// The world grid
// ---------------------------------------------------------------------------
// One tile, in texels. The LPC Revised art is a 32-pixel style; this is not a
// free parameter.
#define GG_TILE 32

// The logical viewport, in tiles and then in pixels. 25x17 tiles at 32px is
// 800x544 - wide enough to read a town's layout at a glance, which is the
// whole point of Ultima VI's single-scale world, and small enough that the
// player is never hunting for their own avatar.
//
// The odd counts matter: with an odd number of rows and columns the avatar
// sits in the exact centre tile, so the view is symmetric in every direction.
#define GG_VIEW_TILES_X 25
#define GG_VIEW_TILES_Y 17
#define GG_VIEW_W (GG_VIEW_TILES_X * GG_TILE)
#define GG_VIEW_H (GG_VIEW_TILES_Y * GG_TILE)

// The HUD occupies a band under the world view rather than floating over it:
// an Ultima-like shows a lot of text, and text over terrain is unreadable.
#define GG_HUD_H 128
#define GG_SCREEN_W GG_VIEW_W
#define GG_SCREEN_H (GG_VIEW_H + GG_HUD_H)

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
// The simulation is turn-based, but the presentation is not: walk cycles,
// water shimmer and the day/night fade all animate continuously while the
// world waits for the player's next move. So there is still a fixed tick, and
// it drives animation and the interpolation of a move in progress.
//
// 60 Hz exactly, in whole nanoseconds.
#define GG_TICK_NS 16666667ULL
#define GG_TICK_HZ (1000000000.0 / (double)GG_TICK_NS)

// Ticks for the avatar to slide from one tile to the next. Ultima VI snapped;
// interpolating reads far better at 32px and costs nothing, because the
// simulation has already resolved the move - this is presentation only.
#define GG_STEP_TICKS 8

// Never make up more than this many ticks after a stall (a window drag, a
// breakpoint). Dropping time is better than fast-forwarding the animation.
#define GG_MAX_CATCHUP_TICKS 12

// ---------------------------------------------------------------------------
// World clock
// ---------------------------------------------------------------------------
// Ultima VI ran a day in real minutes and hung NPC schedules off it. One game
// minute per turn is the classic rate: a walk across a town is a quarter hour,
// and a full day is 1440 turns.
#define GG_MINUTES_PER_TURN 1
#define GG_MINUTES_PER_DAY  (24 * 60)

// ---------------------------------------------------------------------------
// Deterministic RNG
// ---------------------------------------------------------------------------
// xorshift32. The whole simulation is integer-only and seeded, so a run is
// byte-for-byte reproducible - which is what makes a bug report, a replay and
// a generated world all repeatable from a single number.
typedef struct { uint32_t s; } gg_rng;

static inline void gg_rng_seed(gg_rng *r, uint32_t seed) {
    r->s = seed ? seed : 0x9E3779B9u;   // zero is a fixed point; avoid it
}

static inline uint32_t gg_rand(gg_rng *r) {
    uint32_t x = r->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return r->s = x;
}

// Uniform in [0, n). Biased for very large n, which no caller here uses.
static inline uint32_t gg_rand_below(gg_rng *r, uint32_t n) {
    return n ? gg_rand(r) % n : 0;
}

// The same, as an int, for the common case of indexing something whose size is
// already an int. Every call site would otherwise need a cast, and under
// -Wsign-conversion an un-cast one is a build failure - so the cast lives here
// once, next to the check that makes it safe, rather than being sprinkled
// through the callers where it reads as noise.
static inline int gg_rand_belowi(gg_rng *r, int n) {
    return n > 0 ? (int)gg_rand_below(r, (uint32_t)n) : 0;
}

// Inclusive of both ends. A reversed range yields `lo` rather than reading a
// wild count, because a caller that swapped its arguments should get a dull
// answer, not an out-of-range one.
static inline int gg_rand_range(gg_rng *r, int lo, int hi) {
    return hi > lo ? lo + (int)gg_rand_below(r, (uint32_t)(hi - lo + 1)) : lo;
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static inline int gg_clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int gg_absi(int v) { return v < 0 ? -v : v; }

// Chebyshev distance - the number of moves between two tiles when diagonals
// cost the same as orthogonals, which is how this world moves.
static inline int gg_dist_cheb(int ax, int ay, int bx, int by) {
    const int dx = gg_absi(ax - bx), dy = gg_absi(ay - by);
    return dx > dy ? dx : dy;
}

// --- who lives in the world -------------------------------------------------
// These sit here rather than in gg_actor.h because the *map* records people and
// their days, and gg_world.h is below gg_actor.h - putting them there made the
// two headers include each other, which is a compile error wearing a design
// mistake's clothes.
// A place: a region's name, and the town a person says they live in. The same
// width as a map's name, because both are things one file refers to another by.
#define GG_PLACE_MAX      48
#define GG_MAP_NAME_MAX   48

// A flag: one fact the story remembers, by name. Here rather than beside the
// quests because the dialogue book raises them too.
#define GG_FLAG_MAX       24

#define GG_ACTOR_NAME_MAX 24
#define GG_SCHEDULE_MAX   6

// One entry of a daily routine: from `hour`, be at (x, y). Ultima VI's NPCs
// were memorable because they were somewhere for a reason at every hour, so
// this is core rather than decoration.
typedef struct {
    uint8_t hour;
    int16_t x, y;
} gg_sched_entry;

#endif // GG_COMMON_H
