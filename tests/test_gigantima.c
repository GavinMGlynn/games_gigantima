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
#include "core/gg_save.h"
#include "core/gg_dialogue.h"
#include "core/gg_combat.h"
#include "core/gg_magic.h"
#include "core/gg_bestiary.h"
#include "ui/gg_menu.h"
#include "ui/gg_screens.h"
#include "platform/gg_settings.h"
#include "platform/gg_input.h"

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

    // Things lying on the ground are part of the map, so they are part of what
    // "byte for byte" has to mean. Deliberately built up first if the generator
    // happened to leave none: a round-trip test of an empty list proves nothing.
    if (a.grounds == 0) {
        CHECK(gg_ground_drop(&a, 1, 1, GG_ITEM_GOLD, 7), "could not seed a pile");
        CHECK(gg_map_save(&a, path), "second save failed");
        gg_map_free(&b);
        CHECK(gg_map_load(&b, path), "second load failed");
    }
    CHECK(a.grounds > 0, "nothing on the ground, so this proves nothing");
    CHECK(a.grounds == b.grounds, "%d things on the ground became %d",
          a.grounds, b.grounds);
    CHECK(SDL_memcmp(a.ground, b.ground,
                     (size_t)a.grounds * sizeof *a.ground) == 0,
          "what was lying about changed across the round trip");

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

// Everything writes under a disposable directory in the pref path, so a test
// run never touches a real player's games.
static const char *save_base(void) { return gg_pref_file("testsaves/"); }

static void wipe_saves(const char *name) {
    gg_profile_delete(save_base(), name);
}

// ---------------------------------------------------------------------------
// Menus and screens
//
// All the logic here is pure - a menu knows where its cursor is, a screen says
// what choosing a row means - so none of it needs a renderer, and a failure
// points at the rule rather than at the pixels.
// ---------------------------------------------------------------------------
static void the_menu_cursor_skips_disabled_rows_and_wraps(void) {
    gg_menu m;
    gg_menu_reset(&m, "Test");
    gg_menu_add(&m, true,  "one", nullptr);
    gg_menu_add(&m, false, "two", nullptr);
    gg_menu_add(&m, false, "three", nullptr);
    gg_menu_add(&m, true,  "four", nullptr);

    gg_menu_select(&m, 0);
    CHECK(m.cursor == 0, "select(0) landed on %d", m.cursor);

    gg_menu_move(&m, 1);
    CHECK(m.cursor == 3, "down from 0 should skip two disabled rows, got %d", m.cursor);

    // Wrapping matters: on a short menu it is the difference between one
    // keypress to reach the last row and four.
    gg_menu_move(&m, 1);
    CHECK(m.cursor == 0, "down from the last row should wrap, got %d", m.cursor);
    gg_menu_move(&m, -1);
    CHECK(m.cursor == 3, "up from the first row should wrap, got %d", m.cursor);

    // Landing on a disabled row by index must not stick there.
    gg_menu_select(&m, 1);
    CHECK(m.item[m.cursor].enabled, "the cursor settled on a disabled row");
}

static void a_menu_with_nothing_choosable_chooses_nothing(void) {
    gg_menu m;
    gg_menu_reset(&m, "Test");
    gg_menu_add(&m, false, "one", nullptr);
    gg_menu_add(&m, false, "two", nullptr);

    gg_menu_move(&m, 1);          // must terminate rather than spin
    CHECK(gg_menu_chosen(&m) == -1, "a menu of disabled rows returned a choice");

    gg_menu empty;
    gg_menu_reset(&empty, nullptr);
    gg_menu_move(&empty, 1);
    CHECK(gg_menu_chosen(&empty) == -1, "an empty menu returned a choice");
}

static void settings_round_trip_and_clamp(void) {
    const char *path = gg_pref_file("test_settings.txt");
    SDL_RemovePath(path);

    gg_settings a;
    gg_settings_defaults(&a);
    a.scale = 3;
    a.fullscreen = true;
    a.rumble = false;
    a.music = 2;
    SDL_strlcpy(a.last_profile, "Gavin", sizeof a.last_profile);
    CHECK(gg_settings_save(&a, path), "save failed");

    gg_settings b;
    CHECK(gg_settings_load(&b, path), "load failed");
    CHECK(b.scale == a.scale, "scale changed: %d", b.scale);
    CHECK(b.fullscreen == a.fullscreen, "fullscreen changed");
    CHECK(b.rumble == a.rumble, "rumble changed");
    CHECK(b.music == a.music, "music changed");
    CHECK(SDL_strcmp(b.last_profile, "Gavin") == 0,
          "last profile changed to '%s'", b.last_profile);

    // A hand-edited file with nonsense in it must not produce a nonsense
    // window. Settings are a text file precisely so people can edit them.
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    CHECK(io != nullptr, "could not write the hand-edited file");
    if (io) {
        const char *text =
            "# a comment\n"
            "scale = 99\n"
            "music = -4\n"
            "fullscreen = yes\n"
            "something_from_the_future = 12\n"
            "rumble=TRUE\n";
        SDL_WriteIO(io, text, SDL_strlen(text));
        SDL_CloseIO(io);
    }
    gg_settings c;
    CHECK(gg_settings_load(&c, path), "load of the edited file failed");
    CHECK(c.scale >= 1 && c.scale <= 4, "scale 99 was not clamped: %d", c.scale);
    CHECK(c.music >= 0 && c.music <= 10, "music -4 was not clamped: %d", c.music);
    CHECK(c.fullscreen, "fullscreen = yes was not read");
    CHECK(c.rumble, "rumble=TRUE was not read");

    // And a missing file is a first run, not a failure.
    SDL_RemovePath(path);
    gg_settings d;
    CHECK(!gg_settings_load(&d, path), "a missing file reported success");
    gg_settings def;
    gg_settings_defaults(&def);
    CHECK(d.scale == def.scale && d.rumble == def.rumble,
          "a missing file did not leave the defaults");
}

static void the_title_screen_offers_continue_only_when_there_is_one(void) {
    const char *base = save_base();
    wipe_saves("Screens");

    gg_settings set;
    gg_settings_defaults(&set);
    gg_game g;
    CHECK(gg_game_new(&g, 6, "Screens"), "new game failed");

    gg_screens s;
    SDL_zero(s);
    gg_screens_enter(&s, GG_SCREEN_TITLE, base, &set, &g, true);
    CHECK(!s.menu.item[0].enabled,
          "Continue was offered with nothing saved");
    CHECK(s.menu.cursor != 0, "the cursor sat on a row that cannot be chosen");

    CHECK(gg_save_write(&g, base, "Screens"), "save failed");
    gg_screens_enter(&s, GG_SCREEN_TITLE, base, &set, &g, true);
    CHECK(s.menu.item[0].enabled, "Continue was not offered after saving");
    CHECK(s.menu.cursor == 0,
          "Continue should be pre-selected: it is what a returning player wants");

    const gg_screen_result r = gg_screens_choose(&s, base, &set, true);
    CHECK(r.action == GG_ACTION_CONTINUE, "choosing Continue did not continue");
    CHECK(SDL_strcmp(r.name, "Screens") == 0,
          "Continue named '%s' rather than the saved profile", r.name);

    gg_game_free(&g);
    wipe_saves("Screens");
}

static void naming_a_journey_refuses_a_bad_or_taken_name(void) {
    const char *base = save_base();
    wipe_saves("Taken");

    gg_settings set;
    gg_settings_defaults(&set);
    gg_game g;
    CHECK(gg_game_new(&g, 6, "Taken"), "new game failed");
    CHECK(gg_save_write(&g, base, "Taken"), "save failed");

    gg_screens s;
    SDL_zero(s);
    gg_screens_enter(&s, GG_SCREEN_NAME, base, &set, &g, true);

    // Empty.
    gg_screen_result r = gg_screens_choose(&s, base, &set, true);
    CHECK(r.action == GG_ACTION_NONE, "an empty name was accepted");
    CHECK(s.notice[0] != '\0', "an empty name was refused without saying why");

    // Already taken - the one that would otherwise quietly overwrite a game.
    for (const char *c = "Taken"; *c; c++) gg_screens_type(&s, *c);
    r = gg_screens_choose(&s, base, &set, true);
    CHECK(r.action == GG_ACTION_NONE, "a name already in use was accepted");
    CHECK(s.notice[0] != '\0', "a taken name was refused without saying why");

    // Backspace back to something free.
    for (int i = 0; i < 5; i++) gg_screens_type(&s, '\b');
    for (const char *c = "Fresh"; *c; c++) gg_screens_type(&s, *c);
    r = gg_screens_choose(&s, base, &set, true);
    CHECK(r.action == GG_ACTION_NEW_GAME, "a free name was not accepted");
    CHECK(SDL_strcmp(r.name, "Fresh") == 0, "the name came through as '%s'", r.name);

    gg_game_free(&g);
    wipe_saves("Taken");
}

static void forgetting_a_journey_takes_two_steps(void) {
    const char *base = save_base();
    wipe_saves("Doomed");

    gg_settings set;
    gg_settings_defaults(&set);
    gg_game g;
    CHECK(gg_game_new(&g, 6, "Doomed"), "new game failed");
    CHECK(gg_save_write(&g, base, "Doomed"), "save failed");

    gg_screens s;
    SDL_zero(s);
    gg_screens_enter(&s, GG_SCREEN_PROFILES, base, &set, &g, true);
    CHECK(s.profile_count >= 1, "the picker found no profiles");

    // Choosing a profile normally continues it.
    gg_menu_select(&s.menu, 0);
    gg_screen_result r = gg_screens_choose(&s, base, &set, true);
    CHECK(r.action == GG_ACTION_CONTINUE, "choosing a journey did not continue it");

    // Arming "forget", then choosing, deletes. One keypress must never be
    // enough to destroy somebody's game.
    gg_screens_enter(&s, GG_SCREEN_PROFILES, base, &set, &g, true);
    gg_menu_select(&s.menu, s.profile_count + 1);       // "Forget a journey"
    r = gg_screens_choose(&s, base, &set, true);
    CHECK(r.action == GG_ACTION_NONE, "arming the delete deleted something");
    CHECK(s.confirming_delete, "the delete was not armed");
    CHECK(s.notice[0] != '\0', "the armed state is not shown to the player");

    gg_menu_select(&s.menu, 0);
    r = gg_screens_choose(&s, base, &set, true);
    CHECK(r.action == GG_ACTION_DELETE, "the armed delete did not fire");

    // And backing out disarms it rather than leaving a loaded gun.
    gg_screens_enter(&s, GG_SCREEN_PROFILES, base, &set, &g, true);
    gg_menu_select(&s.menu, s.profile_count + 1);
    gg_screens_choose(&s, base, &set, true);
    gg_screens_back(&s);
    CHECK(!s.confirming_delete, "backing out left the delete armed");

    gg_game_free(&g);
    wipe_saves("Doomed");
}

static void every_screen_can_be_left(void) {
    // A screen with no way out is the classic menu bug, and it is invisible
    // until somebody is stuck in it.
    const char *base = save_base();
    gg_settings set;
    gg_settings_defaults(&set);
    gg_game g;
    CHECK(gg_game_new(&g, 6, "Exit"), "new game failed");

    static const gg_screen_id SCREENS[] = {
        GG_SCREEN_PROFILES, GG_SCREEN_NAME, GG_SCREEN_OPTIONS, GG_SCREEN_PAUSE,
    };
    for (size_t i = 0; i < GG_COUNTOF(SCREENS); i++) {
        gg_screens s;
        SDL_zero(s);
        gg_screens_enter(&s, SCREENS[i], base, &set, &g, true);
        const gg_screen_result r = gg_screens_back(&s);
        CHECK(r.action == GG_ACTION_GO,
              "screen %d has no way back out", (int)SCREENS[i]);
        CHECK(r.next != SCREENS[i], "screen %d backs out to itself", (int)SCREENS[i]);
    }

    // And the pause menu's first row resumes, so the commonest thing needs no
    // thought.
    gg_screens s;
    SDL_zero(s);
    gg_screens_enter(&s, GG_SCREEN_PAUSE, base, &set, &g, true);
    gg_menu_select(&s.menu, 0);
    const gg_screen_result r = gg_screens_choose(&s, base, &set, true);
    CHECK(r.action == GG_ACTION_GO && r.next == GG_SCREEN_PLAY,
          "the first row of the pause menu does not resume");

    gg_game_free(&g);
    wipe_saves("Exit");
}

static void the_options_page_cycles_its_values(void) {
    const char *base = save_base();
    gg_settings set;
    gg_settings_defaults(&set);
    gg_game g;
    CHECK(gg_game_new(&g, 6, "Opt"), "new game failed");

    gg_screens s;
    SDL_zero(s);
    gg_screens_enter(&s, GG_SCREEN_OPTIONS, base, &set, &g, true);

    // Window size wraps rather than sticking at the top, so there is no dead
    // end a player has to back out of.
    gg_menu_select(&s.menu, 0);
    const int start = set.scale;
    for (int i = 0; i < 4; i++) gg_screens_choose(&s, base, &set, true);
    CHECK(set.scale == start, "cycling window size four times did not return: %d",
          set.scale);
    CHECK(set.scale >= 1 && set.scale <= 4, "window size left its range: %d", set.scale);

    gg_menu_select(&s.menu, 1);
    const bool fs = set.fullscreen;
    gg_screens_choose(&s, base, &set, true);
    CHECK(set.fullscreen != fs, "fullscreen did not toggle");

    // The disabled sound rows must not be reachable, or a player lands on a
    // control that does nothing.
    CHECK(!s.menu.item[3].enabled && !s.menu.item[4].enabled,
          "the sound rows are selectable before there is any sound");

    gg_game_free(&g);
    wipe_saves("Opt");
}

// Runs the cursor down a menu looking for a row by name, using nothing but the
// move a direction produces. A row that cannot be landed on this way is
// unreachable however good it looks on screen.
static bool menu_reach(gg_screens *s, const char *label) {
    for (int i = 0; i <= s->menu.n; i++) {
        if (SDL_strcmp(s->menu.item[s->menu.cursor].label, label) == 0) return true;
        gg_screens_move(s, 0, 1);
    }
    return false;
}

// A pad offers directions and a choose, and nothing else. Every screen has to
// be reachable with those alone, or a controller player is shut out of part of
// the game with no way to tell why.
static void every_screen_is_reachable_by_directions_alone(void) {
    const char *base = save_base();
    wipe_saves("Reach");

    gg_settings set;
    gg_settings_defaults(&set);
    gg_game g;
    CHECK(gg_game_new(&g, 6, "Reach"), "new game failed");
    CHECK(gg_save_write(&g, base, "Reach"), "save failed");

    bool seen[GG_SCREEN_COUNT];
    SDL_zero(seen);

    gg_screens s;
    SDL_zero(s);
    gg_screens_enter(&s, GG_SCREEN_TITLE, base, &set, &g, true);
    seen[GG_SCREEN_TITLE] = true;

    static const struct { const char *row; gg_screen_id lands; } WAYS[] = {
        { "New journey", GG_SCREEN_NAME },
        { "Journeys",    GG_SCREEN_PROFILES },
        { "Options",     GG_SCREEN_OPTIONS },
    };
    for (size_t i = 0; i < GG_COUNTOF(WAYS); i++) {
        gg_screens_enter(&s, GG_SCREEN_TITLE, base, &set, &g, true);
        CHECK(menu_reach(&s, WAYS[i].row),
              "the title's '%s' row cannot be reached with directions", WAYS[i].row);

        const gg_screen_result r = gg_screens_choose(&s, base, &set, true);
        CHECK(r.action == GG_ACTION_GO && r.next == WAYS[i].lands,
              "'%s' led to %d rather than %d", WAYS[i].row, (int)r.next,
              (int)WAYS[i].lands);
        seen[WAYS[i].lands] = true;
    }

    // Continue leads to the world, which is the only screen not reached by a
    // GO - the frontend has a save to load first.
    gg_screens_enter(&s, GG_SCREEN_TITLE, base, &set, &g, true);
    CHECK(menu_reach(&s, "Continue"), "'Continue' cannot be reached with directions");
    const gg_screen_result cont = gg_screens_choose(&s, base, &set, true);
    CHECK(cont.action == GG_ACTION_CONTINUE, "'Continue' did not continue");
    CHECK(cont.name[0] != '\0', "'Continue' named no journey to continue");
    seen[GG_SCREEN_PLAY] = true;

    // And from the world, backing out is the pause menu - which on a pad is
    // Start, the one button that means something on every screen.
    gg_screens_enter(&s, GG_SCREEN_PLAY, base, &set, &g, true);
    const gg_screen_result paused = gg_screens_back(&s);
    CHECK(paused.action == GG_ACTION_GO && paused.next == GG_SCREEN_PAUSE,
          "the world does not lead to the pause menu");
    seen[GG_SCREEN_PAUSE] = true;

    for (int i = 0; i < GG_SCREEN_COUNT; i++)
        CHECK(seen[i], "screen %d cannot be reached with directions alone", i);

    gg_game_free(&g);
    wipe_saves("Reach");
}

