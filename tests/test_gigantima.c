// test_gigantima.c - unit tests for the simulation.
//
// Every test here runs with no window and no audio device (see the ENVIRONMENT
// property in CMakeLists.txt). That is not a convenience - it is the assertion
// that src/core/ is genuinely headless, and it would start failing the moment
// something in the simulation reached for a renderer.
//
// Tests are named as sentences stating the fact they pin down.
#include "core/gg_common.h"
#include "core/gg_game.h"
#include "core/gg_world.h"
#include "platform/gg_paths.h"
#include "gfx/gg_render.h"
#include "gfx/gg_atlas.h"   // GG_EDGE_* piece indices
#include "core/gg_path.h"

#include <stdio.h>

static int g_checks, g_failures;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(cond)) {                                                        \
            g_failures++;                                                     \
            SDL_Log("FAIL %s:%d: %s", __func__, __LINE__, #cond);             \
            SDL_Log("     " __VA_ARGS__);                                     \
        }                                                                     \
    } while (0)

#define RUN(fn)                                                               \
    do {                                                                      \
        const int before = g_failures;                                        \
        fn();                                                                 \
        SDL_Log("%-58s %s", #fn, g_failures == before ? "ok" : "FAILED");     \
    } while (0)

// ---------------------------------------------------------------------------
// RNG
// ---------------------------------------------------------------------------
static void the_rng_is_reproducible_from_its_seed(void) {
    gg_rng a, b;
    gg_rng_seed(&a, 12345);
    gg_rng_seed(&b, 12345);
    for (int i = 0; i < 1000; i++)
        CHECK(gg_rand(&a) == gg_rand(&b), "diverged at %d", i);
}

static void a_zero_seed_does_not_stick_at_zero(void) {
    // xorshift has a fixed point at zero; gg_rng_seed must substitute.
    gg_rng r;
    gg_rng_seed(&r, 0);
    CHECK(gg_rand(&r) != 0, "zero seed produced a zero stream");
    CHECK(gg_rand(&r) != 0, "zero seed produced a zero stream");
}

static void rand_below_stays_in_range(void) {
    gg_rng r;
    gg_rng_seed(&r, 99);
    for (int i = 0; i < 5000; i++) {
        const uint32_t v = gg_rand_below(&r, 7);
        CHECK(v < 7, "got %u", v);
    }
    CHECK(gg_rand_below(&r, 0) == 0, "n == 0 must not divide by zero");
}

// ---------------------------------------------------------------------------
// Map
// ---------------------------------------------------------------------------
static void generation_is_reproducible_from_its_seed(void) {
    gg_map a, b;
    CHECK(gg_map_generate(&a, 96, 80, 777), "first generate failed");
    CHECK(gg_map_generate(&b, 96, 80, 777), "second generate failed");
    CHECK(a.w == b.w && a.h == b.h, "dimensions differ");
    CHECK(SDL_memcmp(a.cell, b.cell, (size_t)a.w * (size_t)a.h * sizeof *a.cell) == 0,
          "same seed produced a different world");
    CHECK(a.start_x == b.start_x && a.start_y == b.start_y, "start differs");
    gg_map_free(&a);
    gg_map_free(&b);
}

static void different_seeds_produce_different_worlds(void) {
    gg_map a, b;
    gg_map_generate(&a, 96, 80, 1);
    gg_map_generate(&b, 96, 80, 2);
    CHECK(SDL_memcmp(a.cell, b.cell,
                     (size_t)a.w * (size_t)a.h * sizeof *a.cell) != 0,
          "two seeds produced identical worlds");
    gg_map_free(&a);
    gg_map_free(&b);
}

static void off_map_tiles_are_never_walkable(void) {
    gg_map m;
    gg_map_generate(&m, 64, 64, 5);
    CHECK(!gg_map_walkable(&m, -1, 10), "walked off the west edge");
    CHECK(!gg_map_walkable(&m, 10, -1), "walked off the north edge");
    CHECK(!gg_map_walkable(&m, m.w, 10), "walked off the east edge");
    CHECK(!gg_map_walkable(&m, 10, m.h), "walked off the south edge");
    CHECK(gg_map_at(&m, -1, 0) == nullptr, "out of bounds must not clamp");
    gg_map_free(&m);
}

static void water_and_mountain_are_impassable(void) {
    gg_map m;
    CHECK(gg_map_alloc(&m, 8, 8), "alloc failed");
    for (int i = 0; i < 64; i++) m.cell[i].terrain = GG_TILE_GRASS;

    CHECK(gg_map_walkable(&m, 4, 4), "grass should be walkable");

    gg_map_at(&m, 4, 4)->terrain = GG_TILE_WATER;
    CHECK(!gg_map_walkable(&m, 4, 4), "water should not be walkable");

    gg_map_at(&m, 4, 4)->terrain = GG_TILE_MOUNTAIN;
    CHECK(!gg_map_walkable(&m, 4, 4), "mountain should not be walkable");
    gg_map_free(&m);
}

static void a_prop_blocks_its_tile_but_ground_cover_does_not(void) {
    gg_map m;
    CHECK(gg_map_alloc(&m, 8, 8), "alloc failed");
    for (int i = 0; i < 64; i++) m.cell[i].terrain = GG_TILE_GRASS;

    gg_map_at(&m, 2, 2)->prop = GG_PROP_TREE_OAK + 1;
    CHECK(!gg_map_walkable(&m, 2, 2), "a tree should block");

    // A lily pad is scenery on water, not an obstacle - it is the one prop
    // deliberately exempted, so it is the one worth pinning.
    gg_map_at(&m, 3, 3)->prop = GG_PROP_LILYPAD + 1;
    CHECK(gg_map_walkable(&m, 3, 3), "a lily pad should not block");
    gg_map_free(&m);
}

static void the_generated_start_tile_is_always_walkable(void) {
    // The generator can place the town over a lake if the seed is unkind, so
    // it walks outward until it finds ground. Sweep a spread of seeds: a
    // generator that can strand the player will, eventually.
    for (uint32_t seed = 1; seed <= 60; seed++) {
        gg_map m;
        CHECK(gg_map_generate(&m, 128, 112, seed), "generate failed at seed %u", seed);
        CHECK(gg_map_walkable(&m, m.start_x, m.start_y),
              "seed %u starts the player inside terrain at %d,%d",
              seed, m.start_x, m.start_y);
        gg_map_free(&m);
    }
}

static void a_saved_map_reloads_byte_for_byte(void) {
    gg_map a, b;
    CHECK(gg_map_generate(&a, 64, 48, 4242), "generate failed");

    const char *path = gg_pref_file("test_roundtrip.ggmap");
    CHECK(gg_map_save(&a, path), "save failed");
    CHECK(gg_map_load(&b, path), "load failed");

    CHECK(a.w == b.w && a.h == b.h, "dimensions changed");
    CHECK(a.start_x == b.start_x && a.start_y == b.start_y, "start changed");
    CHECK(a.seed == b.seed, "seed changed");
    CHECK(a.regions == b.regions, "region count changed: %d vs %d",
          a.regions, b.regions);
    CHECK(SDL_strcmp(a.name, b.name) == 0, "name changed");
    CHECK(SDL_memcmp(a.cell, b.cell,
                     (size_t)a.w * (size_t)a.h * sizeof *a.cell) == 0,
          "cells changed across the round trip");

    SDL_RemovePath(path);
    gg_map_free(&a);
    gg_map_free(&b);
}

static void loading_a_file_that_is_not_a_map_fails_cleanly(void) {
    const char *path = gg_pref_file("test_garbage.ggmap");
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    CHECK(io != nullptr, "could not create the garbage file");
    if (io) {
        SDL_WriteIO(io, "not a map at all, really", 24);
        SDL_CloseIO(io);
    }

    gg_map m;
    SDL_zero(m);
    CHECK(!gg_map_load(&m, path), "garbage was accepted as a map");
    CHECK(m.cell == nullptr, "a rejected load must not leak an allocation");
    SDL_RemovePath(path);

    CHECK(!gg_map_load(&m, gg_pref_file("test_no_such_file.ggmap")),
          "a missing file was accepted");
}

// ---------------------------------------------------------------------------
// Pathfinding
//
// Tested against a hand-drawn maze rather than a generated world: gg_path
// knows nothing about maps or actors, so a plain character grid is the whole
// context it needs, and a failure points at the search rather than at whatever
// the generator happened to produce.
// ---------------------------------------------------------------------------
typedef struct { const char *const *rows; int w, h; } maze;

static bool maze_passable(void *ctx, int x, int y) {
    const maze *m = ctx;
    if (x < 0 || y < 0 || x >= m->w || y >= m->h) return false;
    return m->rows[y][x] != '#';
}

// Walks the path one step at a time, as the turn loop does, and returns how
// many steps it took to arrive - or -1 if it never did.
static int walk_maze(const maze *mz, int sx, int sy, int tx, int ty, int budget) {
    gg_pathfinder pf;
    if (!gg_path_init(&pf, mz->w, mz->h)) return -1;

    int x = sx, y = sy, steps = 0;
    while (!(x == tx && y == ty) && steps < mz->w * mz->h) {
        int nx, ny;
        if (!gg_path_next_step(&pf, maze_passable, (void *)mz, x, y, tx, ty,
                               budget, &nx, &ny))
            break;
        if (nx == x && ny == y) break;        // no progress: would loop forever
        x = nx; y = ny;
        steps++;
    }
    gg_path_free(&pf);
    return (x == tx && y == ty) ? steps : -1;
}

static void a_path_goes_around_a_wall(void) {
    // The case greedy stepping could not do at all: the target is straight
    // ahead, and the only way there is round the side.
    static const char *const ROWS[] = {
        ".........",
        ".........",
        "####.####",
        "####.####",
        "####.####",
        ".........",
        ".........",
    };
    const maze mz = { ROWS, 9, 7 };

    const int steps = walk_maze(&mz, 1, 0, 1, 6, 400);
    CHECK(steps > 0, "no path found around the wall");
    // Straight down is 6, and there is no straight way down: the only gap is
    // the corridor at x=4, so any honest path is longer.
    CHECK(steps > 6, "the path cut through the wall in %d steps", steps);
    CHECK(steps <= 20, "the path wandered: %d steps", steps);
}

static void a_path_solves_a_serpentine_maze(void) {
    // Long, single-solution, and doubling back on itself, so no amount of
    // heading toward the target gets there. Greedy stepping cannot solve this.
    static const char *const ROWS[] = {
        "###########",
        "#.........#",
        "#########.#",
        "#.........#",
        "#.#########",
        "#.........#",
        "#########.#",
        "#....T....#",
        "###########",
    };
    const maze mz = { ROWS, 11, 9 };
    const int steps = walk_maze(&mz, 1, 1, 5, 7, 4000);
    CHECK(steps > 0, "no path through the maze");
    // The corridors force roughly 8 + 2 + 8 + 2 + 8 + 2 + 4 tiles of travel.
    CHECK(steps >= 25, "the maze was solved suspiciously fast: %d steps", steps);
    CHECK(steps <= 45, "the path wandered: %d steps", steps);
}

static void a_path_never_cuts_a_diagonal_corner(void) {
    // Two walls meeting at a point must not be slipped through. Without the
    // check an actor leaves a sealed room by walking through the join, which
    // looks like walking through a wall.
    static const char *const ROWS[] = {
        ".....",
        ".#...",
        "#....",     // the gap between the two walls is a corner, not a door
        ".....",
        ".....",
    };
    const maze mz = { ROWS, 5, 5 };

    gg_pathfinder pf;
    CHECK(gg_path_init(&pf, 5, 5), "init failed");
    int nx, ny;
    // From (0,1) the only diagonal toward (1,2) squeezes between (1,1) and
    // (0,2), both walls.
    if (gg_path_next_step(&pf, maze_passable, (void *)&mz, 0, 1, 1, 3, 400,
                          &nx, &ny)) {
        CHECK(!(nx == 1 && ny == 2),
              "the path cut the corner between two walls");
    }
    gg_path_free(&pf);
}

static void an_unreachable_target_still_moves_toward_it(void) {
    // A walled-off target must not freeze the actor: it should close the
    // distance as far as it can. That is what makes a resident whose schedule
    // point is behind a locked wall stand against the wall rather than at
    // wherever it happened to be standing.
    static const char *const ROWS[] = {
        ".....#....",
        ".....#....",
        ".....#....",
        ".....#....",
        ".....#....",
    };
    const maze mz = { ROWS, 10, 5 };

    gg_pathfinder pf;
    CHECK(gg_path_init(&pf, 10, 5), "init failed");
    int nx, ny;
    const bool ok = gg_path_next_step(&pf, maze_passable, (void *)&mz,
                                      0, 2, 9, 2, 400, &nx, &ny);
    CHECK(ok, "an unreachable target should still yield a step");
    if (ok) {
        CHECK(gg_path_heuristic(nx, ny, 9, 2) < gg_path_heuristic(0, 2, 9, 2),
              "the step at %d,%d did not close the distance", nx, ny);
        CHECK(maze_passable((void *)&mz, nx, ny), "stepped into a wall");
    }
    gg_path_free(&pf);
}

static void a_completely_boxed_in_actor_reports_no_step(void) {
    static const char *const ROWS[] = {
        "###",
        "#.#",
        "###",
    };
    const maze mz = { ROWS, 3, 3 };

    gg_pathfinder pf;
    CHECK(gg_path_init(&pf, 3, 3), "init failed");
    int nx, ny;
    CHECK(!gg_path_next_step(&pf, maze_passable, (void *)&mz, 1, 1, 0, 0, 400,
                             &nx, &ny),
          "a sealed cell should report no step at all");
    gg_path_free(&pf);
}

static void the_search_is_reproducible(void) {
    // Two nodes with equal f must come out of the heap in the same order every
    // run, or a seeded world stops being reproducible. An open field is the
    // worst case: almost every node ties with several others.
    static const char *const ROWS[] = {
        "...........",
        "...........",
        "...........",
        "...........",
        "...........",
    };
    const maze mz = { ROWS, 11, 5 };

    int first_x = -1, first_y = -1;
    for (int run = 0; run < 8; run++) {
        gg_pathfinder pf;
        CHECK(gg_path_init(&pf, 11, 5), "init failed");
        int nx = -1, ny = -1;
        CHECK(gg_path_next_step(&pf, maze_passable, (void *)&mz, 0, 2, 10, 2,
                                400, &nx, &ny), "no step in an open field");
        if (run == 0) { first_x = nx; first_y = ny; }
        CHECK(nx == first_x && ny == first_y,
              "run %d chose %d,%d where run 0 chose %d,%d",
              run, nx, ny, first_x, first_y);
        gg_path_free(&pf);
    }
}

static void a_resident_crosses_the_town_to_a_fixed_target(void) {
    // The reason pathfinding exists: greedy stepping left a resident pressed
    // against whatever building stood between it and where it was due.
    //
    // One resident with one fixed target, and the rest stood down. Measuring
    // the *scheduled* positions instead was tried and is a trap: somebody's
    // schedule turns over on nearly every hour, so whatever moment the test
    // picks, someone has just been given a new target and had no time to walk
    // to it. That is the clock moving, not the pathing failing.
    gg_game g;
    CHECK(gg_game_new(&g, 91, "Tester"), "new game failed");
    CHECK(g.actors > 1, "the town has no residents to test");

    for (int i = 1; i < g.actors; i++) g.actor[i].active = false;
    gg_actor *a = &g.actor[1];
    a->active = true;

    // A target on the far side of the town square, reachable but with the
    // buildings between - found by walking outward from a guess until a
    // walkable cell turns up, so the test does not depend on the exact layout.
    int tx = g.map.start_x + 12, ty = g.map.start_y + 10;
    for (int r = 0; r < 20 && !gg_map_walkable(&g.map, tx, ty); r++) {
        tx = g.map.start_x + 12 - r;
        ty = g.map.start_y + 10 - r;
    }
    CHECK(gg_map_walkable(&g.map, tx, ty), "could not find a target to walk to");

    const int start_d = gg_dist_cheb(a->x, a->y, tx, ty);
    CHECK(start_d > 8, "the target is too close to prove anything: %d", start_d);

    a->schedn = 1;
    a->sched[0] = (gg_sched_entry){ .hour = 0, .x = (int16_t)tx, .y = (int16_t)ty };

    for (int t = 0; t < 300; t++) {
        gg_game_act(&g, GG_ACT_WAIT);
        if (a->x == tx && a->y == ty) break;
    }

    CHECK(a->x == tx && a->y == ty,
          "%s got from %d tiles away to %d, never arriving at %d,%d",
          a->name, start_d, gg_dist_cheb(a->x, a->y, tx, ty), tx, ty);
    gg_game_free(&g);
}

static void a_resident_walks_round_a_building_rather_than_into_it(void) {
    // The specific failure greedy stepping had: put a wall squarely between an
    // actor and its target and it pressed against the wall for ever.
    gg_game g;
    CHECK(gg_game_new(&g, 92, "Tester"), "new game failed");
    for (int i = 1; i < g.actors; i++) g.actor[i].active = false;
    CHECK(g.actors > 1, "no residents");

    // Build the situation rather than hunt for it: a long wall with the actor
    // on one side and its target on the other, and a gap at one end.
    gg_actor *a = &g.actor[1];
    a->active = true;
    const int cx = g.map.start_x, cy = g.map.start_y;

    for (int y = cy - 6; y <= cy + 6; y++)
        for (int x = cx - 10; x <= cx + 10; x++) {
            gg_cell *c = gg_map_at(&g.map, x, y);
            if (!c) continue;
            c->terrain = GG_TILE_GRASS;
            c->prop = GG_NO_PROP;
            c->flags = 0;
        }
    // A wall down the middle, open only at its northern end.
    for (int y = cy - 3; y <= cy + 6; y++) {
        gg_cell *c = gg_map_at(&g.map, cx, y);
        if (c) { c->terrain = GG_TILE_MOUNTAIN; c->flags |= GG_CELL_BLOCKED; }
    }

    a->x = (int16_t)(cx - 4); a->y = (int16_t)(cy + 2); a->step = 0;
    const int tx = cx + 4, ty = cy + 2;
    CHECK(gg_map_walkable(&g.map, tx, ty), "the target is not walkable");

    a->schedn = 1;
    a->sched[0] = (gg_sched_entry){ .hour = 0, .x = (int16_t)tx, .y = (int16_t)ty };

    for (int t = 0; t < 200; t++) {
        gg_game_act(&g, GG_ACT_WAIT);
        if (a->x == tx && a->y == ty) break;
    }
    CHECK(a->x == tx && a->y == ty,
          "%s stopped at %d,%d instead of walking round the wall to %d,%d",
          a->name, a->x, a->y, tx, ty);
    gg_game_free(&g);
}

// ---------------------------------------------------------------------------
// Buildings
// ---------------------------------------------------------------------------
static void a_buildings_walls_block_but_its_room_does_not(void) {
    // The contract that makes the cutaway safe: a building's *walls* block and
    // its *room* does not, always, whatever is being drawn. Nothing about
    // collision may depend on where the player is standing, or walking out of
    // a house could put them inside a wall.
    gg_map m;
    CHECK(gg_map_alloc(&m, 24, 24), "alloc failed");
    for (int i = 0; i < 24 * 24; i++) m.cell[i].terrain = GG_TILE_GRASS;

    const gg_prop_id house = GG_PROP_HOUSE_BRICK_A;
    CHECK(gg_map_place_prop(&m, 12, 12, house), "placement failed on open grass");

    int x0, y0, x1, y1;
    gg_prop_footprint(house, 12, 12, &x0, &y0, &x1, &y1);
    const gg_prop_size *s = &GG_PROP_SIZE[house];
    const int door_x = x0 + s->door_dx;

    int rx0, ry0, rx1, ry1;
    CHECK(gg_prop_interior(house, 12, 12, &rx0, &ry0, &rx1, &ry1),
          "a house should have a room");

    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            const bool inside = x >= rx0 && x <= rx1 && y >= ry0 && y <= ry1;
            if (x == door_x && y == y1) {
                CHECK(gg_map_walkable(&m, x, y), "the doorway is blocked");
            } else if (inside) {
                CHECK(gg_map_walkable(&m, x, y),
                      "the room at %d,%d is blocked", x, y);
                CHECK(gg_map_at_const(&m, x, y)->flags & GG_CELL_INDOORS,
                      "the room at %d,%d is not flagged indoors", x, y);
            } else {
                CHECK(!gg_map_walkable(&m, x, y),
                      "the wall at %d,%d is walkable", x, y);
            }
        }
    gg_map_free(&m);
}

