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

    RUN(the_camera_moves_in_sub_tile_steps_while_walking);
    RUN(the_camera_clamps_to_the_map_edges);

    RUN(every_terrain_and_item_has_a_name);
    RUN(every_prop_has_a_plausible_footprint);

    SDL_Log("%s", "");
    SDL_Log("%d checks, %d failures", g_checks, g_failures);
    SDL_Quit();
    return g_failures ? 1 : 0;
}