// Options is the one screen with two ways in. Backing out of it from a paused
// game must return to the game, not drop to the title - which would strand a
// player at the front door with unsaved turns behind them.
static void the_options_page_returns_where_it_came_from(void) {
    const char *base = save_base();
    gg_settings set;
    gg_settings_defaults(&set);
    gg_game g;
    CHECK(gg_game_new(&g, 6, "Whence"), "new game failed");

    gg_screens s;
    SDL_zero(s);

    // From the title.
    gg_screens_enter(&s, GG_SCREEN_TITLE, base, &set, &g, true);
    gg_screens_enter(&s, GG_SCREEN_OPTIONS, base, &set, &g, true);
    gg_screen_result r = gg_screens_back(&s);
    CHECK(r.action == GG_ACTION_GO && r.next == GG_SCREEN_TITLE,
          "options from the title went to %d", (int)r.next);

    // From a paused game, by backing out and by the "Back" row alike.
    gg_screens_enter(&s, GG_SCREEN_PAUSE, base, &set, &g, true);
    gg_screens_enter(&s, GG_SCREEN_OPTIONS, base, &set, &g, true);
    r = gg_screens_back(&s);
    CHECK(r.action == GG_ACTION_GO && r.next == GG_SCREEN_PAUSE,
          "backing out of options abandoned a paused game (went to %d)",
          (int)r.next);

    gg_screens_enter(&s, GG_SCREEN_PAUSE, base, &set, &g, true);
    gg_screens_enter(&s, GG_SCREEN_OPTIONS, base, &set, &g, true);
    gg_menu_select(&s.menu, s.menu.n - 1);            // the "Back" row
    r = gg_screens_choose(&s, base, &set, true);
    CHECK(r.action == GG_ACTION_GO && r.next == GG_SCREEN_PAUSE,
          "the Back row abandoned a paused game (went to %d)", (int)r.next);

    // With no game to go back to, the pause screen is not a destination.
    gg_screens_enter(&s, GG_SCREEN_PAUSE, base, &set, &g, false);
    gg_screens_enter(&s, GG_SCREEN_OPTIONS, base, &set, &g, false);
    r = gg_screens_back(&s);
    CHECK(r.action == GG_ACTION_GO && r.next == GG_SCREEN_TITLE,
          "options sent a player to a pause screen with no game behind it");

    gg_game_free(&g);
    wipe_saves("Whence");
}

static void a_value_can_be_nudged_both_ways_without_leaving_the_page(void) {
    const char *base = save_base();
    gg_settings set;
    gg_settings_defaults(&set);
    gg_game g;
    CHECK(gg_game_new(&g, 6, "Nudge"), "new game failed");

    gg_screens s;
    SDL_zero(s);
    gg_screens_enter(&s, GG_SCREEN_OPTIONS, base, &set, &g, true);

    // Forwards then backwards must land exactly where it started, or a player
    // cannot undo a nudge without cycling all the way round.
    gg_menu_select(&s.menu, 0);
    const int start = set.scale;
    gg_screens_adjust(&s, 1, &set);
    CHECK(set.scale != start, "nudging window size right changed nothing");
    gg_screens_adjust(&s, -1, &set);
    CHECK(set.scale == start, "right then left did not return: %d vs %d",
          set.scale, start);

    // And backwards from the bottom wraps to the top rather than sticking.
    while (set.scale > 1) gg_screens_adjust(&s, -1, &set);
    gg_screens_adjust(&s, -1, &set);
    CHECK(set.scale == 4, "window size did not wrap downwards: %d", set.scale);

    // The rows that are not values must ignore this entirely. "Back" is the
    // one that would otherwise leave the page on a sideways nudge.
    const int back = s.menu.n - 1;
    gg_menu_select(&s.menu, back);
    const gg_settings before = set;
    gg_screens_adjust(&s, 1, &set);
    gg_screens_adjust(&s, -1, &set);
    CHECK(s.id == GG_SCREEN_OPTIONS, "a sideways nudge left the options page");
    CHECK(SDL_memcmp(&before, &set, sizeof set) == 0,
          "nudging the Back row changed a setting");

    // Nor should any other screen react to one.
    gg_screens_enter(&s, GG_SCREEN_TITLE, base, &set, &g, true);
    const int cursor = s.menu.cursor;
    gg_screens_adjust(&s, 1, &set);
    CHECK(s.menu.cursor == cursor && s.id == GG_SCREEN_TITLE,
          "a sideways nudge disturbed the title screen");

    gg_game_free(&g);
    wipe_saves("Nudge");
}

// A pad has no keys. The naming screen carries its own alphabet so that a
// journey can be started without ever touching a keyboard - this walks it the
// way a controller would, with nothing but directions and a choose.
static void a_journey_can_be_named_with_directions_alone(void) {
    const char *base = save_base();
    wipe_saves("BAD");

    gg_settings set;
    gg_settings_defaults(&set);
    gg_game g;
    CHECK(gg_game_new(&g, 6, "Pad"), "new game failed");

    gg_screens s;
    SDL_zero(s);
    gg_screens_enter(&s, GG_SCREEN_NAME, base, &set, &g, true);

    // Untouched, the alphabet is out of the way and choosing means "begin" -
    // so the keyboard flow is exactly what it was before the grid existed.
    CHECK(s.key_row < 0, "the alphabet started with a cursor on it");
    gg_screen_result r = gg_screens_choose(&s, base, &set, true);
    CHECK(r.action == GG_ACTION_NONE, "an empty name was accepted");

    // The first direction lands on the first letter, whichever way it was.
    gg_screens_move(&s, 0, 1);
    CHECK(s.key_row == 0 && s.key_col == 0, "the alphabet opened at %d,%d",
          s.key_row, s.key_col);

    // Choosing on the grid types rather than beginning.
    r = gg_screens_choose(&s, base, &set, true);
    CHECK(r.action == GG_ACTION_NONE, "choosing a letter began the journey");
    CHECK(SDL_strcmp(s.typed, "A") == 0, "typing 'A' gave '%s'", s.typed);

    // Every cell must be reachable, and the grid must wrap in both directions
    // rather than trapping the cursor against an edge.
    for (int i = 0; i < 11; i++) gg_screens_move(&s, 1, 0);
    CHECK(s.key_col == 0, "eleven steps right did not come back round: %d", s.key_col);
    gg_screens_move(&s, -1, 0);
    CHECK(s.key_col == 10, "stepping left off the edge did not wrap: %d", s.key_col);

    // Spell "BAD" - B is next to A, and D two further on - to prove the layout
    // is what the drawing claims and not merely self-consistent.
    gg_screens_move(&s, 1, 0);           // back to column 0
    for (int i = 0; i < 5; i++) gg_screens_type(&s, '\b');
    CHECK(s.typed[0] == '\0', "backspacing did not clear the name");

    gg_screens_move(&s, 1, 0);           // A -> B
    gg_screens_choose(&s, base, &set, true);
    gg_screens_move(&s, -1, 0);          // B -> A
    gg_screens_choose(&s, base, &set, true);
    gg_screens_move(&s, 1, 0);
    gg_screens_move(&s, 1, 0);
    gg_screens_move(&s, 1, 0);           // A -> D
    gg_screens_choose(&s, base, &set, true);
    CHECK(SDL_strcmp(s.typed, "BAD") == 0, "the alphabet spelled '%s'", s.typed);

    // Down past the last row reaches the two wide keys. Coming down from the
    // left of the grid lands on "Rub out", which must rub out and nothing more.
    for (int i = 0; i < 6; i++) gg_screens_move(&s, 0, 1);
    r = gg_screens_choose(&s, base, &set, true);
    CHECK(r.action == GG_ACTION_NONE, "Rub out began the journey");
    CHECK(SDL_strcmp(s.typed, "BA") == 0, "Rub out left '%s'", s.typed);

    // Its neighbour is "Begin", and choosing there starts the journey.
    gg_screens_move(&s, 1, 0);
    for (const char *c = "D"; *c; c++) gg_screens_type(&s, *c);
    r = gg_screens_choose(&s, base, &set, true);
    CHECK(r.action == GG_ACTION_NEW_GAME, "Begin did not begin");
    CHECK(SDL_strcmp(r.name, "BAD") == 0, "the pad-typed name came through as '%s'",
          r.name);

    // And Start reaches Begin from anywhere on the grid.
    gg_screens_enter(&s, GG_SCREEN_NAME, base, &set, &g, true);
    for (const char *c = "Padded"; *c; c++) gg_screens_type(&s, *c);
    gg_screens_move(&s, 0, 1);
    gg_screens_move(&s, 1, 0);
    gg_screens_ready(&s);
    r = gg_screens_choose(&s, base, &set, true);
    CHECK(r.action == GG_ACTION_NEW_GAME, "Start did not accept the name");
    CHECK(SDL_strcmp(r.name, "Padded") == 0, "Start gave '%s'", r.name);

    // Whatever the alphabet can produce, the name rules must accept - an
    // unreachable-but-legal character would be a trap for a pad-only player,
    // and a legal-looking key that is refused would be worse.
    gg_screens_enter(&s, GG_SCREEN_NAME, base, &set, &g, true);
    gg_screens_move(&s, 0, 1);
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 11; col++) {
            s.key_row = row;
            s.key_col = col;
            s.typed[0] = '\0';
            gg_screens_choose(&s, base, &set, true);
            CHECK(s.typed[0] != '\0', "the key at %d,%d typed nothing", row, col);
            // Probed in the middle of a name: a space is a legal character but
            // not a legal first or last one, and that rule is about position,
            // not about the character being offered.
            char probe[4] = { 'X', s.typed[0], 'Y', '\0' };
            CHECK(gg_profile_name_ok(probe),
                  "the alphabet offers '%c' at %d,%d, which a name may not hold",
                  s.typed[0], row, col);
        }
    }

    gg_game_free(&g);
    wipe_saves("BAD");
    wipe_saves("Padded");
}

// The two drains share one repeat timer. Calling both in a tick would eat every
// other step, so the world and the menus must never both be asked.
static void the_pad_feeds_the_world_and_the_menus_separately(void) {
    gg_input in;
    SDL_zero(in);

    // A held direction steps once, then waits out the delay - the same shape
    // whether it is a sprite walking or a cursor running down a list.
    in.pad_dy = 1;
    in.dy = 1;
    CHECK(gg_input_nav(&in) == GG_NAV_DOWN, "a held direction did not move a menu");
    CHECK(gg_input_nav(&in) == GG_NAV_NONE, "a menu cursor repeated with no delay");

    int steps = 0;
    for (int i = 0; i < GG_REPEAT_DELAY + 2; i++) {
        if (in.repeat > 0) in.repeat--;
        if (gg_input_nav(&in) != GG_NAV_NONE) steps++;
    }
    CHECK(steps == 1, "a held direction repeated %d times over one delay", steps);

    // Vertical wins on a diagonal, so a stick pushed up and slightly left in a
    // column of rows still goes up.
    SDL_zero(in);
    in.dx = -1;
    in.dy = -1;
    CHECK(gg_input_nav(&in) == GG_NAV_UP, "a diagonal did not resolve upwards");

    // Start is readable from either side, and only once.
    SDL_zero(in);
    in.pause_latched = true;
    CHECK(gg_input_take_pause(&in), "Start was not seen in the world");
    CHECK(!gg_input_take_pause(&in), "Start was seen twice");

    in.pause_latched = true;
    CHECK(gg_input_nav(&in) == GG_NAV_ACCEPT, "Start was not seen in a menu");
    CHECK(!gg_input_take_pause(&in), "Start survived being read as a menu command");

    // Forgetting drops everything, which is what stops the button that left a
    // menu from acting on the world it lands in.
    SDL_zero(in);
    in.latched = GG_ACT_TALK;
    in.nav_latched = GG_NAV_CHOOSE;
    in.pause_latched = true;
    gg_input_forget(&in);
    CHECK(gg_input_take(&in) == GG_ACT_NONE, "a world action survived a screen change");
    CHECK(gg_input_nav(&in) == GG_NAV_NONE, "a menu command survived a screen change");
    CHECK(!gg_input_take_pause(&in), "Start survived a screen change");
}

// ---------------------------------------------------------------------------
// Saves and profiles
// ---------------------------------------------------------------------------
// Is one game the same as another, in every way a save is meant to carry?
static bool games_match(const gg_game *a, const gg_game *b, const char **why) {
    if (a->turn != b->turn)       { *why = "turn"; return false; }
    if (a->minutes != b->minutes) { *why = "minutes"; return false; }
    if (a->day != b->day)         { *why = "day"; return false; }
    if (a->exp != b->exp)         { *why = "experience"; return false; }
    if (a->trailn != b->trailn)   { *why = "trail length"; return false; }
    for (int i = 0; i < a->trailn; i++)
        if (a->trail_x[i] != b->trail_x[i] || a->trail_y[i] != b->trail_y[i]) {
            *why = "the trail"; return false;
        }
    if (a->rng.s != b->rng.s)     { *why = "rng"; return false; }
    if (a->player != b->player)   { *why = "player index"; return false; }
    if (a->actors != b->actors)   { *why = "actor count"; return false; }
    if (a->knownn != b->knownn)   { *why = "words known"; return false; }
    for (int i = 0; i < a->knownn; i++)
        if (SDL_strcmp(a->known[i], b->known[i]) != 0) { *why = "a word"; return false; }
    if (a->packn != b->packn)     { *why = "pack size"; return false; }
    for (int i = 0; i < a->packn; i++)
        if (a->pack[i].kind != b->pack[i].kind ||
            a->pack[i].count != b->pack[i].count) { *why = "pack"; return false; }
    for (int s = 0; s < GG_SLOT_COUNT; s++)
        if (a->equipped[s] != b->equipped[s]) { *why = "what is held"; return false; }
    if (a->map.grounds != b->map.grounds) { *why = "things on the ground"; return false; }
    for (int i = 0; i < a->map.grounds; i++)
        if (a->map.ground[i].x != b->map.ground[i].x ||
            a->map.ground[i].y != b->map.ground[i].y ||
            a->map.ground[i].kind != b->map.ground[i].kind ||
            a->map.ground[i].count != b->map.ground[i].count) {
            *why = "a pile on the ground"; return false;
        }
    if (SDL_strcmp(a->profile, b->profile) != 0) { *why = "profile"; return false; }

    for (int i = 0; i < a->actors; i++) {
        const gg_actor *x = &a->actor[i], *y = &b->actor[i];
        if (x->active != y->active || x->x != y->x || x->y != y->y ||
            x->art != y->art || x->facing != y->facing || x->def != y->def ||
            x->hp != y->hp || x->hp_max != y->hp_max || x->level != y->level ||
            x->party != y->party || x->hostile != y->hostile ||
            x->speed != y->speed || x->energy != y->energy ||
            x->damage != y->damage || x->guard != y->guard ||
            x->beast != y->beast || x->reach != y->reach ||
            x->notice != y->notice || x->flees != y->flees ||
            x->schedn != y->schedn || SDL_strcmp(x->name, y->name) != 0) {
            *why = "an actor";
            return false;
        }
        for (int k = 0; k < x->schedn; k++)
            if (x->sched[k].hour != y->sched[k].hour ||
                x->sched[k].x != y->sched[k].x || x->sched[k].y != y->sched[k].y) {
                *why = "a schedule";
                return false;
            }
    }

    if (a->map.w != b->map.w || a->map.h != b->map.h) { *why = "map size"; return false; }
    if (SDL_memcmp(a->map.cell, b->map.cell,
                   (size_t)a->map.w * (size_t)a->map.h * sizeof *a->map.cell) != 0) {
        *why = "map cells";
        return false;
    }
    *why = "";
    return true;
}

static void a_saved_game_resumes_exactly_where_it_was_left(void) {
    const char *who = "Roundtrip";
    wipe_saves(who);

    gg_game a;
    CHECK(gg_game_new(&a, 4242, who), "new game failed");
    // Play a while, so the save is of a world in motion rather than a fresh
    // one: the clock has turned, residents have walked, the RNG has advanced.
    for (int t = 0; t < 250; t++) gg_game_act(&a, GG_ACT_WAIT);
    // Alter what is carried, hold something, and leave something underfoot, so
    // the save is asked to carry all three and not merely the clock.
    gg_pack_add(&a, GG_ITEM_GOLD, 137);
    gg_pack_add(&a, GG_ITEM_TORCH, 1);
    a.pack_cursor = gg_pack_find(&a, GG_ITEM_TORCH);
    gg_game_act(&a, GG_ACT_PACK);
    gg_game_act(&a, GG_ACT_EQUIP);
    gg_game_act(&a, GG_ACT_PACK);
    gg_ground_drop(&a.map, gg_player_const(&a)->x, gg_player_const(&a)->y,
                   GG_ITEM_SILVER, 2);
    gg_player(&a)->hp = 21;

    CHECK(gg_save_write(&a, save_base(), who), "save failed");

    gg_game b;
    SDL_zero(b);
    CHECK(gg_save_read(&b, save_base(), who), "load failed");

    const char *why = "";
    CHECK(games_match(&a, &b, &why), "the resumed game differs in %s", why);

    // And it must carry on the same: the RNG survived, so the same seed of
    // decisions follows.
    for (int t = 0; t < 60; t++) { gg_game_act(&a, GG_ACT_WAIT); gg_game_act(&b, GG_ACT_WAIT); }
    CHECK(games_match(&a, &b, &why),
          "the resumed game diverged after %s", why);

    gg_game_free(&a);
    gg_game_free(&b);
    wipe_saves(who);
}

static void a_resumed_game_can_still_be_talked_to(void) {
    // A greeting is a pointer into a static table, so it cannot be written to
    // a file. If the rebind is ever dropped, every resident loads mute - and
    // nothing else about the save would look wrong.
    const char *who = "Rebind";
    wipe_saves(who);

    gg_game a;
    CHECK(gg_game_new(&a, 11, who), "new game failed");
    CHECK(gg_save_write(&a, save_base(), who), "save failed");

    gg_game b;
    SDL_zero(b);
    CHECK(gg_save_read(&b, save_base(), who), "load failed");

    int with_greeting = 0, residents = 0;
    for (int i = 0; i < b.actors; i++) {
        if (i == b.player || !b.actor[i].active) continue;
        // Only the townsfolk. Brigands are built rather than taken from the
        // table, so they have no entry to be rebound from and no greeting to
        // come back with - which is correct, not mute.
        if (b.actor[i].def == GG_ACTOR_NO_DEF) continue;
        residents++;
        if (b.actor[i].greeting && b.actor[i].greeting[0]) with_greeting++;
    }
    CHECK(residents > 0, "the loaded game has no residents");
    CHECK(with_greeting == residents,
          "%d of %d residents came back mute", residents - with_greeting, residents);

    gg_game_free(&a);
    gg_game_free(&b);
    wipe_saves(who);
}