static void a_building_can_be_walked_into_and_out_of(void) {
    // End to end, through the simulation rather than around it: stand on the
    // street below the door, walk north twice, and be inside; walk back and be
    // out. This is the test that would have caught a doorway opening onto a
    // wall, which is what the exteriors-only version had.
    gg_map m;
    CHECK(gg_map_alloc(&m, 24, 24), "alloc failed");
    for (int i = 0; i < 24 * 24; i++) m.cell[i].terrain = GG_TILE_GRASS;

    const gg_prop_id house = GG_PROP_HOUSE_BRICK_A;
    CHECK(gg_map_place_prop(&m, 12, 16, house), "placement failed");

    int x0, y0, x1, y1;
    gg_prop_footprint(house, 12, 16, &x0, &y0, &x1, &y1);
    const int door_x = x0 + GG_PROP_SIZE[house].door_dx;

    CHECK(gg_map_walkable(&m, door_x, y1 + 1), "the street outside is blocked");
    CHECK(gg_map_walkable(&m, door_x, y1), "the doorway is blocked");
    CHECK(gg_map_walkable(&m, door_x, y1 - 1),
          "the doorway opens onto a wall - you cannot get in");
    CHECK(gg_prop_interior_contains(house, 12, 16, door_x, y1 - 1),
          "one step past the door should be inside the room");
    CHECK(!gg_prop_interior_contains(house, 12, 16, door_x, y1 + 1),
          "the street should not count as inside");
    gg_map_free(&m);
}

static void a_buildings_doorway_is_walkable(void) {
    gg_map m;
    CHECK(gg_map_alloc(&m, 24, 24), "alloc failed");
    for (int i = 0; i < 24 * 24; i++) m.cell[i].terrain = GG_TILE_GRASS;

    // Every house kind, because a door column outside the footprint would
    // silently put the doorway in a neighbour's wall.
    static const gg_prop_id HOUSES[] = {
        GG_PROP_HOUSE_BRICK_A, GG_PROP_HOUSE_BRICK_B, GG_PROP_HOUSE_PANELED,
    };
    for (size_t i = 0; i < GG_COUNTOF(HOUSES); i++) {
        gg_map n;
        CHECK(gg_map_alloc(&n, 24, 24), "alloc failed");
        for (int k = 0; k < 24 * 24; k++) n.cell[k].terrain = GG_TILE_GRASS;

        CHECK(gg_map_place_prop(&n, 12, 12, HOUSES[i]), "placement %zu failed", i);
        const gg_prop_size *s = &GG_PROP_SIZE[HOUSES[i]];
        CHECK(s->door_dx != GG_NO_DOOR, "house %zu has no door", i);

        int x0, y0, x1, y1;
        gg_prop_footprint(HOUSES[i], 12, 12, &x0, &y0, &x1, &y1);
        const int dx = x0 + s->door_dx;
        CHECK(gg_map_walkable(&n, dx, y1),
              "house %zu: the doorway at %d,%d is not walkable", i, dx, y1);
        CHECK(gg_map_at_const(&n, dx, y1)->flags & GG_CELL_DOOR,
              "house %zu: the doorway is not flagged as one", i);

        // Every door must open into the room, not along a side wall. The
        // panelled house's door was in a corner column: it looked right from
        // outside and put you in the wall when you stepped through, which only
        // showed up in a screenshot.
        CHECK(gg_map_walkable(&n, dx, y1 - 1),
              "house %zu: the doorway at %d,%d opens onto a wall", i, dx, y1);
        CHECK(gg_prop_interior_contains(HOUSES[i], 12, 12, dx, y1 - 1),
              "house %zu: one step past the door is not inside the room", i);
        gg_map_free(&n);
    }
    gg_map_free(&m);
}