static void profiles_do_not_see_each_others_saves(void) {
    wipe_saves("Alice");
    wipe_saves("Bob");

    gg_game a, b;
    CHECK(gg_game_new(&a, 1, "Alice"), "new game failed");
    CHECK(gg_game_new(&b, 2, "Bob"), "new game failed");
    for (int t = 0; t < 30; t++) gg_game_act(&b, GG_ACT_WAIT);

    CHECK(gg_save_write(&a, save_base(), "Alice"), "Alice's save failed");
    CHECK(gg_save_write(&b, save_base(), "Bob"), "Bob's save failed");

    gg_game loaded;
    SDL_zero(loaded);
    CHECK(gg_save_read(&loaded, save_base(), "Alice"), "could not load Alice");
    CHECK(loaded.turn == a.turn,
          "Alice loaded Bob's world: turn %u, expected %u", loaded.turn, a.turn);
    CHECK(SDL_strcmp(loaded.profile, "Alice") == 0,
          "Alice's save says it belongs to '%s'", loaded.profile);

    // Deleting one must leave the other alone.
    gg_profile_delete(save_base(), "Bob");
    CHECK(!gg_save_exists(save_base(), "Bob"), "Bob's save survived deletion");
    CHECK(gg_save_exists(save_base(), "Alice"), "Alice's save went with Bob's");

    gg_game_free(&a);
    gg_game_free(&b);
    gg_game_free(&loaded);
    wipe_saves("Alice");
}

static void a_profile_name_cannot_steer_a_path(void) {
    // A profile name becomes a directory name. Every one of these is a way out
    // of the directory it is supposed to stay in.
    static const char *const BAD[] = {
        "", " ", "..", ".", "../escape", "a/b", "a\\b", "a:b", " leading",
        "trailing ", "nul\tchar", "star*", "quote\"", "pipe|",
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",   // too long
    };
    for (size_t i = 0; i < GG_COUNTOF(BAD); i++)
        CHECK(!gg_profile_name_ok(BAD[i]), "'%s' was accepted as a name", BAD[i]);

    static const char *const GOOD[] = { "Gavin", "Lord British", "a", "x_1-2", "O'Neil" };
    for (size_t i = 0; i < GG_COUNTOF(GOOD); i++)
        CHECK(gg_profile_name_ok(GOOD[i]), "'%s' was rejected", GOOD[i]);

    // And the path builder must refuse the bad ones rather than build one.
    char buf[512];
    CHECK(!gg_profile_dir(save_base(), "../escape", buf, sizeof buf),
          "a traversing name produced a path");
    CHECK(buf[0] == '\0', "a refused path was left half-written");
}

static void a_save_that_is_not_ours_is_refused(void) {
    const char *who = "Garbage";
    wipe_saves(who);

    // Write nonsense where a save would be, by hand.
    char dir[1024], path[1024];
    CHECK(gg_profile_dir(save_base(), who, dir, sizeof dir), "dir failed");
    SDL_CreateDirectory(dir);
    SDL_snprintf(path, sizeof path, "%sworld.ggsave", dir);

    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    CHECK(io != nullptr, "could not write the garbage file");
    if (io) { SDL_WriteIO(io, "not a save file at all", 22); SDL_CloseIO(io); }

    gg_game g;
    SDL_zero(g);
    CHECK(!gg_save_read(&g, save_base(), who), "garbage was accepted as a save");
    CHECK(g.map.cell == nullptr, "a refused load left an allocation behind");

    // A save truncated part way through is the other shape of this, and the
    // one that would leave a half-built world if the loader built in place.
    gg_game good;
    CHECK(gg_game_new(&good, 5, who), "new game failed");
    CHECK(gg_save_write(&good, save_base(), who), "save failed");

    io = SDL_IOFromFile(path, "rb");
    Sint64 size = io ? SDL_GetIOSize(io) : 0;
    char *buf = (size > 0) ? SDL_malloc((size_t)size) : nullptr;
    if (io && buf) SDL_ReadIO(io, buf, (size_t)size);
    if (io) SDL_CloseIO(io);
    CHECK(size > 64, "the save is implausibly small");

    io = SDL_IOFromFile(path, "wb");
    if (io && buf) { SDL_WriteIO(io, buf, (size_t)size / 2); SDL_CloseIO(io); }
    SDL_free(buf);

    SDL_zero(g);
    CHECK(!gg_save_read(&g, save_base(), who), "a truncated save was accepted");
    CHECK(g.map.cell == nullptr, "a truncated load left an allocation behind");

    gg_game_free(&good);
    wipe_saves(who);
}

static void the_profile_list_reports_what_was_saved(void) {
    wipe_saves("Zed");
    wipe_saves("Amy");

    gg_game a, z;
    CHECK(gg_game_new(&a, 3, "Amy"), "new game failed");
    CHECK(gg_game_new(&z, 4, "Zed"), "new game failed");
    for (int t = 0; t < 500; t++) gg_game_act(&z, GG_ACT_WAIT);  // Zed is further on

    CHECK(gg_save_write(&a, save_base(), "Amy"), "save failed");
    CHECK(gg_save_write(&z, save_base(), "Zed"), "save failed");

    gg_profile list[GG_PROFILES_MAX];
    const int n = gg_profile_list(save_base(), list, GG_PROFILES_MAX);
    CHECK(n >= 2, "expected at least two profiles, got %d", n);

    int found_amy = -1, found_zed = -1;
    for (int i = 0; i < n; i++) {
        if (SDL_strcmp(list[i].name, "Amy") == 0) found_amy = i;
        if (SDL_strcmp(list[i].name, "Zed") == 0) found_zed = i;
    }
    CHECK(found_amy >= 0 && found_zed >= 0, "a saved profile is missing from the list");
    if (found_zed >= 0) {
        CHECK(list[found_zed].turns == z.turn, "Zed's turn count is wrong");
        CHECK(list[found_zed].has_save, "Zed's row says there is no save");
        CHECK(list[found_zed].place[0] != '\0', "Zed's row has no place");
    }
    // Sorted with the furthest-on first, so "continue" lands on the right row.
    if (found_amy >= 0 && found_zed >= 0)
        CHECK(found_zed < found_amy, "the list is not newest-first");

    gg_game_free(&a);
    gg_game_free(&z);
    wipe_saves("Amy");
    wipe_saves("Zed");
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
            // Clear of whatever the avatar happens to be holding, plus a
            // margin: the point of this test is the lamp, not the torch.
            if (gg_dist_cheb(p->x, p->y, x, y) <= gg_light_radius(&g) + 2) continue;
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

    // Out in the wilderness, well away from the town. The avatar starts on the
    // square, which now has a fire on it, and a second light beside the one
    // being measured makes the falloff anything but monotonic.
    gg_actor *pm = gg_player(&g);
    pm->x = 30; pm->y = 30; pm->step = 0;
    const gg_actor *p = gg_player_const(&g);
    for (int oy = -GG_LIGHT_MAX_RADIUS; oy <= GG_LIGHT_MAX_RADIUS; oy++)
        for (int ox = -GG_LIGHT_MAX_RADIUS; ox <= GG_LIGHT_MAX_RADIUS; ox++) {
            const gg_cell *c = gg_map_at_const(&g.map, p->x + ox, p->y + oy);
            if (c && GG_HAS_PROP(c))
                CHECK(GG_PROP_SIZE[GG_PROP_OF(c)].light == 0,
                      "the test spot has another light in it at %d,%d",
                      p->x + ox, p->y + oy);
        }

    // Empty-handed first. The reach is an arm's length, deliberately: a player
    // with no torch is in the dark, but is never blind.
    g.packn = 0;
    for (int s = 0; s < GG_SLOT_COUNT; s++) g.equipped[s] = -1;
    CHECK(gg_light_radius(&g) == 1,
          "empty hands should reach one tile, got %d", gg_light_radius(&g));
    CHECK(gg_light_at(&g, p->x, p->y, day) == GG_LIGHT_FULL,
          "the avatar's own tile should be lit even with nothing in hand");
    CHECK(gg_light_at(&g, p->x + 3, p->y, day) == 0,
          "empty hands lit a tile three away");

    // Now hold a torch. The radius is the torch's, out of the item table -
    // light comes from the object, and this is the object the player carries.
    const int torch_r = GG_ITEM[GG_ITEM_TORCH].light;
    CHECK(gg_pack_add(&g, GG_ITEM_TORCH, 1) == 1, "could not take a torch");
    g.pack_cursor = gg_pack_find(&g, GG_ITEM_TORCH);
    g.mode = GG_MODE_PACK;
    gg_game_act(&g, GG_ACT_EQUIP);
    g.mode = GG_MODE_PLAY;
    CHECK(gg_light_radius(&g) == torch_r,
          "a held torch should reach %d tiles, got %d", torch_r,
          gg_light_radius(&g));

    const uint8_t here = gg_light_at(&g, p->x, p->y, day);
    CHECK(here == GG_LIGHT_FULL, "the avatar's own tile should be fully lit, got %u",
          here);

    // Strictly decreasing out to the radius, then nothing.
    uint8_t prev = here;
    for (int d = 1; d <= torch_r; d++) {
        const uint8_t at = gg_light_at(&g, p->x + d, p->y, day);
        CHECK(at < prev, "light at %d tiles (%u) did not fall below %d tiles (%u)",
              d, at, d - 1, prev);
        prev = at;
    }
    CHECK(gg_light_at(&g, p->x + torch_r + 3, p->y, day) == 0,
          "the carried light reaches further than its radius");

    // And putting it away puts the light out again, which is what makes a
    // torch worth carrying at all.
    gg_game_act(&g, GG_ACT_PACK);
    gg_game_act(&g, GG_ACT_EQUIP);
    gg_game_act(&g, GG_ACT_PACK);
    CHECK(gg_light_radius(&g) == 1,
          "putting the torch away left the light on (%d)", gg_light_radius(&g));
    gg_game_free(&g);
}

static void a_lamp_indoors_does_not_light_the_street(void) {
    // The occlusion rule, and the reason it exists: without it every house at
    // night wore a halo, because a room's lamp lit straight through the walls.
    gg_game g;
    CHECK(gg_game_new(&g, 7, "Tester"), "new game failed");
    g.minutes = 0;
    const uint8_t day = gg_game_daylight(&g);

    // Build the situation rather than hunt for one in the generated town: the
    // town square has a fire on it, so an outdoor tile near a house can be lit
    // perfectly legitimately, and hunting found that instead of a leak.
    const int hx = 30, hy = 30;
    for (int y = hy - 12; y <= hy + 6; y++)
        for (int x = hx - 12; x <= hx + 12; x++) {
            gg_cell *c = gg_map_at(&g.map, x, y);
            if (!c) continue;
            c->terrain = GG_TILE_GRASS;
            c->prop = GG_NO_PROP;
            c->flags = 0;
        }
    CHECK(gg_map_place_prop(&g.map, hx, hy, GG_PROP_HOUSE_BRICK_A),
          "could not place a test house");

    int rx0, ry0, rx1, ry1;
    CHECK(gg_prop_interior(GG_PROP_HOUSE_BRICK_A, hx, hy, &rx0, &ry0, &rx1, &ry1),
          "the test house has no room");
    const int lx = rx0, ly = ry0;
    CHECK(gg_map_place_prop(&g.map, lx, ly, GG_PROP_LAMP),
          "could not put a lamp in the room");

    // Park the avatar far away so its own light is not what is being measured.
    gg_actor *pm = gg_player(&g);
    pm->x = 4; pm->y = 4; pm->step = 0;

    CHECK(gg_light_at(&g, lx, ly, day) > 0, "the lamp does not light its own tile");

    // Every outdoor cell within the lamp's reach must be untouched by it.
    int leaked = 0;
    for (int oy = -GG_LIGHT_MAX_RADIUS; oy <= GG_LIGHT_MAX_RADIUS; oy++)
        for (int ox = -GG_LIGHT_MAX_RADIUS; ox <= GG_LIGHT_MAX_RADIUS; ox++) {
            const gg_cell *c = gg_map_at_const(&g.map, lx + ox, ly + oy);
            if (!c || (c->flags & (GG_CELL_INDOORS | GG_CELL_DOOR))) continue;
            if (gg_light_at(&g, lx + ox, ly + oy, day) > 0) leaked++;
        }
    CHECK(leaked == 0, "%d outdoor tiles are lit through the wall", leaked);
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
// The pack
// ---------------------------------------------------------------------------
// The plan's own verification, and the one that matters: a thing taken off the
// ground is in the pack and gone from the world, and both facts survive a save.
static void an_item_taken_off_the_ground_is_in_the_pack_and_gone_from_the_map(void) {
    const char *who = "Taker";
    wipe_saves(who);

    gg_game g;
    CHECK(gg_game_new(&g, 11, who), "new game failed");

    // Put a pile under the avatar's feet. Built explicitly rather than hunted
    // for in the generated world: a test that depends on what the generator
    // happened to scatter is a test that sometimes tests nothing.
    const gg_actor *p = gg_player_const(&g);
    const int x = p->x, y = p->y;
    CHECK(gg_ground_at(&g.map, x, y) < 0, "the avatar started on top of a pile");
    CHECK(gg_ground_drop(&g.map, x, y, GG_ITEM_SILVER, 2), "could not place the pile");

    const int before = gg_pack_count(&g, GG_ITEM_SILVER);
    gg_game_act(&g, GG_ACT_GET);

    CHECK(gg_pack_count(&g, GG_ITEM_SILVER) == before + 2,
          "the pack holds %d bars, expected %d",
          gg_pack_count(&g, GG_ITEM_SILVER), before + 2);
    CHECK(gg_ground_at(&g.map, x, y) < 0,
          "the pile is still on the map after being picked up");

    // And it survives the round trip, which is the half a pack in memory only
    // would pass without.
    CHECK(gg_save_write(&g, save_base(), who), "save failed");
    gg_game b;
    SDL_zero(b);
    CHECK(gg_save_read(&b, save_base(), who), "load failed");
    CHECK(gg_pack_count(&b, GG_ITEM_SILVER) == before + 2,
          "the resumed game carries %d bars, expected %d",
          gg_pack_count(&b, GG_ITEM_SILVER), before + 2);
    CHECK(gg_ground_at(&b.map, x, y) < 0,
          "the resumed map put the pile back on the ground");

    gg_game_free(&b);
    gg_game_free(&g);
    wipe_saves(who);
}

static void what_is_set_down_is_where_it_was_set_down(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 12, "Dropper"), "new game failed");

    const gg_actor *p = gg_player_const(&g);
    const int x = p->x, y = p->y;

    CHECK(gg_pack_add(&g, GG_ITEM_POTION, 3) == 3, "could not take three phials");
    const int slot = gg_pack_find(&g, GG_ITEM_POTION);
    CHECK(slot >= 0, "the phials are not in the pack");

    g.pack_cursor = slot;
    g.mode = GG_MODE_PACK;
    gg_game_act(&g, GG_ACT_DROP);

    CHECK(gg_pack_count(&g, GG_ITEM_POTION) == 0,
          "setting the phials down left %d in the pack",
          gg_pack_count(&g, GG_ITEM_POTION));

    const int here = gg_ground_at(&g.map, x, y);
    CHECK(here >= 0, "nothing was set down");
    CHECK(g.map.ground[here].kind == GG_ITEM_POTION, "the wrong thing was set down");
    CHECK(g.map.ground[here].count == 3, "%u were set down, expected 3",
          g.map.ground[here].count);

    // Taking them back leaves the tile clear again - the two verbs are each
    // other's inverse, which is the property that stops items leaking.
    g.mode = GG_MODE_PLAY;
    gg_game_act(&g, GG_ACT_GET);
    CHECK(gg_pack_count(&g, GG_ITEM_POTION) == 3, "they did not come back");
    CHECK(gg_ground_at(&g.map, x, y) < 0, "the tile still holds something");

    gg_game_free(&g);
}

// A tile can hold more than one kind, and gg_ground_at only ever finds the
// first - so taking one and stopping would strand the rest for good. Found by
// probing a generated town and seeing two piles on the same square.
static void taking_from_a_tile_clears_everything_on_it(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 18, "Sweeper"), "new game failed");
    const gg_actor *p = gg_player_const(&g);
    const int x = p->x, y = p->y;

    g.packn = 0;
    for (int s = 0; s < GG_SLOT_COUNT; s++) g.equipped[s] = -1;

    CHECK(gg_ground_drop(&g.map, x, y, GG_ITEM_BREAD, 2), "drop 1 failed");
    CHECK(gg_ground_drop(&g.map, x, y, GG_ITEM_POTION, 1), "drop 2 failed");
    CHECK(gg_ground_drop(&g.map, x, y, GG_ITEM_APPLE, 3), "drop 3 failed");

    gg_game_act(&g, GG_ACT_GET);

    CHECK(gg_ground_at(&g.map, x, y) < 0,
          "something was left on the tile after taking");
    CHECK(gg_pack_count(&g, GG_ITEM_BREAD) == 2, "the bread was not taken");
    CHECK(gg_pack_count(&g, GG_ITEM_POTION) == 1, "the phial was not taken");
    CHECK(gg_pack_count(&g, GG_ITEM_APPLE) == 3, "the apples were not taken");

    gg_game_free(&g);
}

static void a_second_pile_on_one_tile_joins_the_first(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 13, "Piler"), "new game failed");
    const gg_actor *p = gg_player_const(&g);

    CHECK(gg_ground_drop(&g.map, p->x, p->y, GG_ITEM_GOLD, 10), "first drop failed");
    const int was = g.map.grounds;
    CHECK(gg_ground_drop(&g.map, p->x, p->y, GG_ITEM_GOLD, 5), "second drop failed");

    CHECK(g.map.grounds == was, "two piles of the same kind on one tile");
    const int i = gg_ground_at(&g.map, p->x, p->y);
    CHECK(i >= 0 && g.map.ground[i].count == 15,
          "the joined pile holds %u, expected 15", i >= 0 ? g.map.ground[i].count : 0);

    gg_game_free(&g);
}