static void you_can_walk_behind_a_building(void) {
    // The whole point of drawing buildings as props: the roof overhangs rows
    // the footprint does not cover, and those stay walkable. If the footprint
    // were the sprite, a town would be a wall of roofs you could not get past.
    gg_map m;
    CHECK(gg_map_alloc(&m, 24, 24), "alloc failed");
    for (int i = 0; i < 24 * 24; i++) m.cell[i].terrain = GG_TILE_GRASS;

    const gg_prop_id house = GG_PROP_HOUSE_BRICK_A;
    CHECK(gg_map_place_prop(&m, 12, 12, house), "placement failed");

    int x0, y0, x1, y1;
    gg_prop_footprint(house, 12, 12, &x0, &y0, &x1, &y1);
    const gg_prop_size *s = &GG_PROP_SIZE[house];

    CHECK(s->tiles_h > s->foot_h, "this house has no overhang to test");
    // The row just above the footprint is under the roof and must be open.
    for (int x = x0; x <= x1; x++)
        CHECK(gg_map_walkable(&m, x, y0 - 1),
              "the tile behind the house at %d,%d is blocked", x, y0 - 1);
    gg_map_free(&m);
}

static void a_building_that_does_not_fit_changes_nothing(void) {
    gg_map m;
    CHECK(gg_map_alloc(&m, 24, 24), "alloc failed");
    for (int i = 0; i < 24 * 24; i++) m.cell[i].terrain = GG_TILE_GRASS;

    // Off the map edge.
    CHECK(!gg_map_place_prop(&m, 0, 0, GG_PROP_HOUSE_BRICK_A),
          "a house was placed over the map edge");
    for (int i = 0; i < 24 * 24; i++)
        CHECK(m.cell[i].flags == 0 && m.cell[i].prop == GG_NO_PROP,
              "a refused placement left the map changed");

    // On water.
    for (int i = 0; i < 24 * 24; i++) {
        m.cell[i].terrain = GG_TILE_WATER;
        m.cell[i].flags = GG_CELL_WATER;
    }
    CHECK(!gg_map_place_prop(&m, 12, 12, GG_PROP_HOUSE_BRICK_A),
          "a house was built on water");

    // Onto another building.
    for (int i = 0; i < 24 * 24; i++) {
        m.cell[i].terrain = GG_TILE_GRASS;
        m.cell[i].flags = 0;
        m.cell[i].prop = GG_NO_PROP;
    }
    CHECK(gg_map_place_prop(&m, 12, 12, GG_PROP_HOUSE_BRICK_A), "first failed");
    CHECK(!gg_map_place_prop(&m, 13, 12, GG_PROP_HOUSE_BRICK_A),
          "two houses were placed on top of each other");
    gg_map_free(&m);
}

static void the_generated_town_has_buildings_with_reachable_doors(void) {
    // A door you cannot stand in front of is not a door. Sweep seeds: the town
    // is laid on a grid but the three houses have different footprints, so
    // placement refuses often enough that this is worth checking.
    for (uint32_t seed = 1; seed <= 20; seed++) {
        gg_map m;
        CHECK(gg_map_generate(&m, 160, 140, seed), "generate failed");

        int doors = 0, from_street = 0, into_room = 0;
        for (int y = 0; y < m.h; y++)
            for (int x = 0; x < m.w; x++) {
                if (!(gg_map_at_const(&m, x, y)->flags & GG_CELL_DOOR)) continue;
                doors++;
                // Below a door is the street it opens onto; above is the room.
                // The second is the one furniture could block, since it is
                // scattered into the room after the house is placed.
                if (gg_map_walkable(&m, x, y + 1)) from_street++;
                if (gg_map_walkable(&m, x, y - 1)) into_room++;
            }
        CHECK(doors > 0, "seed %u produced a town with no doors at all", seed);
        CHECK(from_street == doors,
              "seed %u: %d of %d doors cannot be reached from the street",
              seed, doors - from_street, doors);
        CHECK(into_room == doors,
              "seed %u: %d of %d doors are blocked from the inside - furniture "
              "landed in the doorway", seed, doors - into_room, doors);
        gg_map_free(&m);
    }
}

// ---------------------------------------------------------------------------
// Clock
// ---------------------------------------------------------------------------
static void the_clock_wraps_at_midnight_and_advances_the_day(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 11, "Tester"), "new game failed");

    const uint32_t day0 = g.day;
    g.minutes = GG_MINUTES_PER_DAY - 1;
    gg_game_act(&g, GG_ACT_WAIT);

    CHECK(g.minutes < GG_MINUTES_PER_DAY, "minutes did not wrap: %u", g.minutes);
    CHECK(g.day == day0 + 1, "day did not advance: %u -> %u", day0, g.day);
    gg_game_free(&g);
}

static void daylight_peaks_at_noon_and_bottoms_at_midnight(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 12, "Tester"), "new game failed");

    g.minutes = 12 * 60;
    const uint8_t noon = gg_game_daylight(&g);
    g.minutes = 0;
    const uint8_t midnight = gg_game_daylight(&g);
    g.minutes = 6 * 60;
    const uint8_t dawn = gg_game_daylight(&g);

    CHECK(noon == 255, "noon should be full light, got %u", noon);
    CHECK(midnight == 0, "midnight should be no light, got %u", midnight);
    CHECK(dawn > midnight && dawn < noon, "dawn %u should sit between", dawn);
    gg_game_free(&g);
}

// ---------------------------------------------------------------------------
// Turns
// ---------------------------------------------------------------------------
static void walking_into_a_wall_costs_no_turn(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 21, "Tester"), "new game failed");

    // Put a mountain where the player is about to walk, rather than hunting
    // the generated world for a wall. An earlier version searched the four
    // neighbours of the spawn and silently proved nothing on seeds that spawn
    // in open ground - a test that only sometimes tests its rule is worse than
    // no test, because the green tick is not evidence.
    gg_actor *p = gg_player(&g);
    gg_cell *east = gg_map_at(&g.map, p->x + 1, p->y);
    CHECK(east != nullptr, "the player is at the map edge; seed 21 has moved");
    if (!east) { gg_game_free(&g); return; }

    east->terrain = GG_TILE_MOUNTAIN;
    east->prop = GG_NO_PROP;
    east->flags = 0;
    CHECK(!gg_map_walkable(&g.map, p->x + 1, p->y), "the mountain did not take");

    // A refused move must cost nothing at all: if it advanced the clock, a
    // player could starve by walking into a rock.
    const uint32_t turn = g.turn, minutes = g.minutes;
    const int16_t px = p->x, py = p->y;
    gg_game_act(&g, GG_ACT_E);
    CHECK(g.turn == turn, "a blocked move advanced the turn counter");
    CHECK(g.minutes == minutes, "a blocked move advanced the clock");
    CHECK(p->x == px && p->y == py, "a blocked move moved the player");

    // It should still turn the player to face what they walked into, which is
    // what makes "walk at it, then press open" work.
    CHECK(p->facing == GG_FACE_RIGHT, "a blocked move should still turn the player");
    gg_game_free(&g);
}