// Weight is the thing that makes a pack a decision rather than a list.
static void a_pack_will_not_hold_more_than_it_can_carry(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 14, "Hauler"), "new game failed");

    // Silver is deliberately the heavy one. Ask for far more than a person
    // could lift and see how much actually goes in.
    g.packn = 0;
    for (int s = 0; s < GG_SLOT_COUNT; s++) g.equipped[s] = -1;

    const int each = GG_ITEM[GG_ITEM_SILVER].weight;
    CHECK(each > 0, "silver weighs nothing, so this test proves nothing");

    const int taken = gg_pack_add(&g, GG_ITEM_SILVER, 1000);
    CHECK(taken > 0, "not one bar could be carried");
    CHECK(taken == GG_CARRY_MAX / each,
          "took %d bars, but %d is what fits", taken, GG_CARRY_MAX / each);
    CHECK(gg_pack_weight(&g) <= GG_CARRY_MAX,
          "carrying %d, over the limit of %d", gg_pack_weight(&g), GG_CARRY_MAX);

    // One more must be refused, not squeezed in.
    CHECK(gg_pack_add(&g, GG_ITEM_SILVER, 1) == 0, "one bar too many went in");

    // And picking up off the ground obeys the same limit, taking what it can
    // rather than all or nothing - a player standing on a hoard should get an
    // armful, not a refusal.
    const gg_actor *p = gg_player_const(&g);
    gg_pack_take(&g, gg_pack_find(&g, GG_ITEM_SILVER), 2);
    CHECK(gg_ground_drop(&g.map, p->x, p->y, GG_ITEM_SILVER, 9), "drop failed");
    gg_game_act(&g, GG_ACT_GET);

    CHECK(gg_pack_weight(&g) <= GG_CARRY_MAX,
          "picking up went over the limit: %d", gg_pack_weight(&g));
    const int left = gg_ground_at(&g.map, p->x, p->y);
    CHECK(left >= 0 && g.map.ground[left].count == 7,
          "expected 7 bars left on the ground, found %d",
          left >= 0 ? g.map.ground[left].count : 0);

    gg_game_free(&g);
}

static void eating_costs_the_food_and_mends_the_eater(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 15, "Eater"), "new game failed");

    gg_player(&g)->hp = 5;
    CHECK(gg_pack_add(&g, GG_ITEM_BREAD, 2) == 2, "could not take bread");
    const int slot = gg_pack_find(&g, GG_ITEM_BREAD);
    g.pack_cursor = slot;
    g.mode = GG_MODE_PACK;

    const int had = gg_pack_count(&g, GG_ITEM_BREAD);
    gg_game_act(&g, GG_ACT_USE);

    CHECK(gg_player(&g)->hp == 5 + GG_ITEM[GG_ITEM_BREAD].heal,
          "eating bread took health from %d to %d, expected %d", 5,
          gg_player(&g)->hp, 5 + GG_ITEM[GG_ITEM_BREAD].heal);
    CHECK(gg_pack_count(&g, GG_ITEM_BREAD) == had - 1,
          "eating did not use up a loaf");

    // At full health it is refused rather than wasted.
    gg_player(&g)->hp = gg_player(&g)->hp_max;
    const int spare = gg_pack_count(&g, GG_ITEM_BREAD);
    gg_game_act(&g, GG_ACT_USE);
    CHECK(gg_pack_count(&g, GG_ITEM_BREAD) == spare,
          "a hale avatar ate anyway");

    // And a thing with no use says so rather than vanishing.
    gg_pack_add(&g, GG_ITEM_SILVER, 1);
    g.pack_cursor = gg_pack_find(&g, GG_ITEM_SILVER);
    gg_game_act(&g, GG_ACT_USE);
    CHECK(gg_pack_count(&g, GG_ITEM_SILVER) == 1, "a bar of silver was consumed");

    gg_game_free(&g);
}

static void what_is_held_stays_held_through_the_pack_shifting(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 16, "Holder"), "new game failed");

    g.packn = 0;
    for (int s = 0; s < GG_SLOT_COUNT; s++) g.equipped[s] = -1;

    // Three kinds, so emptying the first slot moves the last one into its
    // place - which is exactly when a held index goes stale.
    gg_pack_add(&g, GG_ITEM_BREAD, 1);
    gg_pack_add(&g, GG_ITEM_APPLE, 1);
    gg_pack_add(&g, GG_ITEM_TORCH, 1);
    CHECK(g.packn == 3, "expected three slots, got %d", g.packn);

    g.pack_cursor = gg_pack_find(&g, GG_ITEM_TORCH);
    g.mode = GG_MODE_PACK;
    gg_game_act(&g, GG_ACT_EQUIP);
    CHECK(g.equipped[GG_SLOT_LIGHT] == gg_pack_find(&g, GG_ITEM_TORCH),
          "the torch is not in hand");
    CHECK(gg_light_radius(&g) == GG_ITEM[GG_ITEM_TORCH].light,
          "holding the torch did not light anything");

    // Empty the first slot. The torch must still be the thing in hand.
    gg_pack_take(&g, gg_pack_find(&g, GG_ITEM_BREAD), 1);
    const int torch = gg_pack_find(&g, GG_ITEM_TORCH);
    CHECK(torch >= 0, "the torch left the pack");
    CHECK(g.equipped[GG_SLOT_LIGHT] == torch,
          "the pack shifted and the held slot now points at %d, not the torch at %d",
          g.equipped[GG_SLOT_LIGHT], torch);
    CHECK(gg_light_radius(&g) == GG_ITEM[GG_ITEM_TORCH].light,
          "the light went out when an unrelated slot emptied");

    // Losing the torch itself puts the light out and holds nothing.
    gg_pack_take(&g, torch, 1);
    CHECK(g.equipped[GG_SLOT_LIGHT] == -1, "a torch that is gone is still held");
    CHECK(gg_light_radius(&g) == 1, "the light outlived the torch");

    gg_game_free(&g);
}

// A thing that cannot be held says so, rather than occupying a slot silently.
static void only_things_meant_to_be_held_can_be_held(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 17, "Wielder"), "new game failed");

    gg_pack_add(&g, GG_ITEM_BREAD, 1);
    g.pack_cursor = gg_pack_find(&g, GG_ITEM_BREAD);
    g.mode = GG_MODE_PACK;
    gg_game_act(&g, GG_ACT_EQUIP);

    for (int s = 0; s < GG_SLOT_COUNT; s++)
        CHECK(g.equipped[s] == -1, "a loaf of bread ended up held in slot %d", s);

    // Holding a second torch replaces the first rather than holding both.
    gg_pack_add(&g, GG_ITEM_TORCH, 1);
    g.pack_cursor = gg_pack_find(&g, GG_ITEM_TORCH);
    gg_game_act(&g, GG_ACT_EQUIP);
    const int first = g.equipped[GG_SLOT_LIGHT];
    CHECK(first >= 0, "the torch was not held");

    // Choosing it again puts it away, which is the other half of one key.
    gg_game_act(&g, GG_ACT_EQUIP);
    CHECK(g.equipped[GG_SLOT_LIGHT] == -1, "choosing a held torch again did not stow it");

    gg_game_free(&g);
}

// Every item the table describes has to be usable by the rules that read it.
static void every_item_is_one_the_rules_can_handle(void) {
    for (int i = 0; i < GG_ITEM_COUNT; i++) {
        const gg_item_def *d = &GG_ITEM[i];
        CHECK(d->tiles_w >= 1 && d->tiles_h >= 1, "item %d is zero-sized", i);
        CHECK(d->slot < GG_SLOT_COUNT, "item %d claims slot %u, past the end",
              i, d->slot);
        CHECK(d->light <= GG_LIGHT_MAX_RADIUS,
              "item %d lights %u tiles, past what the renderer scans", i, d->light);

        // A thing that can be used has to do something, and a thing that does
        // something has to be usable - either half alone is a dead control.
        CHECK((d->use == GG_USE_NONE) == (d->heal == 0),
              "item %d has use %u and heal %u, which do not agree",
              i, d->use, d->heal);

        // Nothing may weigh more than a person can carry, or it could be
        // dropped and never picked up again.
        CHECK(d->weight <= GG_CARRY_MAX,
              "item %d weighs %u, more than the %d anyone can carry",
              i, d->weight, GG_CARRY_MAX);

        // The light slot is for things that light; anything else in it would
        // be held and do nothing.
        if (d->slot == GG_SLOT_LIGHT)
            CHECK(d->light > 0, "item %d is held as a light but gives none", i);
    }
}

// ---------------------------------------------------------------------------
// Conversation
// ---------------------------------------------------------------------------
// Writes a dialogue file and returns its path. Authored here rather than
// pointing at the shipped book, so these tests pin the rules rather than
// whatever the vale's writers last said.
static const char *write_dialogue(const char *text) {
    const char *path = gg_pref_file("test_dialogue.txt");
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    CHECK(io != nullptr, "could not write a dialogue file");
    if (io) {
        const size_t n = SDL_strlen(text);
        CHECK(SDL_WriteIO(io, text, n) == n, "short write on the dialogue file");
        SDL_CloseIO(io);
    }
    return path;
}

// The plan's own verification: a conversation defined entirely in a data file,
// with a topic that only unlocks after another has been asked.
static void a_topic_unlocks_only_after_the_word_is_learned(void) {
    const char *path = write_dialogue(
        "person Smith\n"
        "greet Well met.\n"
        "topic name\n"
        "  say I am the smith.\n"
        "topic job forge\n"
        "  say I keep the forge. Ask of the ORDER.\n"
        "  teach order\n"
        "topic order\n"
        "  say Twenty blades by the equinox, and no steel to make them.\n"
        "bye Mind the sparks.\n");
    CHECK(gg_dialogue_load(path), "the dialogue file did not load");
    CHECK(gg_dialogue_speakers() == 1, "expected one speaker, got %d",
          gg_dialogue_speakers());

    const gg_speaker *s = gg_dialogue_find("Smith");
    CHECK(s != nullptr, "the smith is not in the book");
    if (!s) return;
    CHECK(s->topics == 3, "expected three topics, got %d", s->topics);

    gg_game g;
    CHECK(gg_game_new(&g, 21, "Talker"), "new game failed");

    // A fresh player knows the two words everybody starts with, and nothing
    // else - so ORDER is not askable however plainly it is written in the file.
    CHECK(gg_knows(&g, "name") && gg_knows(&g, "job"),
          "a new game does not know the words everyone starts with");
    CHECK(!gg_knows(&g, "order"), "a new game already knows ORDER");

    // Put the smith in the world and walk up to him.
    g.actor[1] = g.actor[0];
    SDL_strlcpy(g.actor[1].name, "Smith", sizeof g.actor[1].name);
    g.actor[1].active = true;
    g.actor[1].def = 0;
    if (g.actors < 2) g.actors = 2;

    g.talking_to = 1;
    g.mode = GG_MODE_CONVERSE;
    g.speaker = gg_dialogue_find("Smith");
    gg_conversation_refresh(&g);

    CHECK(g.askables == 2, "expected two askable words, got %d", g.askables);
    bool offers_order = false;
    for (int i = 0; i < g.askables; i++)
        if (SDL_strcasecmp(g.askable[i], "order") == 0) offers_order = true;
    CHECK(!offers_order, "ORDER was offered before it was learned");

    // Ask about the job. That is the only thing that hands over the word.
    int job = -1;
    for (int i = 0; i < g.askables; i++)
        if (SDL_strcasecmp(g.askable[i], "job") == 0) job = i;
    CHECK(job >= 0, "JOB is not askable");
    g.ask_cursor = job;
    gg_conversation_ask(&g);

    CHECK(gg_knows(&g, "order"), "asking about the job did not teach ORDER");
    CHECK(g.askables == 3, "the new word did not appear in the list (%d words)",
          g.askables);

    offers_order = false;
    for (int i = 0; i < g.askables; i++)
        if (SDL_strcasecmp(g.askable[i], "order") == 0) offers_order = true;
    CHECK(offers_order, "ORDER is known but not offered");

    // And asking it says what the file says, with nothing about it in C.
    for (int i = 0; i < g.askables; i++)
        if (SDL_strcasecmp(g.askable[i], "order") == 0) g.ask_cursor = i;
    gg_conversation_ask(&g);
    CHECK(g.saids == 1, "expected one line of answer, got %d", g.saids);
    CHECK(SDL_strstr(g.said[0], "equinox") != nullptr,
          "the answer did not come from the file: '%s'", g.said[0]);

    gg_game_free(&g);
    gg_dialogue_clear();
    SDL_RemovePath(path);
}

// A word learned from one person unlocks a different person's topic. This is
// the whole reason the gate is "do you know the word" rather than a per-speaker
// flag, and it is what lets a rumour cross a town.
static void a_word_learned_from_one_person_opens_another(void) {
    const char *path = write_dialogue(
        "person Iolo\n"
        "greet Hail.\n"
        "topic job\n"
        "  say Ask Shamino of the CARAVAN.\n"
        "  teach caravan\n"
        "person Shamino\n"
        "greet The gate is watched.\n"
        "topic job\n"
        "  say I watch the road.\n"
        "topic caravan\n"
        "  say It never came.\n");
    CHECK(gg_dialogue_load(path), "the dialogue file did not load");

    gg_game g;
    CHECK(gg_game_new(&g, 22, "Rumour"), "new game failed");

    // Shamino has nothing to say about the caravan yet.
    g.speaker = gg_dialogue_find("Shamino");
    g.mode = GG_MODE_CONVERSE;
    g.talking_to = 0;
    gg_conversation_refresh(&g);
    CHECK(g.askables == 1, "Shamino offers %d words before the rumour, expected 1",
          g.askables);

    // Hear it from Iolo instead.
    g.speaker = gg_dialogue_find("Iolo");
    gg_conversation_refresh(&g);
    g.ask_cursor = 0;
    gg_conversation_ask(&g);
    CHECK(gg_knows(&g, "caravan"), "Iolo did not teach the word");

    // Now Shamino will answer, without anything having been told about him.
    g.speaker = gg_dialogue_find("Shamino");
    gg_conversation_refresh(&g);
    CHECK(g.askables == 2, "Shamino offers %d words after the rumour, expected 2",
          g.askables);

    gg_game_free(&g);
    gg_dialogue_clear();
    SDL_RemovePath(path);
}

static void what_was_learned_survives_a_save(void) {
    const char *who = "Rememberer";
    wipe_saves(who);

    const char *path = write_dialogue(
        "person Nell\n"
        "greet Eighty winters.\n"
        "topic job\n"
        "  say Remembering is work enough.\n"
        "  teach stones\n"
        "topic stones\n"
        "  say They have stood a long while.\n");
    CHECK(gg_dialogue_load(path), "the dialogue file did not load");

    gg_game a;
    CHECK(gg_game_new(&a, 23, who), "new game failed");
    a.speaker = gg_dialogue_find("Nell");
    a.mode = GG_MODE_CONVERSE;
    a.talking_to = 0;
    gg_conversation_refresh(&a);
    a.ask_cursor = 0;
    gg_conversation_ask(&a);
    CHECK(gg_knows(&a, "stones"), "the word was not learned");

    // Saved mid-conversation on purpose: the words have to come back, and the
    // conversation itself must not - it holds a pointer into the book.
    CHECK(gg_save_write(&a, save_base(), who), "save failed");

    gg_game b;
    SDL_zero(b);
    CHECK(gg_save_read(&b, save_base(), who), "load failed");
    CHECK(gg_knows(&b, "stones"), "the resumed game forgot what it was told");
    CHECK(b.mode == GG_MODE_PLAY, "the resumed game came back mid-conversation");
    CHECK(b.speaker == nullptr, "the resumed game kept a pointer into the book");

    gg_game_free(&b);
    gg_game_free(&a);
    gg_dialogue_clear();
    SDL_RemovePath(path);
    wipe_saves(who);
}

// A book that does not parse must load nothing at all. Half a book puts half a
// conversation in somebody's mouth, which is worse than a silent town.
static void a_dialogue_file_that_does_not_parse_loads_nothing(void) {
    static const char *const BAD[] = {
        "greet Hail.\n",                                  // before any person
        "person A\ngreet Hi.\nsay Orphan.\n",             // say with no topic
        "person A\ngreet Hi.\ntopic\n  say Nothing.\n",   // topic with no word
        "person A\ngreet Hi.\ntopic x\n",                 // topic that says nothing
        "person A\ntopic x\n  say Something.\n",          // person with no greeting
        "person A\ngreet Hi.\nwibble What?\n",            // a word we do not know
    };
    for (size_t i = 0; i < GG_COUNTOF(BAD); i++) {
        const char *path = write_dialogue(BAD[i]);
        CHECK(!gg_dialogue_load(path), "bad book %zu loaded anyway", i);
        CHECK(gg_dialogue_speakers() == 0,
              "bad book %zu left %d speakers behind", i, gg_dialogue_speakers());
        SDL_RemovePath(path);
    }

    // And a file that is not there at all is a clean no, not a crash.
    CHECK(!gg_dialogue_load(gg_pref_file("test_no_such_dialogue.txt")),
          "a missing dialogue file reported success");
    CHECK(gg_dialogue_speakers() == 0, "a missing file left speakers behind");
}

static void synonyms_ask_the_same_topic_and_show_one_label(void) {
    const char *path = write_dialogue(
        "person Iolo\n"
        "greet Hail.\n"
        "topic job market stall\n"
        "  say I keep the stall.\n");
    CHECK(gg_dialogue_load(path), "the dialogue file did not load");

    const gg_speaker *s = gg_dialogue_find("Iolo");
    CHECK(s != nullptr, "Iolo is missing");
    if (s) {
        CHECK(gg_speaker_topic(s, "job") == gg_speaker_topic(s, "market"),
              "a synonym found a different topic");
        CHECK(gg_speaker_topic(s, "STALL") == gg_speaker_topic(s, "job"),
              "matching a keyword is case-sensitive, and should not be");
        CHECK(gg_speaker_topic(s, "elephant") == nullptr,
              "a word nobody wrote found a topic");
    }

    // Knowing any synonym offers the topic, and the label is the first word -
    // the one the author chose - not whichever synonym happened to be learned.
    gg_game g;
    CHECK(gg_game_new(&g, 24, "Syn"), "new game failed");
    g.knownn = 0;
    gg_learn(&g, "market");
    g.speaker = s;
    g.mode = GG_MODE_CONVERSE;
    g.talking_to = 0;
    gg_conversation_refresh(&g);
    CHECK(g.askables == 1, "expected one askable word, got %d", g.askables);
    if (g.askables == 1)
        CHECK(SDL_strcmp(g.askable[0], "job") == 0,
              "the list shows '%s', not the label the author chose", g.askable[0]);

    gg_game_free(&g);
    gg_dialogue_clear();
    SDL_RemovePath(path);
}