static void a_legal_move_advances_the_world_by_one_turn(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 31, "Tester"), "new game failed");
    gg_actor *p = gg_player(&g);

    static const gg_action DIRS[4] = { GG_ACT_N, GG_ACT_S, GG_ACT_E, GG_ACT_W };
    static const int DX[4] = { 0, 0, 1, -1 }, DY[4] = { -1, 1, 0, 0 };

    for (int i = 0; i < 4; i++) {
        const int nx = p->x + DX[i], ny = p->y + DY[i];
        if (!gg_map_walkable(&g.map, nx, ny)) continue;
        if (gg_actor_occupied(g.actor, g.actors, nx, ny, g.player)) continue;

        const uint32_t turn = g.turn;
        gg_game_act(&g, DIRS[i]);
        CHECK(g.turn == turn + 1, "one move should be one turn");
        CHECK(p->x == nx && p->y == ny, "the player did not arrive");
        gg_game_free(&g);
        return;
    }
    CHECK(false, "the player is walled in at spawn - the generator is wrong");
    gg_game_free(&g);
}

static void the_player_never_shares_a_tile_with_a_townsperson(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 41, "Tester"), "new game failed");

    // Drive a few hundred turns of the whole world and check the invariant
    // throughout. Waiting rather than walking keeps the player still, so any
    // collision is the schedule code's doing.
    for (int t = 0; t < 400; t++) {
        gg_game_act(&g, GG_ACT_WAIT);
        for (int i = 0; i < g.actors; i++) {
            if (i == g.player || !g.actor[i].active) continue;
            CHECK(!(g.actor[i].x == gg_player(&g)->x &&
                    g.actor[i].y == gg_player(&g)->y),
                  "%s stood on the avatar at turn %d", g.actor[i].name, t);
        }
    }
    gg_game_free(&g);
}

static void townsfolk_never_walk_into_terrain(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 51, "Tester"), "new game failed");

    // A full game day, so every schedule entry comes into force at least once.
    for (int t = 0; t < GG_MINUTES_PER_DAY; t++) {
        gg_game_act(&g, GG_ACT_WAIT);
        for (int i = 0; i < g.actors; i++) {
            const gg_actor *a = &g.actor[i];
            if (!a->active || i == g.player) continue;
            if (!gg_map_walkable(&g.map, a->x, a->y)) {
                CHECK(false, "%s stands in terrain at %d,%d on turn %d",
                      a->name, a->x, a->y, t);
                break;      // one report is enough; do not flood the log
            }
        }
    }
    gg_game_free(&g);
}

static void two_townsfolk_never_share_a_tile(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 61, "Tester"), "new game failed");

    for (int t = 0; t < 500; t++) {
        gg_game_act(&g, GG_ACT_WAIT);
        for (int i = 0; i < g.actors; i++) {
            if (!g.actor[i].active) continue;
            for (int j = i + 1; j < g.actors; j++) {
                if (!g.actor[j].active) continue;
                if (g.actor[i].x == g.actor[j].x && g.actor[i].y == g.actor[j].y) {
                    CHECK(false, "%s and %s share %d,%d on turn %d",
                          g.actor[i].name, g.actor[j].name,
                          g.actor[i].x, g.actor[i].y, t);
                    goto done;
                }
            }
        }
    }
done:
    gg_game_free(&g);
}

// ---------------------------------------------------------------------------
// Schedules
// ---------------------------------------------------------------------------
static void a_schedule_before_its_first_entry_uses_the_last(void) {
    // The day wraps: an NPC who goes to bed at 22:00 is still in bed at 02:00,
    // which only works if the lookup falls back to the final entry.
    gg_actor a;
    SDL_zero(a);
    a.schedn = 3;
    a.sched[0] = (gg_sched_entry){ .hour = 8,  .x = 10, .y = 10 };
    a.sched[1] = (gg_sched_entry){ .hour = 14, .x = 20, .y = 20 };
    a.sched[2] = (gg_sched_entry){ .hour = 22, .x = 30, .y = 30 };

    int x, y;
    CHECK(gg_actor_target_at(&a, 3, &x, &y), "lookup failed");
    CHECK(x == 30 && y == 30, "02:00 should still be the 22:00 entry, got %d,%d", x, y);

    CHECK(gg_actor_target_at(&a, 9, &x, &y), "lookup failed");
    CHECK(x == 10 && y == 10, "09:00 should be the 08:00 entry, got %d,%d", x, y);

    CHECK(gg_actor_target_at(&a, 23, &x, &y), "lookup failed");
    CHECK(x == 30 && y == 30, "23:00 should be the 22:00 entry, got %d,%d", x, y);
}

static void an_actor_with_no_schedule_reports_none(void) {
    gg_actor a;
    SDL_zero(a);
    int x = -1, y = -1;
    CHECK(!gg_actor_target_at(&a, 12, &x, &y), "an empty schedule must report none");
}

// ---------------------------------------------------------------------------
// Facing and interpolation
// ---------------------------------------------------------------------------
static void facing_follows_the_dominant_axis(void) {
    CHECK(gg_facing_from_delta(1, 0)  == GG_FACE_RIGHT, "east");
    CHECK(gg_facing_from_delta(-1, 0) == GG_FACE_LEFT,  "west");
    CHECK(gg_facing_from_delta(0, -1) == GG_FACE_UP,    "north");
    CHECK(gg_facing_from_delta(0, 1)  == GG_FACE_DOWN,  "south");
    // A tie resolves vertical, which is what the LPC walk sheets read best as.
    CHECK(gg_facing_from_delta(1, 1)  == GG_FACE_DOWN,  "south-east ties to down");
    CHECK(gg_facing_from_delta(-1, -1) == GG_FACE_UP,   "north-west ties to up");
}

static void a_finished_step_lands_exactly_on_the_tile(void) {
    gg_actor a;
    SDL_zero(a);
    a.x = 5; a.y = 7;
    gg_actor_move_to(&a, 6, 7);

    int x, y;
    gg_actor_draw_pos(&a, &x, &y);
    CHECK(x == 5 * GG_TILE, "a fresh step should start on the old tile, got %d", x);

    // Run the slide out; the actor must land exactly, with no drift.
    for (int i = 0; i < GG_STEP_TICKS; i++) gg_actor_animate(&a);
    CHECK(a.step == 0, "the step did not finish");
    gg_actor_draw_pos(&a, &x, &y);
    CHECK(x == 6 * GG_TILE && y == 7 * GG_TILE,
          "landed at %d,%d instead of %d,%d", x, y, 6 * GG_TILE, 7 * GG_TILE);
}

// ---------------------------------------------------------------------------
// Light
// ---------------------------------------------------------------------------
static void a_room_is_lit_at_midnight_and_the_street_is_not(void) {
    // The whole point of the lighting item. Before it, GG_CELL_INDOORS was set
    // on every room and read by nothing, so a house at night was exactly as
    // dark as the road outside and going in after dusk gained you nothing.
    gg_game g;
    CHECK(gg_game_new(&g, 7, "Tester"), "new game failed");
    g.minutes = 0;                                  // midnight
    const uint8_t day = gg_game_daylight(&g);
    CHECK(day == 0, "midnight should be pitch dark outdoors, got %u", day);

    // Find a room, and a patch of street well away from the avatar's own light.
    int rx = -1, ry = -1, sx = -1, sy = -1;
    const gg_actor *p = gg_player_const(&g);
    for (int y = 0; y < g.map.h && (rx < 0 || sx < 0); y++)
        for (int x = 0; x < g.map.w && (rx < 0 || sx < 0); x++) {
            const gg_cell *c = gg_map_at_const(&g.map, x, y);
            if (gg_dist_cheb(p->x, p->y, x, y) <= GG_LIGHT_CARRY_RADIUS) continue;
            if (rx < 0 && (c->flags & GG_CELL_INDOORS)) { rx = x; ry = y; }
            if (sx < 0 && c->terrain == GG_TILE_ROAD)   { sx = x; sy = y; }
        }
    CHECK(rx >= 0, "the town has no interior to test");
    CHECK(sx >= 0, "the map has no road to test");

    if (rx >= 0 && sx >= 0) {
        const uint8_t room = gg_light_at(&g, rx, ry, day);
        const uint8_t street = gg_light_at(&g, sx, sy, day);
        CHECK(room > street, "a room at midnight (%u) is no brighter than the "
              "street (%u)", room, street);
        CHECK(street == 0, "the street at midnight should be unlit, got %u", street);
    }
    gg_game_free(&g);
}

static void the_avatar_carries_a_light_that_falls_off(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 7, "Tester"), "new game failed");
    g.minutes = 0;
    const uint8_t day = gg_game_daylight(&g);
    const gg_actor *p = gg_player_const(&g);

    const uint8_t here = gg_light_at(&g, p->x, p->y, day);
    CHECK(here == GG_LIGHT_FULL, "the avatar's own tile should be fully lit, got %u",
          here);

    // Strictly decreasing out to the radius, then nothing.
    uint8_t prev = here;
    for (int d = 1; d <= GG_LIGHT_CARRY_RADIUS; d++) {
        const uint8_t at = gg_light_at(&g, p->x + d, p->y, day);
        CHECK(at < prev, "light at %d tiles (%u) did not fall below %d tiles (%u)",
              d, at, d - 1, prev);
        prev = at;
    }
    CHECK(gg_light_at(&g, p->x + GG_LIGHT_CARRY_RADIUS + 3, p->y, day) == 0,
          "the carried light reaches further than its radius");
    gg_game_free(&g);
}

static void noon_lights_the_whole_outdoors(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 7, "Tester"), "new game failed");
    g.minutes = 12 * 60;
    const uint8_t day = gg_game_daylight(&g);
    CHECK(day == GG_LIGHT_FULL, "noon should be full daylight, got %u", day);

    // Anywhere outdoors, however far from the avatar.
    const gg_actor *p = gg_player_const(&g);
    CHECK(gg_light_at(&g, p->x + 40, p->y + 30, day) == GG_LIGHT_FULL,
          "an outdoor tile at noon is not fully lit");
    gg_game_free(&g);
}

// ---------------------------------------------------------------------------
// Camera
//
// gg_render_camera is pure arithmetic over the game state - no texture, no
// renderer - so it is testable here, and worth testing: a tile-quantised
// camera is invisible in a screenshot and glaring in motion.
// ---------------------------------------------------------------------------
static void the_camera_moves_in_sub_tile_steps_while_walking(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 71, "Tester"), "new game failed");

    // Put the player somewhere with room on every side, so the camera is
    // following rather than sitting against a clamp.
    gg_actor *p = gg_player(&g);
    p->x = (int16_t)(g.map.w / 2);
    p->y = (int16_t)(g.map.h / 2);
    p->step = 0;

    int x0, y0;
    gg_render_camera(&g, &x0, &y0);

    // Start a step east and walk the animation out one tick at a time. The
    // camera must creep, never jump a whole tile: the bug this pins had it
    // stationary for the whole slide and then moving 32 px at once.
    gg_actor_move_to(p, p->x + 1, p->y);

    int prev = x0, biggest = 0, moved_ticks = 0;
    for (int i = 0; i < GG_STEP_TICKS; i++) {
        gg_game_animate(&g);
        int cx, cy;
        gg_render_camera(&g, &cx, &cy);
        const int d = cx - prev;
        CHECK(d >= 0, "the camera moved backwards during an eastward step");
        if (d > biggest) biggest = d;
        if (d > 0) moved_ticks++;
        CHECK(cy == y0, "the camera drifted vertically during a horizontal step");
        prev = cx;
    }

    CHECK(prev - x0 == GG_TILE, "a one-tile step should move the camera one tile, got %d",
          prev - x0);
    CHECK(biggest < GG_TILE, "the camera jumped %d px in one tick - it is still "
          "quantised to whole tiles", biggest);
    CHECK(moved_ticks >= GG_STEP_TICKS / 2,
          "the camera only moved on %d of %d ticks", moved_ticks, GG_STEP_TICKS);
    gg_game_free(&g);
}

static void the_camera_clamps_to_the_map_edges(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 72, "Tester"), "new game failed");
    gg_actor *p = gg_player(&g);

    p->x = 0; p->y = 0; p->step = 0;
    int cx, cy;
    gg_render_camera(&g, &cx, &cy);
    CHECK(cx == 0 && cy == 0, "top-left should clamp to 0,0, got %d,%d", cx, cy);

    p->x = (int16_t)(g.map.w - 1);
    p->y = (int16_t)(g.map.h - 1);
    gg_render_camera(&g, &cx, &cy);
    CHECK(cx == g.map.w * GG_TILE - GG_VIEW_W, "bottom-right x clamp, got %d", cx);
    CHECK(cy == g.map.h * GG_TILE - GG_VIEW_H, "bottom-right y clamp, got %d", cy);
    gg_game_free(&g);
}

// ---------------------------------------------------------------------------
// Shorelines
// ---------------------------------------------------------------------------
// A 5x5 map of grass with a 3x3 lake in the middle. Every one of the nine
// pieces should be selected exactly once, which is the whole autotile contract
// in one picture.
static void a_square_lake_selects_all_nine_edge_pieces(void) {
    gg_map m;
    CHECK(gg_map_alloc(&m, 5, 5), "alloc failed");
    for (int i = 0; i < 25; i++) m.cell[i].terrain = GG_TILE_GRASS;
    for (int y = 1; y <= 3; y++)
        for (int x = 1; x <= 3; x++)
            m.cell[y * 5 + x].terrain = GG_TILE_WATER;

    static const int WANT[3][3] = {
        { GG_EDGE_NW, GG_EDGE_N, GG_EDGE_NE },
        { GG_EDGE_W,  GG_EDGE_C, GG_EDGE_E  },
        { GG_EDGE_SW, GG_EDGE_S, GG_EDGE_SE },
    };
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            const int got = gg_render_water_piece(&m, 1 + i, 1 + j);
            CHECK(got == WANT[j][i], "at %d,%d wanted piece %d, got %d",
                  1 + i, 1 + j, WANT[j][i], got);
        }
    gg_map_free(&m);
}

static void a_one_tile_island_selects_all_four_concave_corners(void) {
    // The hardest case, and the one the LPC sheets have no art for: a single
    // tile of land in open water. Each of the four diagonally adjacent cells
    // has land on exactly one diagonal and no orthogonal neighbour, so each
    // must resolve to the concave corner facing the island - the cell above
    // and left of the island sees land to its south-east.
    gg_map m;
    CHECK(gg_map_alloc(&m, 7, 7), "alloc failed");
    for (int i = 0; i < 49; i++) m.cell[i].terrain = GG_TILE_WATER;
    m.cell[3 * 7 + 3].terrain = GG_TILE_GRASS;

    CHECK(gg_render_water_piece(&m, 2, 2) == GG_EDGE_IN_SE, "up-left of the island");
    CHECK(gg_render_water_piece(&m, 4, 2) == GG_EDGE_IN_SW, "up-right of the island");
    CHECK(gg_render_water_piece(&m, 2, 4) == GG_EDGE_IN_NE, "down-left of the island");
    CHECK(gg_render_water_piece(&m, 4, 4) == GG_EDGE_IN_NW, "down-right of the island");

    // The orthogonal neighbours still take the straight edges, and a cell two
    // away from everything is still plain interior.
    CHECK(gg_render_water_piece(&m, 3, 2) == GG_EDGE_S, "directly above the island");
    CHECK(gg_render_water_piece(&m, 3, 4) == GG_EDGE_N, "directly below the island");
    CHECK(gg_render_water_piece(&m, 1, 1) == GG_EDGE_C, "far from the island");
    gg_map_free(&m);
}

static void an_orthogonal_edge_beats_a_concave_corner(void) {
    // A cell with land to the north *and* land on its south-east diagonal has
    // to take the north edge: the straight boundary is the dominant feature,
    // and drawing the corner instead would leave the north shore unbanked.
    gg_map m;
    CHECK(gg_map_alloc(&m, 7, 7), "alloc failed");
    for (int i = 0; i < 49; i++) m.cell[i].terrain = GG_TILE_WATER;
    m.cell[2 * 7 + 3].terrain = GG_TILE_GRASS;   // north of (3,3)
    m.cell[4 * 7 + 4].terrain = GG_TILE_GRASS;   // south-east of (3,3)

    CHECK(gg_render_water_piece(&m, 3, 3) == GG_EDGE_N,
          "an orthogonal edge must win over a diagonal");
    gg_map_free(&m);
}