// The shipped book has to be one the game can actually read, and its words have
// to be reachable - a topic nobody teaches is a topic nobody can ever ask.
static void the_vale_has_a_book_and_every_word_in_it_is_reachable(void) {
    CHECK(gg_dialogue_load(gg_asset_path("dialogue.txt")),
          "the shipped dialogue file does not load");
    CHECK(gg_dialogue_speakers() > 0, "the shipped book has nobody in it");

    // Collect what the book can teach, plus what everyone starts knowing.
    char vocab[GG_KNOWN_MAX][GG_WORD_MAX];
    int vocabn = 0;
    SDL_strlcpy(vocab[vocabn++], GG_WORD_NAME, GG_WORD_MAX);
    SDL_strlcpy(vocab[vocabn++], GG_WORD_JOB, GG_WORD_MAX);

    // Walked by name, which is the handle the API gives - and naming them here
    // is itself the check that every townsperson has an entry.
    static const char *const WHO[] = {
        "Iolo", "Shamino", "Nell", "Dupre", "Katrina", "Nystul", "Gwenno",
        "Chuckles",
    };
    for (size_t w = 0; w < GG_COUNTOF(WHO); w++) {
        const gg_speaker *s = gg_dialogue_find(WHO[w]);
        CHECK(s != nullptr, "%s is in the town but not in the book", WHO[w]);
        if (!s) continue;
        for (int t = 0; t < s->topics; t++) {
            if (!s->topic[t].teach[0]) continue;
            bool have = false;
            for (int v = 0; v < vocabn; v++)
                if (SDL_strcasecmp(vocab[v], s->topic[t].teach) == 0) have = true;
            if (!have && vocabn < GG_KNOWN_MAX)
                SDL_strlcpy(vocab[vocabn++], s->topic[t].teach, GG_WORD_MAX);
        }
    }

    // Every keyword anyone answers to must be in that vocabulary, or it is a
    // question no player could ever put.
    for (size_t w = 0; w < GG_COUNTOF(WHO); w++) {
        const gg_speaker *s = gg_dialogue_find(WHO[w]);
        if (!s) continue;
        for (int t = 0; t < s->topics; t++) {
            bool reachable = false;
            for (int k = 0; k < s->topic[t].words && !reachable; k++)
                for (int v = 0; v < vocabn && !reachable; v++)
                    if (SDL_strcasecmp(vocab[v], s->topic[t].word[k]) == 0)
                        reachable = true;
            CHECK(reachable,
                  "%s answers to '%s', which nothing in the book ever teaches",
                  WHO[w], s->topic[t].word[0]);
        }
        CHECK(s->bye[0] != '\0', "%s has no parting line", WHO[w]);
    }

    gg_dialogue_clear();
}

// ---------------------------------------------------------------------------
// The party
// ---------------------------------------------------------------------------
// Walks the avatar to (tx, ty) one step at a time, the way a player would.
// Returns the number of steps taken; bounded so a blocked path ends the test
// rather than hanging it - a blocked move costs no turn, so "act until you
// arrive" is exactly the loop that spins forever.
static int walk_to(gg_game *g, int tx, int ty, int budget) {
    int steps = 0;
    while (budget-- > 0) {
        const gg_actor *p = gg_player_const(g);
        if (p->x == tx && p->y == ty) break;
        const int dx = tx > p->x ? 1 : tx < p->x ? -1 : 0;
        const int dy = ty > p->y ? 1 : ty < p->y ? -1 : 0;
        gg_action a = GG_ACT_WAIT;
        if (dx > 0 && dy == 0) a = GG_ACT_E;
        else if (dx < 0 && dy == 0) a = GG_ACT_W;
        else if (dy > 0 && dx == 0) a = GG_ACT_S;
        else if (dy < 0 && dx == 0) a = GG_ACT_N;
        else if (dx > 0 && dy > 0) a = GG_ACT_SE;
        else if (dx > 0 && dy < 0) a = GG_ACT_NE;
        else if (dx < 0 && dy > 0) a = GG_ACT_SW;
        else if (dx < 0 && dy < 0) a = GG_ACT_NW;

        const uint32_t before = g->turn;
        gg_game_act(g, a);
        if (g->turn == before) break;      // blocked; no point going on
        steps++;
    }
    return steps;
}

// The plan's own verification: a companion follows through a door without
// blocking the player, over a long walk.
static void a_companion_follows_through_a_door_without_blocking(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 31, "Leader"), "new game failed");

    // Find a house with a door, and the tiles either side of it. Built from
    // the map rather than hoped for: a test that only sometimes finds a door
    // is a test whose green tick means nothing.
    int dx = -1, dy = -1;
    for (int y = 1; y < g.map.h - 1 && dx < 0; y++)
        for (int x = 1; x < g.map.w - 1 && dx < 0; x++) {
            const gg_cell *c = gg_map_at_const(&g.map, x, y);
            if (!c || !(c->flags & GG_CELL_DOOR)) continue;
            // A door worth testing has open ground outside and a room inside.
            const gg_cell *out = gg_map_at_const(&g.map, x, y + 1);
            const gg_cell *in  = gg_map_at_const(&g.map, x, y - 1);
            if (out && in && gg_map_walkable(&g.map, x, y + 1) &&
                gg_map_walkable(&g.map, x, y - 1) &&
                (in->flags & GG_CELL_INDOORS)) {
                dx = x; dy = y;
            }
        }
    CHECK(dx >= 0, "the generated town has no door with room on both sides");
    if (dx < 0) { gg_game_free(&g); return; }

    // Stand the avatar two south of the door, and a companion behind them.
    gg_actor *p = gg_player(&g);
    p->x = (int16_t)dx; p->y = (int16_t)(dy + 2); p->step = 0;
    g.trailn = 0;

    gg_actor *c = &g.actor[1];
    CHECK(g.actors >= 2, "the world has no one to recruit");
    c->active = true;
    c->x = (int16_t)dx; c->y = (int16_t)(dy + 3); c->step = 0;
    c->schedn = 0;
    CHECK(gg_party_join(&g, 1), "could not recruit");
    CHECK(gg_party_size(&g) == 1, "expected a party of one, got %d",
          gg_party_size(&g));

    // In through the door, and well inside.
    const int in_x = dx, in_y = dy - 1;
    const int steps = walk_to(&g, in_x, in_y, 40);
    CHECK(steps >= 3, "the avatar only managed %d steps to the doorway", steps);
    CHECK(p->x == in_x && p->y == in_y,
          "the avatar is at %d,%d, not the %d,%d it walked to",
          p->x, p->y, in_x, in_y);

    // The companion has to have come through with them - not still be outside.
    const gg_cell *where = gg_map_at_const(&g.map, c->x, c->y);
    CHECK(where != nullptr, "the companion left the map");
    CHECK(gg_dist_cheb(p->x, p->y, c->x, c->y) <= 2,
          "the companion is %d tiles behind after following through a door",
          gg_dist_cheb(p->x, p->y, c->x, c->y));

    // And never on top of the avatar.
    CHECK(!(c->x == p->x && c->y == p->y),
          "the companion is standing on the avatar");

    // Now the long walk. Out again and back, several times, checking every
    // turn that nobody is sharing a tile and the avatar is never stuck.
    const int out_x = dx, out_y = dy + 3;
    for (int lap = 0; lap < 4; lap++) {
        const int there = walk_to(&g, out_x, out_y, 40);
        const int back  = walk_to(&g, in_x, in_y, 40);
        CHECK(there > 0 && back > 0,
              "lap %d: the avatar could not move (%d out, %d back) - blocked by "
              "its own party", lap, there, back);

        for (int i = 0; i < g.actors; i++) {
            if (!g.actor[i].active || i == g.player) continue;
            CHECK(!(g.actor[i].x == p->x && g.actor[i].y == p->y),
                  "lap %d: %s is standing on the avatar", lap, g.actor[i].name);
        }
    }

    gg_game_free(&g);
}

// Whoever is in the way steps aside rather than being talked at. Without this
// the party can wall the avatar into a doorway they just followed them through.
static void a_companion_in_the_way_swaps_places(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 32, "Swapper"), "new game failed");

    gg_actor *p = gg_player(&g);
    gg_actor *c = &g.actor[1];
    c->active = true;
    c->schedn = 0;

    // Find open ground with an open tile beside it, so the swap is the only
    // thing being tested and not the terrain.
    int fx = -1, fy = -1;
    for (int y = 1; y < g.map.h - 1 && fx < 0; y++)
        for (int x = 1; x < g.map.w - 1 && fx < 0; x++)
            if (gg_map_walkable(&g.map, x, y) && gg_map_walkable(&g.map, x + 1, y))
                { fx = x; fy = y; }
    CHECK(fx >= 0, "no two walkable tiles side by side");
    if (fx < 0) { gg_game_free(&g); return; }

    p->x = (int16_t)fx; p->y = (int16_t)fy; p->step = 0;
    c->x = (int16_t)(fx + 1); c->y = (int16_t)fy; c->step = 0;
    CHECK(gg_party_join(&g, 1), "could not recruit");

    const uint32_t before = g.turn;
    gg_game_act(&g, GG_ACT_E);

    CHECK(g.turn == before + 1, "the swap did not cost a turn");
    CHECK(p->x == fx + 1 && p->y == fy,
          "the avatar is at %d,%d, expected %d,%d", p->x, p->y, fx + 1, fy);
    CHECK(c->x == fx && c->y == fy,
          "the companion is at %d,%d, expected %d,%d", c->x, c->y, fx, fy);
    CHECK(g.mode == GG_MODE_PLAY,
          "walking into a companion started a conversation instead of a swap");

    // Somebody who is *not* in the party is still talked to, not shoved.
    gg_party_leave(&g, 1);
    c->x = (int16_t)fx; c->y = (int16_t)fy;
    p->x = (int16_t)(fx + 1); p->y = (int16_t)fy;
    gg_game_act(&g, GG_ACT_W);
    CHECK(g.mode == GG_MODE_CONVERSE,
          "walking into a townsperson no longer talks to them");

    gg_game_free(&g);
}

static void the_line_closes_when_somebody_leaves_it(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 33, "Marshal"), "new game failed");
    CHECK(g.actors > GG_PARTY_MAX, "not enough people to fill a party");

    for (int i = 1; i <= GG_PARTY_MAX; i++) {
        g.actor[i].active = true;
        CHECK(gg_party_join(&g, i), "could not recruit number %d", i);
    }
    CHECK(gg_party_size(&g) == GG_PARTY_MAX, "the party is %d, expected %d",
          gg_party_size(&g), GG_PARTY_MAX);

    // Full means full.
    CHECK(!gg_party_join(&g, GG_PARTY_MAX + 1),
          "a full party took one more");

    // Losing the one in the middle must close the gap, or whoever was behind
    // follows a footprint nobody is making and the line stretches out.
    const int middle = gg_party_at(&g, 2);
    CHECK(middle >= 0, "there is nobody in slot 2");
    gg_party_leave(&g, middle);

    CHECK(gg_party_size(&g) == GG_PARTY_MAX - 1, "the party did not shrink");
    for (int slot = 1; slot <= gg_party_size(&g); slot++)
        CHECK(gg_party_at(&g, slot) >= 0, "slot %d is empty with people behind it",
              slot);
    CHECK(gg_party_at(&g, GG_PARTY_MAX) < 0, "the last slot was not vacated");

    // And somebody who left keeps their own stats and can be taken back.
    CHECK(g.actor[middle].party == GG_NOT_IN_PARTY, "they are still in the line");
    CHECK(g.actor[middle].hp > 0, "they lost their health on leaving");
    CHECK(gg_party_join(&g, middle), "they could not be taken back");

    gg_game_free(&g);
}

// Recruiting is declared in the dialogue file, not in C.
static void a_companion_is_recruited_by_a_topic_in_the_book(void) {
    const char *path = write_dialogue(
        "person Dupre\n"
        "greet Well met.\n"
        "topic job\n"
        "  say I stand about looking dangerous. Say COME if thou needest me.\n"
        "  teach come\n"
        "topic come join\n"
        "  say Aye, somebody has to keep thee alive.\n"
        "  joins\n");
    CHECK(gg_dialogue_load(path), "the dialogue file did not load");

    gg_game g;
    CHECK(gg_game_new(&g, 34, "Recruiter"), "new game failed");
    g.actor[1].active = true;
    SDL_strlcpy(g.actor[1].name, "Dupre", sizeof g.actor[1].name);

    g.talking_to = 1;
    g.mode = GG_MODE_CONVERSE;
    g.speaker = gg_dialogue_find("Dupre");
    gg_conversation_refresh(&g);

    // COME is not offered until the job topic has handed the word over, so
    // nobody can be recruited by a player who has not been asked to be.
    for (int i = 0; i < g.askables; i++)
        CHECK(SDL_strcasecmp(g.askable[i], "come") != 0,
              "COME was offered before it was learned");
    CHECK(gg_party_size(&g) == 0, "somebody joined before being asked");

    for (int i = 0; i < g.askables; i++)
        if (SDL_strcasecmp(g.askable[i], "job") == 0) g.ask_cursor = i;
    gg_conversation_ask(&g);
    CHECK(gg_knows(&g, "come"), "the job topic did not teach COME");

    int come = -1;
    for (int i = 0; i < g.askables; i++)
        if (SDL_strcasecmp(g.askable[i], "come") == 0) come = i;
    CHECK(come >= 0, "COME is known but not offered");
    if (come >= 0) {
        g.ask_cursor = come;
        gg_conversation_ask(&g);
    }

    CHECK(gg_party_size(&g) == 1, "asking COME did not recruit");
    CHECK(g.actor[1].party == 1, "the recruit is in slot %u, expected 1",
          g.actor[1].party);
    CHECK(g.actor[1].schedn == 0,
          "a companion is still keeping their daily schedule");

    // Asking again sends them away, which is the other half of the one word.
    if (come >= 0) {
        g.ask_cursor = come;
        gg_conversation_ask(&g);
    }
    CHECK(gg_party_size(&g) == 0, "asking COME again did not send them away");

    gg_game_free(&g);
    gg_dialogue_clear();
    SDL_RemovePath(path);
}

static void a_party_survives_a_save_in_order(void) {
    const char *who = "Captain";
    wipe_saves(who);

    gg_game a;
    CHECK(gg_game_new(&a, 35, who), "new game failed");
    a.actor[1].active = true;
    a.actor[2].active = true;
    CHECK(gg_party_join(&a, 1), "could not recruit the first");
    CHECK(gg_party_join(&a, 2), "could not recruit the second");
    a.actor[1].hp = 7;

    // Walk a little so there are footprints to carry.
    for (int i = 0; i < 6; i++) gg_game_act(&a, GG_ACT_E);
    CHECK(a.trailn > 1, "walking left no trail");

    CHECK(gg_save_write(&a, save_base(), who), "save failed");
    gg_game b;
    SDL_zero(b);
    CHECK(gg_save_read(&b, save_base(), who), "load failed");

    CHECK(gg_party_size(&b) == 2, "the resumed party is %d, expected 2",
          gg_party_size(&b));
    CHECK(gg_party_at(&b, 1) == 1 && gg_party_at(&b, 2) == 2,
          "the line came back in a different order");
    CHECK(b.actor[1].hp == 7, "a companion's health came back as %d, expected 7",
          b.actor[1].hp);
    CHECK(b.trailn == a.trailn, "the trail came back %d long, expected %d",
          b.trailn, a.trailn);
    for (int i = 0; i < a.trailn; i++)
        CHECK(b.trail_x[i] == a.trail_x[i] && b.trail_y[i] == a.trail_y[i],
              "footprint %d moved across the save", i);

    gg_game_free(&b);
    gg_game_free(&a);
    wipe_saves(who);
}

// ---------------------------------------------------------------------------
// Combat
// ---------------------------------------------------------------------------
// Builds the same fight every time: an empty patch of ground, the Avatar in the
// middle of it, and foes placed exactly. Nothing is hunted for in the generated
// world, so what this measures is the combat rules and not the map.
static bool set_up_encounter(gg_game *g, uint32_t seed, int *cx, int *cy) {
    if (!gg_game_new(g, seed, "Fighter")) return false;

    // Clear the hostiles the world spawns, so only what a test places is in it.
    for (int i = 0; i < g->actors; i++)
        if (g->actor[i].hostile) g->actor[i].active = false;

    // A clear square big enough to hold a foe beyond GG_NOTICE_RANGE and still
    // have room to walk in - otherwise "it has not noticed me yet" cannot be
    // set up at all.
    const int x0 = 2, y0 = 2;
    const int side = GG_NOTICE_RANGE * 2 + 12;
    for (int y = y0; y < y0 + side; y++)
        for (int x = x0; x < x0 + side; x++) {
            gg_cell *c = gg_map_at(&g->map, x, y);
            if (!c) return false;
            c->terrain = GG_TILE_GRASS;
            c->prop = GG_NO_PROP;
            c->flags = 0;
        }

    gg_actor *p = gg_player(g);
    p->x = (int16_t)(x0 + side / 2);
    p->y = (int16_t)(y0 + side / 2);
    p->step = 0;
    p->hp = p->hp_max = 30;
    p->level = 1;
    g->trailn = 0;
    g->mode = GG_MODE_PLAY;

    // Nobody else near enough to wander in and change the arithmetic.
    for (int i = 0; i < g->actors; i++) {
        if (i == g->player || !g->actor[i].active) continue;
        if (gg_dist_cheb(p->x, p->y, g->actor[i].x, g->actor[i].y) < side + 8)
            g->actor[i].active = false;
    }

    *cx = p->x;
    *cy = p->y;
    return true;
}

// One step of an FNV-style hash. A function rather than a macro so the one
// signed-to-unsigned conversion happens once, in a place with a named type,
// rather than at every call site where -Wsign-conversion is right to ask.
static uint32_t mix32(uint32_t h, int32_t v) {
    uint32_t u = 0;
    SDL_memcpy(&u, &v, sizeof u);
    return (h ^ u) * 16777619u;
}