static void water_at_the_map_edge_draws_no_shoreline_against_nothing(void) {
    // Off-map counts as water, so a lake running off the edge keeps its
    // interior fill there rather than drawing a beach against the void.
    gg_map m;
    CHECK(gg_map_alloc(&m, 4, 4), "alloc failed");
    for (int i = 0; i < 16; i++) m.cell[i].terrain = GG_TILE_WATER;

    CHECK(gg_render_water_piece(&m, 0, 0) == GG_EDGE_C,
          "the top-left corner of an all-water map should be interior");
    CHECK(gg_render_water_piece(&m, 3, 3) == GG_EDGE_C,
          "the bottom-right corner of an all-water map should be interior");
    gg_map_free(&m);
}

static void deep_water_is_never_adjacent_to_land(void) {
    // The deep set has no land boundary art at all - it is drawn as deep water
    // inside shallow. If the generator ever puts deep water against grass, the
    // drop-off renders as a hard square edge.
    for (uint32_t seed = 1; seed <= 25; seed++) {
        gg_map m;
        CHECK(gg_map_generate(&m, 160, 128, seed), "generate failed");
        bool bad = false;
        for (int y = 0; y < m.h && !bad; y++)
            for (int x = 0; x < m.w && !bad; x++) {
                if (gg_map_at_const(&m, x, y)->terrain != GG_TILE_WATER_DEEP)
                    continue;
                static const int DX[4] = { 0, 0, -1, 1 };
                static const int DY[4] = { -1, 1, 0, 0 };
                for (int k = 0; k < 4; k++) {
                    const gg_cell *n = gg_map_at_const(&m, x + DX[k], y + DY[k]);
                    if (n && !(n->flags & GG_CELL_WATER)) {
                        CHECK(false, "seed %u: deep water at %d,%d touches %s",
                              seed, x, y, GG_TERRAIN[n->terrain].name);
                        bad = true;
                        break;
                    }
                }
            }
        gg_map_free(&m);
    }
}

// ---------------------------------------------------------------------------
// Land meeting land
// ---------------------------------------------------------------------------
static gg_map *land_scene(gg_map *m) {
    // 7x7 of grass with a dirt cross carved through the middle, so the centre
    // cell has dirt on every side and grass only on the diagonals.
    SDL_zerop(m);
    if (!gg_map_alloc(m, 7, 7)) return nullptr;
    for (int i = 0; i < 49; i++) m->cell[i].terrain = GG_TILE_GRASS;
    return m;
}

static void a_one_tile_road_takes_a_verge_on_both_sides(void) {
    // The case a single-piece selector cannot express, and the reason the
    // overlay returns a mask: with one piece the road was grassy down its west
    // side and hard-edged down its east.
    gg_map m;
    CHECK(land_scene(&m) != nullptr, "alloc failed");
    for (int y = 0; y < 7; y++) m.cell[y * 7 + 3].terrain = GG_TILE_ROAD;

    const uint16_t mask = gg_render_overlay_mask(&m, 3, 3, GG_OVERLAY_GRASS);
    CHECK(mask & (1u << GG_EDGE_W), "no verge on the west side");
    CHECK(mask & (1u << GG_EDGE_E), "no verge on the east side");
    CHECK(!(mask & (1u << GG_EDGE_N)), "north is road, not grass");
    CHECK(!(mask & (1u << GG_EDGE_S)), "south is road, not grass");
    gg_map_free(&m);
}

static void a_lone_patch_takes_a_verge_on_all_four_sides(void) {
    gg_map m;
    CHECK(land_scene(&m) != nullptr, "alloc failed");
    m.cell[3 * 7 + 3].terrain = GG_TILE_SAND;

    const uint16_t mask = gg_render_overlay_mask(&m, 3, 3, GG_OVERLAY_GRASS);
    for (int p = 0; p < 4; p++) {
        static const int SIDES[4] = { GG_EDGE_N, GG_EDGE_S, GG_EDGE_E, GG_EDGE_W };
        CHECK(mask & (1u << SIDES[p]), "side %d has no verge", p);
    }
    // Straight pieces already cover the corners; drawing the concave ones too
    // would double the alpha there and leave a visible seam.
    CHECK(!(mask & (1u << GG_EDGE_IN_NW)), "concave corner drawn needlessly");
    CHECK(!(mask & (1u << GG_EDGE_IN_SE)), "concave corner drawn needlessly");
    gg_map_free(&m);
}

static void a_diagonal_only_neighbour_uses_the_concave_piece(void) {
    gg_map m;
    CHECK(land_scene(&m) != nullptr, "alloc failed");
    // Dirt everywhere except one grass cell on the centre's north-west diagonal.
    for (int i = 0; i < 49; i++) m.cell[i].terrain = GG_TILE_DIRT;
    m.cell[2 * 7 + 2].terrain = GG_TILE_GRASS;

    const uint16_t mask = gg_render_overlay_mask(&m, 3, 3, GG_OVERLAY_GRASS);
    CHECK(mask == (1u << GG_EDGE_IN_NW),
          "expected only the concave NW piece, got mask 0x%x", mask);
    gg_map_free(&m);
}

static void a_straight_verge_suppresses_its_own_corner(void) {
    gg_map m;
    CHECK(land_scene(&m) != nullptr, "alloc failed");
    for (int i = 0; i < 49; i++) m.cell[i].terrain = GG_TILE_DIRT;
    m.cell[2 * 7 + 2].terrain = GG_TILE_GRASS;   // north-west diagonal
    m.cell[2 * 7 + 3].terrain = GG_TILE_GRASS;   // due north

    const uint16_t mask = gg_render_overlay_mask(&m, 3, 3, GG_OVERLAY_GRASS);
    CHECK(mask & (1u << GG_EDGE_N), "the north verge is missing");
    CHECK(!(mask & (1u << GG_EDGE_IN_NW)),
          "the north piece already covers that corner; drawing both doubles it");
    gg_map_free(&m);
}

static void a_patch_touching_no_grass_needs_no_overlay(void) {
    gg_map m;
    CHECK(land_scene(&m) != nullptr, "alloc failed");
    for (int i = 0; i < 49; i++) m.cell[i].terrain = GG_TILE_DIRT;
    CHECK(gg_render_overlay_mask(&m, 3, 3, GG_OVERLAY_GRASS) == 0,
          "a cell surrounded by its own kind should draw nothing");
    gg_map_free(&m);
}

static void a_boundary_is_drawn_from_the_softer_side_only(void) {
    // Rank decides which way round a transition goes, and it must go one way
    // only: if both sides drew onto each other the boundary would be doubled,
    // and each cell would be fringed with the other's colour.
    gg_map m;
    CHECK(land_scene(&m) != nullptr, "alloc failed");
    for (int i = 0; i < 49; i++) m.cell[i].terrain = GG_TILE_DESERT;
    for (int x = 0; x < 7; x++) m.cell[2 * 7 + x].terrain = GG_TILE_SAND;

    // Sand outranks desert, so the desert cell below the sand takes a verge...
    const uint16_t onto_desert =
        gg_render_overlay_mask(&m, 3, 3, GG_OVERLAY_SAND);
    CHECK(onto_desert & (1u << GG_EDGE_N), "sand should bleed onto desert");

    // ...and the sand cell above it takes nothing from the desert.
    const uint16_t onto_sand =
        gg_render_overlay_mask(&m, 3, 2, GG_OVERLAY_DESERT);
    CHECK(onto_sand == 0, "desert must not bleed back onto sand, got 0x%x",
          onto_sand);
    gg_map_free(&m);
}