// Plays the encounter out and returns a number that stands for everything that
// happened in it - health, turns, the RNG, what fell and what it left.
static uint32_t play_out_encounter(uint32_t seed) {
    gg_game g;
    int cx = 0, cy = 0;
    if (!set_up_encounter(&g, seed, &cx, &cy)) return 0;

    CHECK(gg_spawn_named(&g, "BRIGAND", cx + 1, cy) >= 0, "no brigand");
    CHECK(gg_spawn_named(&g, "OUTLAW", cx + 3, cy + 1) >= 0, "no outlaw");

    // Swing east until one side is done, bounded so a stalemate ends the test
    // rather than hanging it.
    int turns = 0;
    while (turns++ < 200 && g.mode == GG_MODE_PLAY) {
        bool any = false;
        for (int i = 0; i < g.actors; i++)
            if (g.actor[i].active && g.actor[i].hostile) any = true;
        if (!any) break;
        gg_game_act(&g, GG_ACT_E);
    }

    // A hash of the outcome. Every part of it is integer and seeded, so two
    // runs of the same seed must produce the same number.
    uint32_t h = 2166136261u;
    h = mix32(h, (int32_t)g.turn);
    h = mix32(h, (int32_t)g.rng.s);
    h = mix32(h, gg_player_const(&g)->hp);
    h = mix32(h, (int32_t)g.mode);
    h = mix32(h, g.map.grounds);
    for (int i = 0; i < g.map.grounds; i++) {
        h = mix32(h, g.map.ground[i].x);
        h = mix32(h, g.map.ground[i].y);
        h = mix32(h, g.map.ground[i].kind);
        h = mix32(h, g.map.ground[i].count);
    }
    for (int i = 0; i < g.actors; i++) {
        h = mix32(h, g.actor[i].active ? 1 : 0);
        h = mix32(h, g.actor[i].hp);
        h = mix32(h, g.actor[i].x);
        h = mix32(h, g.actor[i].y);
    }

    gg_game_free(&g);
    return h;
}

// The plan's own verification: a scripted encounter with a fixed seed
// resolving identically every run.
static void a_scripted_encounter_resolves_the_same_way_every_time(void) {
    const uint32_t first = play_out_encounter(4242);
    CHECK(first != 0, "the encounter could not be set up");

    for (int run = 0; run < 4; run++) {
        const uint32_t again = play_out_encounter(4242);
        CHECK(again == first,
              "run %d of the same encounter ended differently (%u vs %u)",
              run, again, first);
    }

    // And a different seed must actually produce a different fight, or the
    // check above would pass on a simulation that ignores its dice.
    const uint32_t other = play_out_encounter(99);
    CHECK(other != first, "two different seeds fought identical battles");
}

static void a_blow_lands_or_misses_by_the_dice_and_never_for_nothing(void) {
    gg_game g;
    int cx = 0, cy = 0;
    CHECK(set_up_encounter(&g, 7, &cx, &cy), "setup failed");

    const int foe = gg_spawn_named(&g, "BRIGAND", cx + 1, cy);
    CHECK(foe >= 0, "no brigand");

    int hits = 0, misses = 0, total = 0;
    for (int i = 0; i < 200; i++) {
        g.actor[foe].hp = g.actor[foe].hp_max;      // keep them standing
        g.actor[foe].active = true;
        const int hurt = gg_strike(&g, g.player, foe);
        if (hurt > 0) { hits++; total += hurt; } else misses++;
    }
    CHECK(hits > 0 && misses > 0,
          "%d hits and %d misses in 200 blows - the dice are not being rolled",
          hits, misses);
    // A blow that connects and takes nothing off reads as a bug however the
    // arithmetic got there.
    CHECK(total >= hits, "some blows landed for no damage at all");

    gg_game_free(&g);
}

static void armour_turns_blows_aside_and_a_weapon_drives_them_home(void) {
    gg_game g;
    int cx = 0, cy = 0;
    CHECK(set_up_encounter(&g, 8, &cx, &cy), "setup failed");

    const int bare = gg_attack_power(&g, g.player);
    CHECK(gg_pack_add(&g, GG_ITEM_HAMMER, 1) == 1, "could not take a hammer");
    g.pack_cursor = gg_pack_find(&g, GG_ITEM_HAMMER);
    g.mode = GG_MODE_PACK;
    gg_game_act(&g, GG_ACT_EQUIP);
    g.mode = GG_MODE_PLAY;

    CHECK(gg_attack_power(&g, g.player) == bare + GG_ITEM[GG_ITEM_HAMMER].damage,
          "a readied hammer added %d, expected %d",
          gg_attack_power(&g, g.player) - bare, GG_ITEM[GG_ITEM_HAMMER].damage);

    const int unguarded = gg_guard_power(&g, g.player);
    CHECK(gg_pack_add(&g, GG_ITEM_SHIELD, 1) == 1, "could not take a shield");
    g.pack_cursor = gg_pack_find(&g, GG_ITEM_SHIELD);
    g.mode = GG_MODE_PACK;
    gg_game_act(&g, GG_ACT_EQUIP);
    g.mode = GG_MODE_PLAY;

    CHECK(gg_guard_power(&g, g.player) == unguarded + GG_ITEM[GG_ITEM_SHIELD].guard,
          "a shield turned aside %d, expected %d",
          gg_guard_power(&g, g.player) - unguarded, GG_ITEM[GG_ITEM_SHIELD].guard);

    // Both at once: a weapon and armour occupy different slots, so readying
    // one must not put the other away.
    CHECK(gg_attack_power(&g, g.player) > bare,
          "readying the shield disarmed the hammer");

    gg_game_free(&g);
}

static void a_thrown_stone_reaches_across_the_room_and_lands_there(void) {
    gg_game g;
    int cx = 0, cy = 0;
    CHECK(set_up_encounter(&g, 9, &cx, &cy), "setup failed");

    const int foe = gg_spawn_named(&g, "BRIGAND", cx + 4, cy);
    CHECK(foe >= 0, "no brigand");
    g.actor[foe].hp = g.actor[foe].hp_max = 90;     // survives the whole test

    // Bare-handed, four tiles is out of reach.
    CHECK(gg_reach(&g, g.player) == 1, "empty hands reach further than a tile");
    CHECK(!gg_throw_at(&g, g.player, g.actor[foe].x, g.actor[foe].y),
          "something was thrown with nothing in hand");

    CHECK(gg_pack_add(&g, GG_ITEM_STONE, 3) == 3, "could not take stones");
    g.pack_cursor = gg_pack_find(&g, GG_ITEM_STONE);
    g.mode = GG_MODE_PACK;
    gg_game_act(&g, GG_ACT_EQUIP);
    g.mode = GG_MODE_PLAY;
    CHECK(gg_reach(&g, g.player) == GG_ITEM[GG_ITEM_STONE].reach,
          "a readied stone reaches %d, expected %d", gg_reach(&g, g.player),
          GG_ITEM[GG_ITEM_STONE].reach);

    const int carried = gg_pack_count(&g, GG_ITEM_STONE);
    CHECK(gg_throw_at(&g, g.player, g.actor[foe].x, g.actor[foe].y),
          "the stone was not thrown");
    CHECK(gg_pack_count(&g, GG_ITEM_STONE) == carried - 1,
          "throwing did not use up a stone");

    // It lies where it was thrown, so a fight is worth walking back across.
    const int where = gg_ground_at(&g.map, g.actor[foe].x, g.actor[foe].y);
    CHECK(where >= 0, "the stone vanished instead of landing");
    if (where >= 0)
        CHECK(g.map.ground[where].kind == GG_ITEM_STONE,
              "something other than the stone landed there");

    // Out of range is refused rather than stretched to.
    g.actor[foe].x = (int16_t)(cx + 9);
    CHECK(!gg_throw_at(&g, g.player, g.actor[foe].x, g.actor[foe].y),
          "a stone was thrown further than it reaches");

    gg_game_free(&g);
}

static void a_wall_stops_a_stone(void) {
    gg_game g;
    int cx = 0, cy = 0;
    CHECK(set_up_encounter(&g, 10, &cx, &cy), "setup failed");

    const int foe = gg_spawn_named(&g, "BRIGAND", cx + 3, cy);
    CHECK(foe >= 0, "no brigand");
    CHECK(gg_line_of_sight(&g, cx, cy, cx + 3, cy), "open ground blocked sight");

    // Ready the stone first. Readying costs a turn, and a turn is one the
    // brigand also gets - doing this after placing it moved it off the tile
    // the wall was built to hide, which is how this test first failed.
    gg_pack_add(&g, GG_ITEM_STONE, 2);
    g.pack_cursor = gg_pack_find(&g, GG_ITEM_STONE);
    g.mode = GG_MODE_PACK;
    gg_game_act(&g, GG_ACT_EQUIP);
    g.mode = GG_MODE_PLAY;

    // Now the wall, and the brigand put back behind it.
    gg_cell *between = gg_map_at(&g.map, cx + 1, cy);
    CHECK(between != nullptr, "no cell between");
    if (between) {
        between->terrain = GG_TILE_WALL_BRICK;
        between->flags |= GG_CELL_BLOCKED;
    }
    g.actor[foe].x = (int16_t)(cx + 3);
    g.actor[foe].y = (int16_t)cy;
    CHECK(!gg_line_of_sight(&g, cx, cy, cx + 3, cy), "a wall did not block sight");

    const int had = gg_pack_count(&g, GG_ITEM_STONE);
    CHECK(!gg_throw_at(&g, g.player, g.actor[foe].x, g.actor[foe].y),
          "a stone was thrown through a wall");
    CHECK(gg_pack_count(&g, GG_ITEM_STONE) == had,
          "a refused throw still used up a stone");

    gg_game_free(&g);
}

static void the_quick_strike_before_the_slow_and_more_often(void) {
    gg_game g;
    int cx = 0, cy = 0;
    CHECK(set_up_encounter(&g, 11, &cx, &cy), "setup failed");

    // An outlaw is the quick one and a brigand the slow one; that is the whole
    // observable difference initiative makes, so it is what gets checked.
    const int far_off = GG_NOTICE_RANGE + 3;
    CHECK(gg_spawn_named(&g, "OUTLAW", cx + far_off, cy) >= 0, "no outlaw");
    CHECK(gg_spawn_named(&g, "BRIGAND", cx - far_off, cy) >= 0, "no brigand");

    const int outlaw = g.actors - 2, brigand = g.actors - 1;
    CHECK(g.actor[outlaw].speed > g.actor[brigand].speed,
          "the outlaw is not the quicker of the two");

    // Neither has noticed anything yet, so neither has moved.
    const int ox = g.actor[outlaw].x, bx = g.actor[brigand].x;
    gg_game_act(&g, GG_ACT_WAIT);
    CHECK(g.actor[outlaw].x == ox && g.actor[brigand].x == bx,
          "something charged from beyond where it could see");

    // Bring both into range, the same distance out, and let them come.
    g.actor[outlaw].x = (int16_t)(cx + GG_NOTICE_RANGE - 1);
    g.actor[brigand].x = (int16_t)(cx - (GG_NOTICE_RANGE - 1));
    const int o_start = gg_dist_cheb(cx, cy, g.actor[outlaw].x, g.actor[outlaw].y);
    const int b_start = gg_dist_cheb(cx, cy, g.actor[brigand].x, g.actor[brigand].y);
    CHECK(o_start == b_start, "the two did not start the same distance away");

    for (int i = 0; i < 3; i++) gg_game_act(&g, GG_ACT_WAIT);

    const gg_actor *p = gg_player_const(&g);
    const int o_now = gg_dist_cheb(p->x, p->y, g.actor[outlaw].x, g.actor[outlaw].y);
    const int b_now = gg_dist_cheb(p->x, p->y, g.actor[brigand].x, g.actor[brigand].y);
    CHECK(o_now < b_now,
          "after three turns the quick one is %d away and the slow one %d - "
          "speed bought nothing", o_now, b_now);

    gg_game_free(&g);
}

static void what_falls_leaves_what_it_carried(void) {
    gg_game g;
    int cx = 0, cy = 0;
    CHECK(set_up_encounter(&g, 12, &cx, &cy), "setup failed");

    const int foe = gg_spawn_named(&g, "BRIGAND", cx + 1, cy);
    CHECK(foe >= 0, "no brigand");
    const int fx = g.actor[foe].x, fy = g.actor[foe].y;

    // What it carries comes out of the bestiary now, so that is what this
    // reads: the first line of its table is the one it always drops.
    const gg_beast *b = gg_bestiary_at(g.actor[foe].beast);
    CHECK(b != nullptr, "the brigand came from no row of the bestiary");
    CHECK(b && b->loots > 0, "a brigand carries nothing worth taking");
    if (!b || b->loots == 0) { gg_game_free(&g); return; }
    const uint8_t kind = b->loot[0].kind;
    CHECK(b->loot[0].chance == 100,
          "this test wants a certainty, and that line drops %u%% of the time",
          b->loot[0].chance);

    CHECK(gg_ground_at(&g.map, fx, fy) < 0, "something was already lying there");

    g.actor[foe].hp = 1;
    gg_strike(&g, g.player, foe);
    // One blow always takes at least one off, so one hit point is always fatal
    // - but the dice may still miss, so swing until it lands.
    for (int i = 0; i < 60 && g.actor[foe].active; i++) {
        g.actor[foe].hp = 1;
        gg_strike(&g, g.player, foe);
    }
    CHECK(!g.actor[foe].active, "the brigand would not fall");

    const int loot = gg_ground_at(&g.map, fx, fy);
    CHECK(loot >= 0, "the brigand left nothing behind");
    // Its table may have dropped more than one kind, so the certain one has to
    // be somewhere on the tile rather than necessarily first.
    bool found = false;
    int total = 0;
    for (int i = 0; i < g.map.grounds; i++)
        if (g.map.ground[i].x == fx && g.map.ground[i].y == fy) {
            if (g.map.ground[i].kind == kind) {
                found = true;
                total = g.map.ground[i].count;
            }
        }
    CHECK(found, "the brigand did not leave the thing it always leaves");
    CHECK(total >= b->loot[0].least && total <= b->loot[0].most,
          "it left %d, which is outside the %u to %u its table allows",
          total, b->loot[0].least, b->loot[0].most);

    // And what it left can be picked up like anything else.
    gg_player(&g)->x = (int16_t)fx;
    gg_player(&g)->y = (int16_t)fy;
    gg_game_act(&g, GG_ACT_GET);
    CHECK(gg_pack_count(&g, (gg_item_id)kind) >= total,
          "the loot could not be picked up");

    gg_game_free(&g);
}

static void a_townsperson_is_never_caught_in_a_fight(void) {
    gg_game g;
    int cx = 0, cy = 0;
    CHECK(set_up_encounter(&g, 13, &cx, &cy), "setup failed");

    // Somebody who is neither ours nor hostile, standing right beside it all.
    gg_actor *bystander = &g.actor[1];
    bystander->active = true;
    bystander->hostile = false;
    bystander->party = GG_NOT_IN_PARTY;
    bystander->x = (int16_t)(cx + 1);
    bystander->y = (int16_t)cy;
    bystander->hp = bystander->hp_max = 20;

    CHECK(!gg_at_odds(&g, g.player, 1), "a townsperson counts as an enemy");
    CHECK(gg_strike(&g, g.player, 1) == 0, "a townsperson was struck");
    CHECK(bystander->hp == 20, "a townsperson lost health in somebody's fight");

    const int foe = gg_spawn_named(&g, "BRIGAND", cx + 2, cy);
    CHECK(foe >= 0, "no brigand");
    CHECK(!gg_at_odds(&g, foe, 1), "a brigand counts a townsperson as an enemy");

    gg_game_free(&g);
}

static void the_avatar_dying_ends_the_game_rather_than_the_world(void) {
    gg_game g;
    int cx = 0, cy = 0;
    CHECK(set_up_encounter(&g, 14, &cx, &cy), "setup failed");

    const int foe = gg_spawn_named(&g, "BRIGAND", cx + 1, cy);
    CHECK(foe >= 0, "no brigand");

    gg_player(&g)->hp = 1;
    for (int i = 0; i < 60 && g.mode == GG_MODE_PLAY; i++) {
        gg_player(&g)->hp = 1;
        gg_strike(&g, foe, g.player);
    }
    CHECK(g.mode == GG_MODE_GAMEOVER, "the avatar survived being killed");

    // The Avatar's actor must still be there: the camera, the HUD and
    // gg_player all read through it, and a dead index is a crash, not an end.
    CHECK(g.actor[g.player].active, "the avatar was removed from the world");
    CHECK(gg_player_const(&g)->hp == 0, "a slain avatar has health left");

    // And the world stops turning for them.
    const uint32_t stopped = g.turn;
    gg_game_act(&g, GG_ACT_E);
    CHECK(g.turn == stopped, "the world went on turning after the end");

    gg_game_free(&g);
}

// ---------------------------------------------------------------------------
// Magic
// ---------------------------------------------------------------------------
static const char *write_spells(const char *text) {
    const char *path = gg_pref_file("test_spells.txt");
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    CHECK(io != nullptr, "could not write a spell file");
    if (io) {
        const size_t n = SDL_strlen(text);
        CHECK(SDL_WriteIO(io, text, n) == n, "short write on the spell file");
        SDL_CloseIO(io);
    }
    return path;
}