static void equal_ranks_do_not_transition(void) {
    // Grass and worn grass are the same ground wearing different amounts of
    // traffic; fringing one with the other would be visible and pointless.
    gg_map m;
    CHECK(land_scene(&m) != nullptr, "alloc failed");
    for (int x = 0; x < 7; x++) m.cell[2 * 7 + x].terrain = GG_TILE_GRASS_WORN;

    CHECK(gg_render_overlay_mask(&m, 3, 3, GG_OVERLAY_GRASS) == 0,
          "grass and worn grass should not fringe each other");
    gg_map_free(&m);
}

static void water_and_masonry_take_no_overlay(void) {
    // Water carries its bank in its own edge sets, and GG_TILE_CLIFF stands in
    // for masonry - a grass fringe up the side of every building is worse than
    // a hard edge on the few loose cliffs.
    gg_map m;
    CHECK(land_scene(&m) != nullptr, "alloc failed");
    m.cell[3 * 7 + 3].terrain = GG_TILE_WATER;
    CHECK(gg_render_overlay_mask(&m, 3, 3, GG_OVERLAY_GRASS) == 0,
          "water should take no land overlay");

    m.cell[3 * 7 + 3].terrain = GG_TILE_CLIFF;
    CHECK(gg_render_overlay_mask(&m, 3, 3, GG_OVERLAY_GRASS) == 0,
          "masonry should take no land overlay");
    gg_map_free(&m);
}

static void the_map_edge_grows_no_verge(void) {
    // Off-map is not grass. Treating it as grass would fringe the whole border.
    gg_map m;
    CHECK(land_scene(&m) != nullptr, "alloc failed");
    for (int i = 0; i < 49; i++) m.cell[i].terrain = GG_TILE_DIRT;
    CHECK(gg_render_overlay_mask(&m, 0, 0, GG_OVERLAY_GRASS) == 0, "the top-left corner sprouted a verge");
    CHECK(gg_render_overlay_mask(&m, 6, 6, GG_OVERLAY_GRASS) == 0, "the bottom-right corner sprouted a verge");
    gg_map_free(&m);
}

static void the_coastline_has_no_isolated_puddles(void) {
    // Smoothing exists to remove the single-tile spurs and notches the ellipse
    // jitter leaves, because each one needs a concave corner piece the LPC
    // sheets do not carry and so renders as a square step.
    for (uint32_t seed = 1; seed <= 25; seed++) {
        gg_map m;
        CHECK(gg_map_generate(&m, 160, 128, seed), "generate failed");
        for (int y = 0; y < m.h; y++)
            for (int x = 0; x < m.w; x++) {
                if (!(gg_map_at_const(&m, x, y)->flags & GG_CELL_WATER)) continue;
                int wet = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        if (!dx && !dy) continue;
                        const gg_cell *n = gg_map_at_const(&m, x + dx, y + dy);
                        if (n && (n->flags & GG_CELL_WATER)) wet++;
                    }
                CHECK(wet >= 2, "seed %u: water at %d,%d has only %d wet "
                      "neighbours - that is a puddle, not a lake",
                      seed, x, y, wet);
            }
        gg_map_free(&m);
    }
}

// ---------------------------------------------------------------------------
// Content tables
// ---------------------------------------------------------------------------
static void every_terrain_and_item_has_a_name(void) {
    // A missing row shows up as a null name, and a null name reaches the HUD
    // as a crash rather than a blank.
    for (int i = 0; i < GG_TILE_COUNT; i++)
        CHECK(GG_TERRAIN[i].name != nullptr, "terrain %d has no name", i);
    for (int i = 0; i < GG_ITEM_COUNT; i++)
        CHECK(GG_ITEM_NAME[i] != nullptr, "item %d has no name", i);
}

static void every_prop_has_a_plausible_footprint(void) {
    for (int i = 0; i < GG_PROP_COUNT; i++) {
        const gg_prop_size *s = &GG_PROP_SIZE[i];
        CHECK(s->tiles_w >= 1 && s->tiles_h >= 1, "prop %d is zero-sized", i);
        CHECK(s->foot_h >= 1 && s->foot_h <= s->tiles_h,
              "prop %d has a footprint of %u in a %u-tall sprite",
              i, s->foot_h, s->tiles_h);
    }
}

// ---------------------------------------------------------------------------
int main(void) {
    // The simulation must not need video; prove it by never initialising it.
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");

    RUN(the_rng_is_reproducible_from_its_seed);
    RUN(a_zero_seed_does_not_stick_at_zero);
    RUN(rand_below_stays_in_range);

    RUN(generation_is_reproducible_from_its_seed);
    RUN(different_seeds_produce_different_worlds);
    RUN(off_map_tiles_are_never_walkable);
    RUN(water_and_mountain_are_impassable);
    RUN(a_prop_blocks_its_tile_but_ground_cover_does_not);
    RUN(the_generated_start_tile_is_always_walkable);
    RUN(a_saved_map_reloads_byte_for_byte);
    RUN(loading_a_file_that_is_not_a_map_fails_cleanly);

    RUN(a_path_goes_around_a_wall);
    RUN(a_path_solves_a_serpentine_maze);
    RUN(a_path_never_cuts_a_diagonal_corner);
    RUN(an_unreachable_target_still_moves_toward_it);
    RUN(a_completely_boxed_in_actor_reports_no_step);
    RUN(the_search_is_reproducible);
    RUN(a_resident_crosses_the_town_to_a_fixed_target);
    RUN(a_resident_walks_round_a_building_rather_than_into_it);

    RUN(a_buildings_walls_block_but_its_room_does_not);
    RUN(a_building_can_be_walked_into_and_out_of);
    RUN(a_buildings_doorway_is_walkable);
    RUN(you_can_walk_behind_a_building);
    RUN(a_building_that_does_not_fit_changes_nothing);
    RUN(the_generated_town_has_buildings_with_reachable_doors);

    RUN(the_clock_wraps_at_midnight_and_advances_the_day);
    RUN(daylight_peaks_at_noon_and_bottoms_at_midnight);

    RUN(walking_into_a_wall_costs_no_turn);
    RUN(a_legal_move_advances_the_world_by_one_turn);
    RUN(the_player_never_shares_a_tile_with_a_townsperson);
    RUN(townsfolk_never_walk_into_terrain);
    RUN(two_townsfolk_never_share_a_tile);

    RUN(a_schedule_before_its_first_entry_uses_the_last);
    RUN(an_actor_with_no_schedule_reports_none);

    RUN(facing_follows_the_dominant_axis);
    RUN(a_finished_step_lands_exactly_on_the_tile);

    RUN(a_room_is_lit_at_midnight_and_the_street_is_not);
    RUN(the_avatar_carries_a_light_that_falls_off);
    RUN(noon_lights_the_whole_outdoors);

    RUN(the_camera_moves_in_sub_tile_steps_while_walking);
    RUN(the_camera_clamps_to_the_map_edges);

    RUN(a_square_lake_selects_all_nine_edge_pieces);
    RUN(a_one_tile_island_selects_all_four_concave_corners);
    RUN(an_orthogonal_edge_beats_a_concave_corner);
    RUN(water_at_the_map_edge_draws_no_shoreline_against_nothing);
    RUN(deep_water_is_never_adjacent_to_land);
    RUN(the_coastline_has_no_isolated_puddles);

    RUN(a_one_tile_road_takes_a_verge_on_both_sides);
    RUN(a_lone_patch_takes_a_verge_on_all_four_sides);
    RUN(a_diagonal_only_neighbour_uses_the_concave_piece);
    RUN(a_straight_verge_suppresses_its_own_corner);
    RUN(a_patch_touching_no_grass_needs_no_overlay);
    RUN(a_boundary_is_drawn_from_the_softer_side_only);
    RUN(equal_ranks_do_not_transition);
    RUN(water_and_masonry_take_no_overlay);
    RUN(the_map_edge_grows_no_verge);

    RUN(every_terrain_and_item_has_a_name);
    RUN(every_prop_has_a_plausible_footprint);

    SDL_Log("%s", "");
    SDL_Log("%d checks, %d failures", g_checks, g_failures);
    SDL_Quit();
    return g_failures ? 1 : 0;
}