// The plan's own verification: a spell defined entirely in a data file, cast,
// with its effect and its reagent cost both observable.
static void a_spell_from_a_file_costs_reagents_and_does_what_it_says(void) {
    const char *path = write_spells(
        "rune MANI life\n"
        "spell MANI\n"
        "  name Heal\n"
        "  circle 1\n"
        "  costs GINSENG 2\n"
        "  effect heal 10\n"
        "  say Thy hurts close over.\n");
    CHECK(gg_magic_load(path), "the spell file did not load");
    CHECK(gg_magic_spells() == 1, "expected one spell, got %d", gg_magic_spells());
    CHECK(gg_magic_runes() == 1, "expected one rune, got %d", gg_magic_runes());

    gg_game g;
    CHECK(gg_game_new(&g, 41, "Mage"), "new game failed");
    gg_player(&g)->hp = 5;

    // Not known: the rune has not been learned, so the words cannot be spoken
    // however plainly they are written in the file.
    CHECK(!gg_knows(&g, "MANI"), "a new game already knows MANI");
    CHECK(!gg_spell_known(&g, 0), "an unlearned spell counts as known");
    CHECK(!gg_cast(&g, 0), "an unlearned spell was cast");
    CHECK(gg_player_const(&g)->hp == 5, "an unlearned spell healed anyway");

    // Known, but unaffordable. Both halves of the price are checked, and the
    // reagents must not be spent when the spell does not go off.
    gg_learn(&g, "MANI");
    CHECK(gg_spell_known(&g, 0), "the rune was learned but the spell is not known");
    CHECK(!gg_spell_afford(&g, 0), "afforded a spell with no reagents at all");
    CHECK(!gg_cast(&g, 0), "a spell was cast with nothing to pay for it");
    CHECK(gg_player_const(&g)->hp == 5, "an unpaid spell healed anyway");

    // One root is not two.
    CHECK(gg_pack_add(&g, GG_ITEM_GINSENG, 1) == 1, "could not take ginseng");
    CHECK(!gg_spell_afford(&g, 0), "one root paid for a spell that wants two");
    CHECK(!gg_cast(&g, 0), "a spell went off half paid");
    CHECK(gg_pack_count(&g, GG_ITEM_GINSENG) == 1,
          "a refused spell spent the reagents anyway");

    // Now it works, and both the effect and the price are visible.
    CHECK(gg_pack_add(&g, GG_ITEM_GINSENG, 1) == 1, "could not take more ginseng");
    const int before_hp = gg_player_const(&g)->hp;
    const int before_reagents = gg_pack_count(&g, GG_ITEM_GINSENG);
    CHECK(before_reagents == 2, "expected two roots, have %d", before_reagents);

    CHECK(gg_cast(&g, 0), "the spell would not be cast");
    CHECK(gg_player_const(&g)->hp == before_hp + 10,
          "healing took health from %d to %d, expected %d", before_hp,
          gg_player_const(&g)->hp, before_hp + 10);
    CHECK(gg_pack_count(&g, GG_ITEM_GINSENG) == 0,
          "casting left %d roots, expected none",
          gg_pack_count(&g, GG_ITEM_GINSENG));

    gg_game_free(&g);
    gg_magic_clear();
    SDL_RemovePath(path);
}

// The runic system: a spell is castable when its *words* are, and a phrase of
// two runes needs both.
static void a_phrase_needs_every_rune_in_it(void) {
    const char *path = write_spells(
        "rune VAS great\n"
        "rune MANI life\n"
        "spell MANI\n"
        "  name Heal\n"
        "  circle 1\n"
        "  costs GINSENG 1\n"
        "  effect heal 5\n"
        "spell VAS MANI\n"
        "  name Great Heal\n"
        "  circle 1\n"
        "  costs GINSENG 1\n"
        "  effect heal 20\n");
    CHECK(gg_magic_load(path), "the spell file did not load");

    gg_game g;
    CHECK(gg_game_new(&g, 42, "Runes"), "new game failed");

    gg_learn(&g, "MANI");
    CHECK(gg_spell_known(&g, 0), "MANI alone does not cast MANI");
    CHECK(!gg_spell_known(&g, 1), "VAS MANI was castable without VAS");

    gg_learn(&g, "VAS");
    CHECK(gg_spell_known(&g, 1), "both runes known but the phrase is not");

    // The book lists what is known and nothing else, so a player is never
    // shown a spell they cannot speak.
    gg_game h;
    CHECK(gg_game_new(&h, 42, "Empty"), "second new game failed");
    CHECK(gg_spell_next(&h, 0, 1) < 0,
          "the book offered a spell to somebody who knows no runes");
    gg_learn(&h, "MANI");
    CHECK(gg_spell_next(&h, 0, 1) == 0, "the book did not offer the known spell");

    gg_game_free(&h);
    gg_game_free(&g);
    gg_magic_clear();
    SDL_RemovePath(path);
}

static void a_spell_of_light_lasts_its_turns_and_then_goes_out(void) {
    const char *path = write_spells(
        "rune IN create\n"
        "rune LOR light\n"
        "spell IN LOR\n"
        "  name Light\n"
        "  circle 1\n"
        "  costs ASH 1\n"
        "  effect light 6 turns 5\n");
    CHECK(gg_magic_load(path), "the spell file did not load");

    gg_game g;
    CHECK(gg_game_new(&g, 43, "Lamp"), "new game failed");
    // Nothing in hand, so the only light is the one that gets cast.
    g.packn = 0;
    for (int s = 0; s < GG_SLOT_COUNT; s++) g.equipped[s] = -1;
    gg_learn(&g, "IN");
    gg_learn(&g, "LOR");
    gg_pack_add(&g, GG_ITEM_ASH, 1);

    CHECK(gg_light_radius(&g) == 1, "empty hands already light six tiles");
    CHECK(gg_cast(&g, 0), "the light would not be cast");
    CHECK(gg_light_radius(&g) == 6, "the spell lit %d tiles, expected 6",
          gg_light_radius(&g));

    // It burns down by the turn and then stops being the brightest thing.
    for (int i = 0; i < 4; i++) gg_game_act(&g, GG_ACT_WAIT);
    CHECK(gg_light_radius(&g) == 6, "the light went out early");
    gg_game_act(&g, GG_ACT_WAIT);
    CHECK(gg_light_radius(&g) == 1, "the light outlasted its turns");

    // And a torch in hand is not put out when a spell lapses.
    gg_pack_add(&g, GG_ITEM_ASH, 1);
    gg_pack_add(&g, GG_ITEM_TORCH, 1);
    g.pack_cursor = gg_pack_find(&g, GG_ITEM_TORCH);
    g.mode = GG_MODE_PACK;
    gg_game_act(&g, GG_ACT_EQUIP);
    g.mode = GG_MODE_PLAY;
    const int torch = GG_ITEM[GG_ITEM_TORCH].light;
    CHECK(gg_light_radius(&g) == torch, "the torch is not lighting anything");

    CHECK(gg_cast(&g, 0), "the light would not be cast a second time");
    CHECK(gg_light_radius(&g) == 6, "the spell did not outshine the torch");
    for (int i = 0; i < 6; i++) gg_game_act(&g, GG_ACT_WAIT);
    CHECK(gg_light_radius(&g) == torch,
          "the spell lapsing put the torch out too (%d)", gg_light_radius(&g));

    gg_game_free(&g);
    gg_magic_clear();
    SDL_RemovePath(path);
}

static void a_bolt_needs_something_to_aim_at_and_spends_nothing_without_one(void) {
    const char *path = write_spells(
        "rune IN create\n"
        "rune FLAM flame\n"
        "spell IN FLAM\n"
        "  name Fire Bolt\n"
        "  circle 1\n"
        "  costs ASH 1\n"
        "  effect harm 6 reach 4\n");
    CHECK(gg_magic_load(path), "the spell file did not load");

    gg_game g;
    int cx = 0, cy = 0;
    CHECK(set_up_encounter(&g, 44, &cx, &cy), "setup failed");
    gg_learn(&g, "IN");
    gg_learn(&g, "FLAM");
    gg_pack_add(&g, GG_ITEM_ASH, 3);

    // Nothing to aim at: refused, and nothing spent.
    const int had = gg_pack_count(&g, GG_ITEM_ASH);
    CHECK(!gg_cast(&g, 0), "a bolt was cast at nothing");
    CHECK(gg_pack_count(&g, GG_ITEM_ASH) == had,
          "a bolt with no target still burned the ash");

    // Out of reach is the same answer.
    const int far_foe = gg_spawn_named(&g, "BRIGAND", cx + 7, cy);
    CHECK(far_foe >= 0, "no distant brigand");
    g.actor[far_foe].hp = g.actor[far_foe].hp_max = 90;
    CHECK(!gg_cast(&g, 0), "a bolt reached further than it says it does");
    CHECK(gg_pack_count(&g, GG_ITEM_ASH) == had, "a refused bolt spent ash");

    // In reach: it lands, and it costs.
    const int near_foe = gg_spawn_named(&g, "BRIGAND", cx + 3, cy);
    CHECK(near_foe >= 0, "no near brigand");
    g.actor[near_foe].hp = g.actor[near_foe].hp_max = 90;

    const int before = g.actor[near_foe].hp;
    CHECK(gg_cast(&g, 0), "the bolt would not be cast");
    CHECK(g.actor[near_foe].hp == before - 6,
          "the bolt took %d, expected 6", before - g.actor[near_foe].hp);
    CHECK(gg_pack_count(&g, GG_ITEM_ASH) == had - 1, "the bolt cost no ash");

    gg_game_free(&g);
    gg_magic_clear();
    SDL_RemovePath(path);
}

// A spell file that does not parse must load nothing at all, the same rule the
// dialogue book follows and for the same reason.
static void a_spell_file_that_does_not_parse_loads_nothing(void) {
    static const char *const BAD[] = {
        "spell MANI\n  name Heal\n",                       // rune never declared
        "rune MANI life\nname Heal\n",                     // before any spell
        "rune MANI life\nspell MANI\n  circle 1\n"
            "  effect heal 4\n",                           // no name
        "rune MANI life\nspell MANI\n  name Heal\n",       // does nothing
        "rune MANI life\nspell MANI\n  name Heal\n"
            "  effect heal 0\n",                           // does nothing, loudly
        "rune MANI life\nspell MANI\n  name Heal\n"
            "  effect wibble 4\n",                         // no such effect
        "rune IN create\nspell IN\n  name Light\n"
            "  effect light 4\n",                          // a light with no time
        "rune IN create\nspell IN\n  name Bolt\n"
            "  effect harm 4\n",                           // harm with no reach
        "rune MANI life\nspell MANI\n  name Heal\n"
            "  costs UNOBTAINIUM 1\n  effect heal 4\n",    // no such reagent
        "rune MANI life\nspell MANI\n  name Heal\n"
            "  circle 99\n  effect heal 4\n",              // no such circle
    };
    for (size_t i = 0; i < GG_COUNTOF(BAD); i++) {
        const char *path = write_spells(BAD[i]);
        CHECK(!gg_magic_load(path), "bad spell file %zu loaded anyway", i);
        CHECK(gg_magic_spells() == 0,
              "bad spell file %zu left %d spells behind", i, gg_magic_spells());
        SDL_RemovePath(path);
    }
    CHECK(!gg_magic_load(gg_pref_file("test_no_such_spells.txt")),
          "a missing spell file reported success");
}

// The shipped book, and the runes that unlock it: every rune a spell needs has
// to be one somebody in the vale will actually teach, or the spell is written
// for nobody.
static void every_spell_in_the_vale_can_be_learned_from_somebody(void) {
    CHECK(gg_dialogue_load(gg_asset_path("dialogue.txt")), "no dialogue");
    CHECK(gg_magic_load(gg_asset_path("spells.txt")), "no spells");
    CHECK(gg_magic_spells() > 0, "the vale has no spells");

    // Collect every word anybody teaches, plus the two everyone starts with.
    char taught[GG_KNOWN_MAX][GG_WORD_MAX];
    int n = 0;
    SDL_strlcpy(taught[n++], GG_WORD_NAME, GG_WORD_MAX);
    SDL_strlcpy(taught[n++], GG_WORD_JOB, GG_WORD_MAX);

    static const char *const WHO[] = {
        "Iolo", "Shamino", "Nell", "Dupre", "Katrina", "Nystul", "Gwenno",
        "Chuckles",
    };
    for (size_t w = 0; w < GG_COUNTOF(WHO); w++) {
        const gg_speaker *s = gg_dialogue_find(WHO[w]);
        if (!s) continue;
        for (int t = 0; t < s->topics; t++) {
            if (!s->topic[t].teach[0]) continue;
            bool have = false;
            for (int i = 0; i < n; i++)
                if (SDL_strcasecmp(taught[i], s->topic[t].teach) == 0) have = true;
            if (!have && n < GG_KNOWN_MAX)
                SDL_strlcpy(taught[n++], s->topic[t].teach, GG_WORD_MAX);
        }
    }

    for (int i = 0; i < gg_magic_spells(); i++) {
        const gg_spell *sp = gg_magic_spell(i);
        for (int r = 0; r < sp->runes; r++) {
            bool teachable = false;
            for (int k = 0; k < n; k++)
                if (SDL_strcasecmp(taught[k], sp->rune[r]) == 0) teachable = true;
            CHECK(teachable,
                  "'%s' needs the rune %s, which nobody in the vale teaches",
                  sp->name, sp->rune[r]);
        }
        // And every reagent it wants has to be a thing that exists to be found.
        for (int k = 0; k < sp->reagents; k++)
            CHECK(sp->reagent[k] < GG_ITEM_COUNT,
                  "'%s' wants a reagent that is not an item", sp->name);
    }

    // Every rune declared should mean something and be used by some spell -
    // a rune nobody casts with is a word taught for nothing.
    for (int r = 0; r < gg_magic_runes(); r++) {
        const gg_rune *ru = gg_magic_rune(r);
        CHECK(ru->meaning[0] != '\0', "the rune %s means nothing", ru->word);
        bool used = false;
        for (int i = 0; i < gg_magic_spells() && !used; i++) {
            const gg_spell *sp = gg_magic_spell(i);
            for (int k = 0; k < sp->runes; k++)
                if (SDL_strcasecmp(sp->rune[k], ru->word) == 0) used = true;
        }
        CHECK(used, "the rune %s is in no spell at all", ru->word);
    }

    gg_magic_clear();
    gg_dialogue_clear();
}

static void a_spell_of_light_survives_a_save(void) {
    const char *who = "Lightbearer";
    wipe_saves(who);

    const char *path = write_spells(
        "rune IN create\nrune LOR light\n"
        "spell IN LOR\n  name Light\n  circle 1\n  costs ASH 1\n"
        "  effect light 6 turns 40\n");
    CHECK(gg_magic_load(path), "the spell file did not load");

    gg_game a;
    CHECK(gg_game_new(&a, 45, who), "new game failed");
    a.packn = 0;
    for (int s = 0; s < GG_SLOT_COUNT; s++) a.equipped[s] = -1;
    gg_learn(&a, "IN");
    gg_learn(&a, "LOR");
    gg_pack_add(&a, GG_ITEM_ASH, 1);
    CHECK(gg_cast(&a, 0), "the light would not be cast");
    CHECK(a.light_turns > 0, "the light is not burning");

    CHECK(gg_save_write(&a, save_base(), who), "save failed");
    gg_game b;
    SDL_zero(b);
    CHECK(gg_save_read(&b, save_base(), who), "load failed");

    CHECK(b.light_turns == a.light_turns, "the light came back with %d turns, "
          "expected %d", b.light_turns, a.light_turns);
    CHECK(gg_light_radius(&b) == gg_light_radius(&a),
          "the resumed game is lit differently");
    CHECK(gg_knows(&b, "LOR"), "the runes were forgotten");

    gg_game_free(&b);
    gg_game_free(&a);
    gg_magic_clear();
    SDL_RemovePath(path);
    wipe_saves(who);
}

// ---------------------------------------------------------------------------
// The bestiary
// ---------------------------------------------------------------------------
static const char *write_bestiary(const char *text) {
    const char *path = gg_pref_file("test_bestiary.txt");
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    CHECK(io != nullptr, "could not write a bestiary");
    if (io) {
        const size_t n = SDL_strlen(text);
        CHECK(SDL_WriteIO(io, text, n) == n, "short write on the bestiary");
        SDL_CloseIO(io);
    }
    return path;
}

// Puts the shipped bestiary back, so a test that loaded its own does not leave
// every test after it fighting something that no longer exists.
static void restore_bestiary(void) {
    CHECK(gg_bestiary_load(gg_asset_path("bestiary.txt")),
          "could not put the shipped bestiary back");
}

// The plan's own verification: a creature added with no code change. Every
// number below comes out of the file and is then observed in the world.
static void a_creature_can_be_added_in_a_file_alone(void) {
    const char *path = write_bestiary(
        "creature WOLF\n"
        "  name a lean wolf\n"
        "  art BRIGAND\n"
        "  health 23\n"
        "  level 2\n"
        "  speed 175\n"
        "  damage 4\n"
        "  guard 1\n"
        "  reach 1\n"
        "  notice 11\n"
        "  flees 5\n"
        "  loot BREAD 2 2 100\n"
        "  haunts 3\n");
    CHECK(gg_bestiary_load(path), "the bestiary did not load");
    CHECK(gg_bestiary_count() == 1, "expected one creature, got %d",
          gg_bestiary_count());

    const int which = gg_bestiary_find("WOLF");
    CHECK(which >= 0, "the wolf is not in the bestiary");

    gg_game g;
    int cx = 0, cy = 0;
    CHECK(set_up_encounter(&g, 51, &cx, &cy), "setup failed");

    const int who = gg_spawn_named(&g, "WOLF", cx + 2, cy);
    CHECK(who >= 0, "the wolf would not be placed");
    if (who < 0) { gg_game_free(&g); restore_bestiary(); return; }

    // Every one of these is a number in the file and nowhere in C.
    const gg_actor *a = &g.actor[who];
    CHECK(SDL_strcmp(a->name, "a lean wolf") == 0,
          "it is called '%s', not what the file says", a->name);
    CHECK(a->hp == 23 && a->hp_max == 23, "it has %d health, expected 23", a->hp);
    CHECK(a->level == 2, "it is level %u, expected 2", a->level);
    CHECK(a->speed == 175, "it moves at %u, expected 175", a->speed);
    CHECK(a->damage == 4, "it deals %u, expected 4", a->damage);
    CHECK(a->guard == 1, "it turns aside %u, expected 1", a->guard);
    CHECK(a->notice == 11, "it notices at %u, expected 11", a->notice);
    CHECK(a->flees == 5, "it flees at %d, expected 5", a->flees);
    CHECK(a->hostile, "it is not hostile");
    CHECK(a->art == GG_ACTOR_BRIGAND, "it is wearing the wrong sprite");

    // Its stats reach the rules, not just the struct.
    CHECK(gg_attack_power(&g, who) == 4, "the rules give it %d damage",
          gg_attack_power(&g, who));
    CHECK(gg_guard_power(&g, who) == 1, "the rules give it %d guard",
          gg_guard_power(&g, who));
    CHECK(gg_at_odds(&g, g.player, who), "the wolf is not an enemy");

    // And its loot table is the one in the file.
    const int fx = a->x, fy = a->y;
    for (int i = 0; i < 80 && g.actor[who].active; i++) {
        g.actor[who].hp = 1;
        gg_strike(&g, g.player, who);
    }
    CHECK(!g.actor[who].active, "the wolf would not fall");
    const int loot = gg_ground_at(&g.map, fx, fy);
    CHECK(loot >= 0, "the wolf left nothing");
    if (loot >= 0) {
        CHECK(g.map.ground[loot].kind == GG_ITEM_BREAD,
              "it left the wrong thing");
        CHECK(g.map.ground[loot].count == 2, "it left %u, expected 2",
              g.map.ground[loot].count);
    }

    gg_game_free(&g);

    // And the generator places `haunts` of it without being told what it is.
    gg_game w;
    CHECK(gg_game_new(&w, 52, "Wilds"), "new game failed");
    int wolves = 0;
    for (int i = 0; i < w.actors; i++)
        if (w.actor[i].active && w.actor[i].hostile &&
            SDL_strcmp(w.actor[i].name, "a lean wolf") == 0) wolves++;
    CHECK(wolves > 0, "the generator placed none of the only creature there is");
    CHECK(wolves <= 3, "the generator placed %d, more than the file allows",
          wolves);
    gg_game_free(&w);

    restore_bestiary();
    SDL_RemovePath(path);
}

// Behaviour out of the file: a hurt creature would rather be elsewhere.
static void a_creature_flees_when_it_is_hurt_enough(void) {
    const char *path = write_bestiary(
        "creature COWARD\n"
        "  name a coward\n"
        "  art BRIGAND\n"
        "  health 20\n"
        "  damage 2\n"
        "  notice 12\n"
        "  flees 10\n"
        "creature DIEHARD\n"
        "  name a diehard\n"
        "  art BRIGAND\n"
        "  health 20\n"
        "  damage 2\n"
        "  notice 12\n"
        "  flees 0\n");
    CHECK(gg_bestiary_load(path), "the bestiary did not load");

    gg_game g;
    int cx = 0, cy = 0;
    CHECK(set_up_encounter(&g, 53, &cx, &cy), "setup failed");

    const int coward = gg_spawn_named(&g, "COWARD", cx + 3, cy);
    const int diehard = gg_spawn_named(&g, "DIEHARD", cx - 3, cy);
    CHECK(coward >= 0 && diehard >= 0, "the pair would not be placed");
    if (coward < 0 || diehard < 0) { gg_game_free(&g); restore_bestiary(); return; }

    // Hale, both close in.
    for (int i = 0; i < 2; i++) gg_game_act(&g, GG_ACT_WAIT);
    const gg_actor *p = gg_player_const(&g);
    CHECK(gg_dist_cheb(p->x, p->y, g.actor[coward].x, g.actor[coward].y) < 3,
          "the coward did not close while it was hale");

    // Hurt the coward past its limit and it turns round.
    g.actor[coward].hp = 5;
    const int before = gg_dist_cheb(p->x, p->y,
                                    g.actor[coward].x, g.actor[coward].y);
    for (int i = 0; i < 3; i++) gg_game_act(&g, GG_ACT_WAIT);
    const int after = gg_dist_cheb(p->x, p->y,
                                   g.actor[coward].x, g.actor[coward].y);
    CHECK(after > before, "a coward at 5 of 20 health closed from %d to %d",
          before, after);

    // The one whose file says it never flees does not, at the same health.
    g.actor[diehard].hp = 5;
    const int d_before = gg_dist_cheb(p->x, p->y,
                                      g.actor[diehard].x, g.actor[diehard].y);
    for (int i = 0; i < 3; i++) gg_game_act(&g, GG_ACT_WAIT);
    const int d_after = gg_dist_cheb(p->x, p->y,
                                     g.actor[diehard].x, g.actor[diehard].y);
    CHECK(d_after <= d_before,
          "a creature whose file says it never flees ran anyway (%d to %d)",
          d_before, d_after);

    gg_game_free(&g);
    restore_bestiary();
    SDL_RemovePath(path);
}

// How near you must come is the creature's own business, out of its file - not
// one number shared by everything in the world.
static void a_creature_notices_at_the_distance_its_file_says(void) {
    const char *path = write_bestiary(
        "creature WATCHFUL\n"
        "  name a watchful thing\n"
        "  art BRIGAND\n"
        "  health 20\n"
        "  damage 2\n"
        "  notice 12\n"
        "creature DOZY\n"
        "  name a dozy thing\n"
        "  art BRIGAND\n"
        "  health 20\n"
        "  damage 2\n"
        "  notice 3\n");
    CHECK(gg_bestiary_load(path), "the bestiary did not load");

    gg_game g;
    int cx = 0, cy = 0;
    CHECK(set_up_encounter(&g, 56, &cx, &cy), "setup failed");

    // Both the same distance out - far enough that the watchful one cares and
    // the dozy one does not, which is only true if each reads its own number.
    const int out = 9;
    const int watchful = gg_spawn_named(&g, "WATCHFUL", cx + out, cy);
    const int dozy = gg_spawn_named(&g, "DOZY", cx - out, cy);
    CHECK(watchful >= 0 && dozy >= 0, "the pair would not be placed");
    if (watchful < 0 || dozy < 0) { gg_game_free(&g); restore_bestiary(); return; }
    CHECK(out < g.actor[watchful].notice && out > g.actor[dozy].notice,
          "this test wants a distance between the two notice ranges");

    const int w_before = gg_dist_cheb(cx, cy, g.actor[watchful].x,
                                      g.actor[watchful].y);
    const int d_before = gg_dist_cheb(cx, cy, g.actor[dozy].x, g.actor[dozy].y);

    for (int i = 0; i < 3; i++) gg_game_act(&g, GG_ACT_WAIT);

    const gg_actor *p = gg_player_const(&g);
    const int w_after = gg_dist_cheb(p->x, p->y, g.actor[watchful].x,
                                     g.actor[watchful].y);
    const int d_after = gg_dist_cheb(p->x, p->y, g.actor[dozy].x, g.actor[dozy].y);

    CHECK(w_after < w_before,
          "the watchful one notices at %u and was %d away, and did not stir",
          g.actor[watchful].notice, w_before);
    CHECK(d_after == d_before,
          "the dozy one notices at %u and was %d away, but closed from %d to %d",
          g.actor[dozy].notice, d_before, d_before, d_after);

    gg_game_free(&g);
    restore_bestiary();
    SDL_RemovePath(path);
}

// A creature with reach fights at a distance without anything in its hands.
static void a_creature_with_reach_strikes_from_where_it_stands(void) {
    const char *path = write_bestiary(
        "creature SLINGER\n"
        "  name a slinger\n"
        "  art OUTLAW\n"
        "  health 12\n"
        "  damage 2\n"
        "  reach 4\n"
        "  notice 10\n"
        "  loot STONE 1 1 100\n");
    CHECK(gg_bestiary_load(path), "the bestiary did not load");

    gg_game g;
    int cx = 0, cy = 0;
    CHECK(set_up_encounter(&g, 54, &cx, &cy), "setup failed");

    const int who = gg_spawn_named(&g, "SLINGER", cx + 3, cy);
    CHECK(who >= 0, "the slinger would not be placed");
    if (who < 0) { gg_game_free(&g); restore_bestiary(); return; }

    CHECK(gg_reach(&g, who) == 4, "it reaches %d, expected 4", gg_reach(&g, who));

    // Three tiles away and it can already hurt you - which is the whole
    // difference a reach above one makes.
    const int before = gg_player_const(&g)->hp;
    for (int i = 0; i < 12; i++) {
        gg_game_act(&g, GG_ACT_WAIT);
        if (gg_player_const(&g)->hp < before) break;
    }
    CHECK(gg_player_const(&g)->hp < before,
          "a slinger three tiles away never landed anything");

    gg_game_free(&g);
    restore_bestiary();
    SDL_RemovePath(path);
}

static void a_bestiary_that_does_not_parse_loads_nothing(void) {
    static const char *const BAD[] = {
        "name a thing\n",                                    // before any creature
        "creature X\n  name a thing\n  art NOSUCHART\n",     // no such art
        "creature X\n  art BRIGAND\n  damage 1\n",           // no name
        "creature X\n  name a thing\n  art BRIGAND\n",       // does no damage
        "creature X\n  name a thing\n  damage 1\n  wibble 3\n",  // unknown word
        "creature X\n  name a thing\n  damage 1\n"
            "  loot NOSUCHITEM 1 1 100\n",                   // no such loot
        "creature X\n  name a thing\n  damage 1\n"
            "  loot GOLD 5 2 100\n",                         // most below least
        "creature X\n  name a thing\n  damage 1\n"
            "  loot GOLD 1 2 400\n",                         // impossible chance
        "creature X\n  name a thing\n  damage 1\n  health 0\n",  // no health
        "creature X\n  name a thing\n  damage 1\n  speed nine\n", // not a number
    };
    for (size_t i = 0; i < GG_COUNTOF(BAD); i++) {
        const char *path = write_bestiary(BAD[i]);
        CHECK(!gg_bestiary_load(path), "bad bestiary %zu loaded anyway", i);
        CHECK(gg_bestiary_count() == 0,
              "bad bestiary %zu left %d creatures behind", i,
              gg_bestiary_count());
        SDL_RemovePath(path);
    }
    CHECK(!gg_bestiary_load(gg_pref_file("test_no_such_bestiary.txt")),
          "a missing bestiary reported success");
    restore_bestiary();
}

// The shipped bestiary has to be one the world can actually use.
static void the_vale_is_stocked_with_creatures_that_work(void) {
    CHECK(gg_bestiary_load(gg_asset_path("bestiary.txt")), "no bestiary");
    CHECK(gg_bestiary_count() > 0, "the vale holds nothing at all");

    int haunting = 0;
    for (int i = 0; i < gg_bestiary_count(); i++) {
        const gg_beast *b = gg_bestiary_at(i);
        CHECK(b->name[0] != '\0', "creature %d has no name", i);
        CHECK(b->art < GG_ACTOR_COUNT, "%s wears art that does not exist", b->id);
        CHECK(b->health > 0, "%s has no health", b->id);
        CHECK(b->damage > 0, "%s can hurt nobody", b->id);
        CHECK(b->speed > 0, "%s can never act", b->id);
        CHECK(b->reach >= 1, "%s cannot reach its own tile", b->id);
        CHECK(b->notice >= 1, "%s never notices anything", b->id);
        CHECK(b->flees < b->health,
              "%s flees at %d of %d health, so it flees from the start",
              b->id, b->flees, b->health);
        for (int k = 0; k < b->loots; k++) {
            CHECK(b->loot[k].kind < GG_ITEM_COUNT, "%s drops a thing that is not "
                  "an item", b->id);
            CHECK(b->loot[k].least <= b->loot[k].most,
                  "%s drops between %u and %u, which is backwards", b->id,
                  b->loot[k].least, b->loot[k].most);
        }
        haunting += b->haunts;
    }
    CHECK(haunting > 0, "nothing in the bestiary ever appears in a map");

    // And a generated world actually holds some of them.
    gg_game g;
    CHECK(gg_game_new(&g, 55, "Stocked"), "new game failed");
    int foes = 0;
    for (int i = 0; i < g.actors; i++)
        if (g.actor[i].active && g.actor[i].hostile) foes++;
    CHECK(foes > 0, "a generated world holds nothing hostile at all");
    CHECK(foes <= haunting, "the world holds %d, more than the %d the file "
          "allows", foes, haunting);
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
    for (int i = 0; i < GG_ITEM_COUNT; i++) {
        CHECK(GG_ITEM[i].one != nullptr, "item %d has no singular name", i);
        CHECK(GG_ITEM[i].many != nullptr, "item %d has no plural name", i);
        CHECK(GG_ITEM[i].short_name != nullptr, "item %d has no short name", i);
    }
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

// The baker writes a prop's geometry into gg_ids.c and its atlas rectangle into
// gg_atlas.h, and nothing until now checked that the two agreed. They are
// generated in the same pass from the same declaration, so a disagreement means
// the pair on disk came from different runs - a stale generated file, which is
// exactly the failure that leaves a sprite drawn at the wrong size.
static void a_props_atlas_rect_matches_the_size_it_declares(void) {
    for (int i = 0; i < GG_PROP_COUNT; i++) {
        const gg_prop_size *s = &GG_PROP_SIZE[i];
        const gg_rect *r = &GG_PROP_RECT[i];

        CHECK(r->w == s->tiles_w * GG_TILE,
              "prop %d is %u tiles wide but its atlas rect is %d px",
              i, s->tiles_w, r->w);
        CHECK(r->h == s->tiles_h * GG_TILE,
              "prop %d is %u tiles tall but its atlas rect is %d px",
              i, s->tiles_h, r->h);
        CHECK(r->x >= 0 && r->y >= 0, "prop %d has a negative atlas origin", i);
    }
}

// ---------------------------------------------------------------------------
int main(void) {
    // The simulation must not need video; prove it by never initialising it.
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");

    // The world is content now. A test binary that loads none of it is testing
    // an engine with an empty world, which is not the engine that ships - so
    // the shipped bestiary is loaded here, exactly as the game loads it. Tests
    // that want their own creatures load over the top and clear afterwards.
    if (!gg_bestiary_load(gg_asset_path("bestiary.txt")))
        SDL_Log("gigantima: tests could not load the bestiary");

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

    RUN(the_menu_cursor_skips_disabled_rows_and_wraps);
    RUN(a_menu_with_nothing_choosable_chooses_nothing);
    RUN(settings_round_trip_and_clamp);
    RUN(the_title_screen_offers_continue_only_when_there_is_one);
    RUN(naming_a_journey_refuses_a_bad_or_taken_name);
    RUN(forgetting_a_journey_takes_two_steps);
    RUN(every_screen_can_be_left);
    RUN(the_options_page_cycles_its_values);
    RUN(every_screen_is_reachable_by_directions_alone);
    RUN(the_options_page_returns_where_it_came_from);
    RUN(a_value_can_be_nudged_both_ways_without_leaving_the_page);
    RUN(a_journey_can_be_named_with_directions_alone);
    RUN(the_pad_feeds_the_world_and_the_menus_separately);

    RUN(a_profile_name_cannot_steer_a_path);
    RUN(a_saved_game_resumes_exactly_where_it_was_left);
    RUN(a_resumed_game_can_still_be_talked_to);
    RUN(profiles_do_not_see_each_others_saves);
    RUN(a_save_that_is_not_ours_is_refused);
    RUN(the_profile_list_reports_what_was_saved);

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
    RUN(a_lamp_indoors_does_not_light_the_street);
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

    RUN(an_item_taken_off_the_ground_is_in_the_pack_and_gone_from_the_map);
    RUN(what_is_set_down_is_where_it_was_set_down);
    RUN(a_second_pile_on_one_tile_joins_the_first);
    RUN(taking_from_a_tile_clears_everything_on_it);
    RUN(a_pack_will_not_hold_more_than_it_can_carry);
    RUN(eating_costs_the_food_and_mends_the_eater);
    RUN(what_is_held_stays_held_through_the_pack_shifting);
    RUN(only_things_meant_to_be_held_can_be_held);
    RUN(every_item_is_one_the_rules_can_handle);

    RUN(a_topic_unlocks_only_after_the_word_is_learned);
    RUN(a_word_learned_from_one_person_opens_another);
    RUN(what_was_learned_survives_a_save);
    RUN(a_dialogue_file_that_does_not_parse_loads_nothing);
    RUN(synonyms_ask_the_same_topic_and_show_one_label);
    RUN(the_vale_has_a_book_and_every_word_in_it_is_reachable);

    RUN(a_companion_follows_through_a_door_without_blocking);
    RUN(a_companion_in_the_way_swaps_places);
    RUN(the_line_closes_when_somebody_leaves_it);
    RUN(a_companion_is_recruited_by_a_topic_in_the_book);
    RUN(a_party_survives_a_save_in_order);

    RUN(a_scripted_encounter_resolves_the_same_way_every_time);
    RUN(a_blow_lands_or_misses_by_the_dice_and_never_for_nothing);
    RUN(armour_turns_blows_aside_and_a_weapon_drives_them_home);
    RUN(a_thrown_stone_reaches_across_the_room_and_lands_there);
    RUN(a_wall_stops_a_stone);
    RUN(the_quick_strike_before_the_slow_and_more_often);
    RUN(what_falls_leaves_what_it_carried);
    RUN(a_townsperson_is_never_caught_in_a_fight);
    RUN(the_avatar_dying_ends_the_game_rather_than_the_world);

    RUN(a_spell_from_a_file_costs_reagents_and_does_what_it_says);
    RUN(a_phrase_needs_every_rune_in_it);
    RUN(a_spell_of_light_lasts_its_turns_and_then_goes_out);
    RUN(a_bolt_needs_something_to_aim_at_and_spends_nothing_without_one);
    RUN(a_spell_file_that_does_not_parse_loads_nothing);
    RUN(every_spell_in_the_vale_can_be_learned_from_somebody);
    RUN(a_spell_of_light_survives_a_save);

    RUN(a_creature_can_be_added_in_a_file_alone);
    RUN(a_creature_flees_when_it_is_hurt_enough);
    RUN(a_creature_notices_at_the_distance_its_file_says);
    RUN(a_creature_with_reach_strikes_from_where_it_stands);
    RUN(a_bestiary_that_does_not_parse_loads_nothing);
    RUN(the_vale_is_stocked_with_creatures_that_work);

    RUN(every_terrain_and_item_has_a_name);
    RUN(every_prop_has_a_plausible_footprint);
    RUN(a_props_atlas_rect_matches_the_size_it_declares);

    SDL_Log("%s", "");
    SDL_Log("%d checks, %d failures", g_checks, g_failures);
    SDL_Quit();
    return g_failures ? 1 : 0;
}
