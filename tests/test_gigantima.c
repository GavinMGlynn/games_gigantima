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
#include "core/gg_replay.h"
#include "core/gg_maptext.h"
#include "core/gg_dialogue.h"
#include "core/gg_combat.h"
#include "core/gg_magic.h"
#include "core/gg_bestiary.h"
#include "core/gg_quest.h"
#include "audio/gg_audio.h"
#include "editor/gg_edit.h"
#include "ui/gg_menu.h"
#include "ui/gg_screens.h"
#include "gfx/gg_font.h"
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
// Puts the shipped bestiary back, so a test that loaded its own does not leave
// every test after it fighting something that no longer exists.
static void restore_bestiary(void) {
    CHECK(gg_bestiary_load(gg_asset_path("bestiary.txt")),
          "could not put the shipped bestiary back");
}

// The same for the book of people. It is the roll of who lives in a generated
// town as well as what they say, so a test that leaves it cleared leaves every
// test after it in an empty world.
static void restore_dialogue(void) {
    CHECK(gg_dialogue_load(gg_asset_path("dialogue.txt")),
          "could not put the shipped dialogue back");
}

// And the book of words, for the same reason: a test that leaves its own
// spells loaded leaves every test after it unable to cast anything real.
static void restore_spells(void) {
    CHECK(gg_magic_load(gg_asset_path("spells.txt")),
          "could not put the shipped spells back");
}

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

    // The sound rows are live now that there is sound. They were disabled
    // while there was none, on the grounds that a control doing nothing is
    // worse than one that says why - so this check is the inverse of the one
    // that stood here before, and it is the same rule.
    CHECK(s.menu.item[3].enabled && s.menu.item[4].enabled,
          "the sound rows are dead but there is sound to turn down");

    gg_menu_select(&s.menu, 3);
    const int was_music = set.music;
    gg_screens_choose(&s, base, &set, true);
    CHECK(set.music != was_music, "the music row did not turn anything");
    // Eleven positions, 0 to 10, so eleven turns in all - and one has already
    // been taken above.
    for (int i = 0; i < 10; i++) gg_screens_choose(&s, base, &set, true);
    CHECK(set.music == was_music,
          "eleven turns of an eleven-position control did not come back round "
          "(%d, was %d)", set.music, was_music);
    CHECK(set.music >= 0 && set.music <= 10, "music left its range: %d",
          set.music);

    // Silence has to be reachable, and it is the whole reason the cycle wraps
    // rather than stopping at one.
    while (set.music != 0) gg_screens_choose(&s, base, &set, true);
    CHECK(set.music == 0, "a player cannot turn the music off");

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

    // The keys page is one further in, behind Options - the only screen that
    // is not reachable from the title in one step.
    gg_screens_enter(&s, GG_SCREEN_OPTIONS, base, &set, &g, true);
    CHECK(menu_reach(&s, "Keys"),
          "the options page's 'Keys' row cannot be reached with directions");
    const gg_screen_result keys = gg_screens_choose(&s, base, &set, true);
    CHECK(keys.action == GG_ACTION_GO && keys.next == GG_SCREEN_KEYS,
          "'Keys' led to %d rather than the keys page", (int)keys.next);
    seen[GG_SCREEN_KEYS] = true;

    // And backing out of it goes to the page it came from rather than to the
    // title, the same rule Options itself follows.
    gg_screens_enter(&s, GG_SCREEN_KEYS, base, &set, &g, true);
    const gg_screen_result out = gg_screens_back(&s);
    CHECK(out.action == GG_ACTION_GO && out.next == GG_SCREEN_OPTIONS,
          "backing out of the keys page went to %d", (int)out.next);

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

// Every verb the game has, reached with a pad and nothing else.
//
// The claim being pinned is not "the pad works" but "a controller is not the
// poor relation": everything a player has to be able to do is reachable from
// the pad, either as a button of its own or through a panel the pad can open.
// Two of them were not, until the triggers were given a meaning - a pad could
// not pick anything up off the ground, which is enough on its own to make the
// storyline unfinishable.
static void every_verb_can_be_reached_with_a_pad(void) {
    gg_input in;
    SDL_zero(in);

    // What a button press looks like coming out of SDL.
    #define PRESS(which) do {                                                 \
        SDL_Event ev;                                                         \
        SDL_zero(ev);                                                         \
        ev.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;                              \
        ev.gbutton.button = (uint8_t)(which);                                 \
        CHECK(gg_input_event(&in, &ev), "the pad ignored button %d", (which)); \
    } while (0)

    #define PULL(which, howfar) do {                                          \
        SDL_Event ev;                                                         \
        SDL_zero(ev);                                                         \
        ev.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;                              \
        ev.gaxis.axis = (uint8_t)(which);                                     \
        ev.gaxis.value = (int16_t)(howfar);                                   \
        CHECK(gg_input_event(&in, &ev), "the pad ignored axis %d", (which));  \
    } while (0)

    static const struct { int button; gg_action verb; const char *what; } BUTTONS[] = {
        { SDL_GAMEPAD_BUTTON_SOUTH,          GG_ACT_TALK,  "talking to somebody" },
        { SDL_GAMEPAD_BUTTON_EAST,           GG_ACT_WAIT,  "waiting, and closing a panel" },
        { SDL_GAMEPAD_BUTTON_WEST,           GG_ACT_LOOK,  "looking at something" },
        { SDL_GAMEPAD_BUTTON_NORTH,          GG_ACT_OPEN,  "opening a door" },
        { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  GG_ACT_CAST,  "speaking a spell" },
        { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, GG_ACT_FIGHT, "striking" },
        { SDL_GAMEPAD_BUTTON_BACK,           GG_ACT_PACK,  "opening the pack" },
    };
    for (size_t i = 0; i < GG_COUNTOF(BUTTONS); i++) {
        PRESS(BUTTONS[i].button);
        const gg_action got = gg_input_take(&in);
        CHECK(got == BUTTONS[i].verb, "%s came out as %s", BUTTONS[i].what,
              gg_action_name(got));
    }

    // The triggers, which are axes and have to become edges.
    PULL(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 30000);
    CHECK(gg_input_take(&in) == GG_ACT_GET,
          "the left trigger does not pick anything up");
    PULL(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 30000);
    CHECK(gg_input_take(&in) == GG_ACT_JOURNAL,
          "the right trigger does not open the journal");

    // Held, not pumped: a finger resting past the threshold must not fire
    // again, and the trigger must fall well back before it can.
    PULL(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32000);
    CHECK(gg_input_take(&in) == GG_ACT_NONE, "a held trigger fired twice");
    PULL(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, GG_PAD_TRIGGER - 100);
    CHECK(gg_input_take(&in) == GG_ACT_NONE,
          "a trigger re-armed while it was still pulled");
    PULL(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0);
    PULL(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 30000);
    CHECK(gg_input_take(&in) == GG_ACT_GET, "a released trigger would not fire again");

    // A stick is not a trigger: it is sampled per tick, so an axis event for
    // one is not claimed here at all and must not latch a verb.
    {
        SDL_Event ev;
        SDL_zero(ev);
        ev.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
        ev.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
        ev.gaxis.value = 32000;
        CHECK(!gg_input_event(&in, &ev), "a stick was claimed as a button");
        CHECK(gg_input_take(&in) == GG_ACT_NONE, "the stick latched an action");
    }

    // Start, which is its own stream because it means something on every
    // screen.
    PRESS(SDL_GAMEPAD_BUTTON_START);
    CHECK(gg_input_take_pause(&in), "Start does not reach the pause menu");

    #undef PULL
    #undef PRESS

    // And the three the pack carries. They have no button of their own in the
    // world - the four face buttons are spoken for - so the pack gives its own
    // meanings to three of them, which is the same trick a console game plays.
    gg_game g;
    CHECK(gg_game_new(&g, 12, "Padded"), "new game failed");
    g.packn = 0;
    for (int sl = 0; sl < GG_SLOT_COUNT; sl++) g.equipped[sl] = -1;
    CHECK(gg_pack_add(&g, GG_ITEM_TORCH, 1) == 1, "could not take a torch");
    CHECK(gg_pack_add(&g, GG_ITEM_BREAD, 1) == 1, "could not take bread");

    gg_game_act(&g, GG_ACT_PACK);
    CHECK(g.mode == GG_MODE_PACK, "the pack would not open");

    g.pack_cursor = gg_pack_find(&g, GG_ITEM_TORCH);
    gg_game_act(&g, GG_ACT_OPEN);            // Y readies
    CHECK(g.equipped[GG_SLOT_LIGHT] >= 0, "Y does not ready a thing in the pack");

    g.pack_cursor = gg_pack_find(&g, GG_ITEM_BREAD);
    const int loaves = gg_pack_count(&g, GG_ITEM_BREAD);
    gg_player(&g)->hp = 1;                   // eating a full stomach is refused
    gg_game_act(&g, GG_ACT_TALK);            // A uses
    CHECK(gg_pack_count(&g, GG_ITEM_BREAD) < loaves, "A does not use a thing");

    g.pack_cursor = gg_pack_find(&g, GG_ITEM_TORCH);
    gg_game_act(&g, GG_ACT_LOOK);            // X sets down
    CHECK(gg_pack_find(&g, GG_ITEM_TORCH) < 0, "X does not set a thing down");

    gg_game_act(&g, GG_ACT_WAIT);            // B closes
    CHECK(g.mode == GG_MODE_PLAY, "B does not close the pack");

    gg_game_free(&g);
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

    // And every map walked in and left, which is a save's memory of everywhere
    // the Avatar is not standing.
    if (SDL_strcmp(a->here, b->here) != 0) { *why = "which map this is"; return false; }
    if (a->visiteds != b->visiteds) { *why = "how many maps are remembered"; return false; }
    for (int m = 0; m < a->visiteds; m++) {
        const gg_visited *x = &a->visited[m], *y = &b->visited[m];
        if (SDL_strcmp(x->leaf, y->leaf) != 0) { *why = "a remembered map"; return false; }
        if (x->map.grounds != y->map.grounds) {
            *why = "what lies on a remembered map's floor"; return false;
        }
        for (int i = 0; i < x->map.grounds; i++)
            if (x->map.ground[i].x != y->map.ground[i].x ||
                x->map.ground[i].y != y->map.ground[i].y ||
                x->map.ground[i].kind != y->map.ground[i].kind ||
                x->map.ground[i].count != y->map.ground[i].count) {
                *why = "a pile on a remembered map"; return false;
            }
        if (x->whos != y->whos) { *why = "who is left in a remembered map"; return false; }
        for (int i = 0; i < x->whos; i++)
            if (x->who[i].x != y->who[i].x || x->who[i].y != y->who[i].y ||
                x->who[i].hp != y->who[i].hp || x->who[i].def != y->who[i].def ||
                x->who[i].beast != y->who[i].beast ||
                SDL_strcmp(x->who[i].name, y->who[i].name) != 0) {
                *why = "somebody in a remembered map"; return false;
            }
        if (SDL_memcmp(x->map.cell, y->map.cell,
                       (size_t)x->map.w * (size_t)x->map.h * sizeof *x->map.cell) != 0) {
            *why = "a remembered map's cells"; return false;
        }
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
    restore_dialogue();
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
    restore_dialogue();
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
    restore_dialogue();
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
    restore_dialogue();
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

    restore_dialogue();
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
    restore_dialogue();
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
    restore_dialogue();
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
// Replay
// ---------------------------------------------------------------------------
// A session played twice: once recording, once from the recording. The
// simulation is integer-only and seeded and the world advances only inside
// gg_game_act, so the second run has to end on the same number as the first -
// and if it ever does not, the bug is that divergence and this is what finds
// it.
//
// The actions are drawn from a seeded RNG of the test's own rather than by
// hand: a hand-written list exercises what the author thought of, and this has
// to exercise whatever the world does.
static void a_recorded_session_replays_to_the_same_world(void) {
    const char *path = gg_pref_file("test_session.ggreplay");

    static const gg_action MENU[] = {
        GG_ACT_N, GG_ACT_S, GG_ACT_E, GG_ACT_W,
        GG_ACT_NE, GG_ACT_NW, GG_ACT_SE, GG_ACT_SW,
        GG_ACT_WAIT, GG_ACT_TALK, GG_ACT_LOOK, GG_ACT_OPEN,
        GG_ACT_GET, GG_ACT_FIGHT, GG_ACT_PACK, GG_ACT_USE,
        GG_ACT_EQUIP, GG_ACT_DROP, GG_ACT_JOURNAL, GG_ACT_CAST,
    };
    const int PLAYED = 400;

    // --- the session --------------------------------------------------------
    gg_game a;
    CHECK(gg_game_new(&a, 9001, "Recorder"), "new game failed");

    gg_recorder rec;
    CHECK(gg_record_begin(&rec, path, &a, nullptr), "could not start recording");

    gg_rng script;
    gg_rng_seed(&script, 31337);
    gg_action played[400];
    for (int i = 0; i < PLAYED; i++) {
        const gg_action act = MENU[gg_rand_belowi(&script, (int)GG_COUNTOF(MENU))];
        played[i] = act;
        gg_record_act(&rec, act);
        gg_game_act(&a, act);
        gg_game_animate(&a);
    }
    const uint64_t was = gg_record_end(&rec, &a);
    CHECK(was == gg_state_hash(&a), "the recorder wrote a hash of another world");

    // The world has to have actually gone somewhere, or this proves that two
    // untouched worlds are the same.
    CHECK(a.turn > 20, "the session only reached turn %u", a.turn);
    {
        gg_game fresh;
        CHECK(gg_game_new(&fresh, 9001, "Recorder"), "new game failed");
        CHECK(was != gg_state_hash(&fresh), "the session ended where it began");
        gg_game_free(&fresh);
    }

    // --- the replay ---------------------------------------------------------
    gg_replay r;
    CHECK(gg_replay_load(&r, path), "the replay would not load");
    CHECK(r.steps == PLAYED, "the replay holds %d steps, expected %d",
          r.steps, PLAYED);
    CHECK(r.seed == 9001, "the replay says seed %u", r.seed);
    CHECK(SDL_strcmp(r.profile, "Recorder") == 0, "the replay says '%s'",
          r.profile);
    CHECK(r.has_hash && r.hash == was, "the replay ends on %016llX, not %016llX",
          (unsigned long long)r.hash, (unsigned long long)was);

    // Every action came back as the action that was recorded. Checked because
    // the names are the file format: one misspelling in the table and a replay
    // is a different session that happens to parse.
    for (int i = 0; i < r.steps && i < PLAYED; i++) {
        CHECK(r.step[i].kind == GG_STEP_ACT, "step %d is not an action", i);
        CHECK(r.step[i].act == played[i], "step %d came back as %s, not %s",
              i, gg_action_name((gg_action)r.step[i].act),
              gg_action_name(played[i]));
    }

    gg_game b;
    CHECK(gg_game_new(&b, r.seed, r.profile), "the replay's world would not build");
    for (int i = 0; i < r.steps; i++) {
        if (r.step[i].kind != GG_STEP_ACT) continue;
        gg_game_act(&b, (gg_action)r.step[i].act);
        gg_game_animate(&b);
    }
    const uint64_t now = gg_state_hash(&b);
    CHECK(now == was, "the replay ended on %016llX and the session on %016llX",
          (unsigned long long)now, (unsigned long long)was);

    // And the hash is not a constant. One *turn* more, not one action more:
    // a blocked move costs no turn and changes nothing, so a replay one action
    // short can legitimately hash the same - which this test claimed was a
    // failure until the random script ended on a bump.
    gg_game c;
    CHECK(gg_game_new(&c, r.seed, r.profile), "the third world would not build");
    for (int i = 0; i < r.steps; i++)
        if (r.step[i].kind == GG_STEP_ACT) {
            gg_game_act(&c, (gg_action)r.step[i].act);
            gg_game_animate(&c);
        }
    CHECK(gg_state_hash(&c) == was, "the third world came out different");
    const uint32_t before = c.turn;
    gg_game_act(&c, GG_ACT_WAIT);
    CHECK(c.turn == before + 1, "waiting did not cost a turn");
    CHECK(gg_state_hash(&c) != was,
          "a session with one more turn in it hashes the same");

    gg_replay_free(&r);
    gg_game_free(&c);
    gg_game_free(&b);
    gg_game_free(&a);
    SDL_RemovePath(path);
}

// Every part of the world the hash claims to cover, poked one at a time. A hash
// that misses a field is worse than no hash: it makes a divergence in that
// field look like agreement.
static void the_state_hash_notices_every_part_of_the_world(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 4242, "Hasher"), "new game failed");
    for (int i = 0; i < 30; i++) gg_game_act(&g, GG_ACT_WAIT);

    const uint64_t base = gg_state_hash(&g);
    CHECK(gg_state_hash(&g) == base, "the hash of an untouched world moved");

    #define POKE(what, change) do {                                           \
        change;                                                               \
        CHECK(gg_state_hash(&g) != base, "the hash ignores " what);           \
    } while (0)

    POKE("the turn counter",  g.turn++);
    g.turn--;
    POKE("the clock",         g.minutes += 7);
    g.minutes -= 7;
    POKE("the day",           g.day++);
    g.day--;
    POKE("the RNG",           g.rng.s ^= 0x5A5Au);
    g.rng.s ^= 0x5A5Au;
    POKE("experience",        g.exp += 5);
    g.exp -= 5;
    POKE("the tally of the slain", g.slain++);
    g.slain--;
    POKE("what mode it is in", g.mode = GG_MODE_JOURNAL);
    g.mode = GG_MODE_PLAY;
    POKE("whether the story is over", g.story_over = true);
    g.story_over = false;

    POKE("where the Avatar is standing", gg_player(&g)->x++);
    gg_player(&g)->x--;
    POKE("the Avatar's health", gg_player(&g)->hp--);
    gg_player(&g)->hp++;
    POKE("which way somebody faces", g.actor[1].facing =
         (uint8_t)((g.actor[1].facing + 1) % 4));
    g.actor[1].facing = (uint8_t)((g.actor[1].facing + 3) % 4);
    POKE("whether somebody is still standing", g.actor[1].active = false);
    g.actor[1].active = true;
    POKE("who walks with the Avatar", g.actor[1].party = 1);
    g.actor[1].party = GG_NOT_IN_PARTY;

    POKE("what is carried", gg_pack_add(&g, GG_ITEM_STONE, 1));
    gg_pack_take(&g, gg_pack_find(&g, GG_ITEM_STONE), 1);
    POKE("what is held", g.equipped[GG_SLOT_LIGHT] = 0);
    g.equipped[GG_SLOT_LIGHT] = -1;

    POKE("a word learned", gg_learn(&g, "nothing_in_particular"));
    g.knownn--;
    POKE("a flag raised", gg_raise_flag(&g, "a_flag_of_no_consequence"));
    g.flags--;
    POKE("how far along a quest is", g.quest[0]++);
    g.quest[0]--;

    POKE("the ground", gg_ground_drop(&g.map, g.map.start_x, g.map.start_y,
                                      GG_ITEM_GOLD, 1));
    gg_ground_remove(&g.map, gg_ground_at(&g.map, g.map.start_x, g.map.start_y));
    POKE("the terrain", g.map.cell[0].terrain ^= 1);
    g.map.cell[0].terrain ^= 1;
    POKE("the trail behind the Avatar", g.trail_x[0]++);
    g.trail_x[0]--;
    POKE("which map is underfoot", SDL_strlcpy(g.here, "elsewhere.ggmap",
                                               sizeof g.here));
    g.here[0] = '\0';

    #undef POKE

    // Everything was put back, so it must hash the same again - which is also
    // what proves the pokes above were the only difference each time.
    CHECK(gg_state_hash(&g) == base,
          "putting the world back did not put the hash back");

    // And the other half of the claim: **animation is not state**. The world
    // advances only inside gg_game_act; gg_game_animate moves the drawing on,
    // and a hash that noticed would report a divergence on every replay that
    // ran at a different frame rate - which was this file's first bug.
    gg_game_act(&g, GG_ACT_E);
    const uint64_t stepped = gg_state_hash(&g);
    for (int i = 0; i < GG_STEP_TICKS * 3; i++) gg_game_animate(&g);
    CHECK(gg_state_hash(&g) == stepped,
          "drawing the world changed what the world hashes to");

    gg_game_free(&g);
}

// ---------------------------------------------------------------------------
// Getting at the game
// ---------------------------------------------------------------------------
// Rebinding a key, from the page a player would do it on, and then proving the
// input layer answers to the new key and no longer to the old one.
static void a_key_can_be_moved_and_the_world_hears_the_new_one(void) {
    const char *base = save_base();
    gg_settings set;
    gg_settings_defaults(&set);

    gg_game g;
    CHECK(gg_game_new(&g, 3, "Rebind"), "new game failed");

    gg_screens s;
    SDL_zero(s);
    gg_screens_enter(&s, GG_SCREEN_KEYS, base, &set, &g, true);
    CHECK(s.binding < 0, "the keys page opened already waiting for a key");
    CHECK(s.menu.n > 3, "the keys page has %d rows", s.menu.n);

    // Choosing a row asks for a key and answers nothing else until it has one.
    gg_menu_select(&s.menu, 0);
    const gg_screen_result waiting = gg_screens_choose(&s, base, &set, true);
    CHECK(waiting.action == GG_ACTION_NONE, "choosing a key row went somewhere");
    CHECK(s.binding >= 0, "the keys page is not waiting for a key");
    CHECK(s.notice[0] != '\0', "the page does not say it is waiting");

    // The first row is walking north, which starts on the up arrow.
    CHECK(set.key[GG_ACT_N] == SDL_SCANCODE_UP, "north does not start on Up");
    CHECK(gg_screens_bind(&s, &set, SDL_SCANCODE_HOME), "binding changed nothing");
    CHECK(set.key[GG_ACT_N] == SDL_SCANCODE_HOME, "north was not moved to Home");
    CHECK(s.binding < 0, "the page is still waiting after being told a key");

    // The world hears the new key, and no longer the old one.
    gg_input in;
    SDL_zero(in);
    gg_input_bind(&in, &set);

    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.scancode = SDL_SCANCODE_HOME;
    CHECK(gg_input_event(&in, &ev), "the new key was ignored");
    gg_input_tick(&in);
    CHECK(gg_input_take(&in) == GG_ACT_N, "Home does not walk north");

    SDL_zero(in);
    gg_input_bind(&in, &set);
    ev.key.scancode = SDL_SCANCODE_UP;
    CHECK(!gg_input_event(&in, &ev), "the old key still does something");
    gg_input_tick(&in);
    CHECK(gg_input_take(&in) == GG_ACT_NONE, "Up still walks north");

    // A key that already had a job loses it rather than doing both: two
    // actions on one key is a game doing two things at once.
    gg_menu_select(&s.menu, 1);                    // walking south
    gg_screens_choose(&s, base, &set, true);
    CHECK(gg_screens_bind(&s, &set, SDL_SCANCODE_HOME), "binding changed nothing");
    CHECK(set.key[GG_ACT_S] == SDL_SCANCODE_HOME, "south did not take Home");
    CHECK(set.key[GG_ACT_N] != SDL_SCANCODE_HOME,
          "Home is bound to two things at once");

    // Escape leaves it alone.
    gg_menu_select(&s.menu, 2);
    gg_screens_choose(&s, base, &set, true);
    const uint16_t was = set.key[GG_ACT_W];
    CHECK(!gg_screens_bind(&s, &set, SDL_SCANCODE_ESCAPE),
          "escaping reported a change");
    CHECK(set.key[GG_ACT_W] == was, "escaping changed the binding anyway");

    // And "put them all back" does.
    const int rows = s.menu.n - 2;                 // the row above "Back"
    gg_menu_select(&s.menu, rows);
    gg_screens_choose(&s, base, &set, true);
    CHECK(set.key[GG_ACT_N] == SDL_SCANCODE_UP, "putting them back left north on %d",
          set.key[GG_ACT_N]);
    CHECK(set.key[GG_ACT_S] == SDL_SCANCODE_DOWN, "south was not put back");

    gg_game_free(&g);
}

// The three options a player might need in order to play at all, each set from
// the options page and each surviving being written out and read back.
static void what_makes_the_game_reachable_survives_being_put_down(void) {
    // Copied, not held: gg_pref_file hands back a static buffer, and
    // save_base() below writes over it. The first version of this test wrote
    // its settings to the saves *directory*.
    char path[1024];
    SDL_strlcpy(path, gg_pref_file("test_access.txt"), sizeof path);
    gg_settings set;
    gg_settings_defaults(&set);

    CHECK(set.text_scale == 1, "text starts at %d", set.text_scale);
    CHECK(!set.plain_colours, "the plain palette starts on");

    gg_game g;
    CHECK(gg_game_new(&g, 4, "Options"), "new game failed");
    gg_screens s;
    SDL_zero(s);
    gg_screens_enter(&s, GG_SCREEN_OPTIONS, save_base(), &set, &g, true);

    // Found by name, so this is a test of the options page and not of where
    // its rows happen to sit.
    int text_row = -1, colour_row = -1;
    for (int i = 0; i < s.menu.n; i++) {
        if (SDL_strcmp(s.menu.item[i].label, "Text size") == 0) text_row = i;
        if (SDL_strcmp(s.menu.item[i].label, "Map colours") == 0) colour_row = i;
    }
    CHECK(text_row >= 0, "the options page has no text size row");
    CHECK(colour_row >= 0, "the options page has no map colours row");

    gg_menu_select(&s.menu, text_row);
    gg_screens_adjust(&s, 1, &set);
    CHECK(set.text_scale == 2, "the text did not grow");
    CHECK(SDL_strcmp(s.menu.item[text_row].detail, "large") == 0,
          "the row says '%s' after growing", s.menu.item[text_row].detail);

    gg_menu_select(&s.menu, colour_row);
    gg_screens_adjust(&s, 1, &set);
    CHECK(set.plain_colours, "the plain palette did not come on");

    // Rebind one as well, so the file has to carry all three kinds of thing.
    set.key[GG_ACT_FIGHT] = SDL_SCANCODE_END;
    set.alt[GG_ACT_FIGHT] = 0;

    CHECK(gg_settings_save(&set, path), "the settings would not save");

    gg_settings back;
    CHECK(gg_settings_load(&back, path), "the settings would not load");
    CHECK(back.text_scale == 2, "the text size came back as %d", back.text_scale);
    CHECK(back.plain_colours, "the palette came back off");
    CHECK(back.key[GG_ACT_FIGHT] == SDL_SCANCODE_END,
          "striking came back on scancode %d", back.key[GG_ACT_FIGHT]);
    CHECK(back.alt[GG_ACT_FIGHT] == 0, "an unbound key came back bound");

    // Every other binding survived too - a settings file that carries one key
    // and loses the rest is worse than one that carries none.
    for (int a = 1; a < GG_ACT_COUNT; a++)
        CHECK(back.key[a] == set.key[a] && back.alt[a] == set.alt[a],
              "the key for %s did not survive", gg_action_name((gg_action)a));

    // The font honours the size, which is what makes the panels move with it.
    gg_font_scale(back.text_scale);
    const int large = gg_font_height();
    gg_font_scale(1);
    CHECK(large == gg_font_height() * 2, "large text is %d against %d",
          large, gg_font_height());

    gg_game_free(&g);
    SDL_RemovePath(path);
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
// Which tune the world calls for is pure arithmetic over the game state, so it
// is checked as a number rather than by listening - which is the only way any
// of this could be checked at all.
static void the_tune_follows_where_you_are_and_what_hour_it_is(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 61, "Listener"), "new game failed");

    // Find a tile in the town, and one well outside every region.
    int tx = -1, ty = -1, wx = -1, wy = -1;
    for (int i = 0; i < g.map.regions && tx < 0; i++)
        if (g.map.region[i].kind == GG_REGION_TOWN) {
            tx = g.map.region[i].x + g.map.region[i].w / 2;
            ty = g.map.region[i].y + g.map.region[i].h / 2;
        }
    CHECK(tx >= 0, "the map has no town");
    for (int y = 1; y < g.map.h && wx < 0; y++)
        for (int x = 1; x < g.map.w && wx < 0; x++)
            if (gg_map_region_at(&g.map, x, y) < 0) { wx = x; wy = y; }
    CHECK(wx >= 0, "the map is all region and no wilderness");
    if (tx < 0 || wx < 0) { gg_game_free(&g); return; }

    gg_actor *p = gg_player(&g);

    // Four corners of the same rule: town or wild, day or night, all different.
    p->x = (int16_t)tx; p->y = (int16_t)ty;
    g.minutes = 12 * 60;
    const int town_day = gg_audio_tune_for(&g);
    g.minutes = 1 * 60;
    const int town_night = gg_audio_tune_for(&g);

    p->x = (int16_t)wx; p->y = (int16_t)wy;
    g.minutes = 12 * 60;
    const int wild_day = gg_audio_tune_for(&g);
    g.minutes = 1 * 60;
    const int wild_night = gg_audio_tune_for(&g);

    CHECK(town_day != town_night, "the town sounds the same at noon and at one");
    CHECK(wild_day != wild_night, "the wild sounds the same at noon and at one");
    CHECK(town_day != wild_day, "the town and the wild sound the same by day");
    CHECK(town_night != wild_night, "the town and the wild sound the same by night");

    // Every answer has to be a tune that exists, or the mixer reads off the end.
    const int all[] = { town_day, town_night, wild_day, wild_night };
    for (size_t i = 0; i < GG_COUNTOF(all); i++) {
        CHECK(all[i] >= 0 && all[i] < gg_audio_tune_count(),
              "the world asked for tune %d, and there are %d", all[i],
              gg_audio_tune_count());
        CHECK(gg_audio_tune_name(all[i])[0] != '\0', "tune %d has no name",
              all[i]);
    }

    // And the boundary is where the HUD says it is, so what a player reads and
    // what they hear agree.
    p->x = (int16_t)wx; p->y = (int16_t)wy;
    g.minutes = 6 * 60;
    CHECK(gg_audio_tune_for(&g) == wild_day, "six in the morning is not day");
    g.minutes = 5 * 60 + 59;
    CHECK(gg_audio_tune_for(&g) == wild_night, "one minute to six is not night");
    g.minutes = 19 * 60 + 59;
    CHECK(gg_audio_tune_for(&g) == wild_day, "one minute to eight is not day");
    g.minutes = 20 * 60;
    CHECK(gg_audio_tune_for(&g) == wild_night, "eight at night is not night");

    // An index out of range names nothing rather than reading off the end.
    CHECK(gg_audio_tune_name(-1)[0] == '\0', "a tune before the first has a name");
    CHECK(gg_audio_tune_name(gg_audio_tune_count())[0] == '\0',
          "a tune past the last has a name");

    gg_game_free(&g);
}

// The simulation says what happened; it does not know that anything listens.
static void the_world_says_what_it_did_and_forgets_it(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 62, "Noisy"), "new game failed");

    gg_event heard[GG_EVENTS_MAX];
    gg_events_drain(&g, heard, GG_EVENTS_MAX);   // clear whatever setup made

    // A step is a sound.
    const gg_actor *p = gg_player_const(&g);
    const int before = gg_dist_cheb(0, 0, p->x, p->y);
    (void)before;
    gg_game_act(&g, GG_ACT_E);
    int n = gg_events_drain(&g, heard, GG_EVENTS_MAX);
    CHECK(n > 0, "walking made no sound at all");
    bool stepped = false;
    for (int i = 0; i < n; i++)
        if (heard[i] == GG_EV_STEP || heard[i] == GG_EV_DOOR ||
            heard[i] == GG_EV_BUMP) stepped = true;
    CHECK(stepped, "walking made a sound, but not one about walking");

    // Draining empties it: a sound is heard once.
    n = gg_events_drain(&g, heard, GG_EVENTS_MAX);
    CHECK(n == 0, "the same %d sounds were heard twice", n);

    // Picking something up is a different sound from setting it down.
    gg_ground_drop(&g.map, gg_player_const(&g)->x, gg_player_const(&g)->y,
                   GG_ITEM_APPLE, 1);
    gg_game_act(&g, GG_ACT_GET);
    n = gg_events_drain(&g, heard, GG_EVENTS_MAX);
    bool took = false;
    for (int i = 0; i < n; i++) if (heard[i] == GG_EV_TAKE) took = true;
    CHECK(took, "taking something made no sound about taking");

    // The queue is bounded, and overflow drops rather than grows.
    for (int i = 0; i < GG_EVENTS_MAX * 4; i++) gg_emit(&g, GG_EV_STEP);
    CHECK(g.events <= GG_EVENTS_MAX, "the queue grew to %d, past its bound of %d",
          g.events, GG_EVENTS_MAX);
    n = gg_events_drain(&g, heard, GG_EVENTS_MAX);
    CHECK(n == GG_EVENTS_MAX, "drained %d of a full queue of %d", n,
          GG_EVENTS_MAX);

    // Something past the end of the enum is refused rather than stored.
    gg_emit(&g, (gg_event)GG_EV_COUNT);
    CHECK(g.events == 0, "a sound that does not exist went into the queue");

    gg_game_free(&g);
}

// Every event the simulation can emit needs a sound, or something happens in
// silence and nobody notices until they wonder why.
static void every_event_has_a_sound_baked_for_it(void) {
    for (int i = 0; i < GG_EV_COUNT; i++) {
        char path[1024];
        // The names the baker writes, and the ones gg_audio.c looks for.
        static const char *const NAME[GG_EV_COUNT] = {
            [GG_EV_STEP] = "step", [GG_EV_BUMP] = "bump", [GG_EV_BLOW] = "blow",
            [GG_EV_HURT] = "hurt", [GG_EV_DIE] = "die", [GG_EV_TAKE] = "take",
            [GG_EV_DROP] = "drop", [GG_EV_COIN] = "coin", [GG_EV_DOOR] = "door",
            [GG_EV_CAST] = "cast", [GG_EV_LEARN] = "learn",
            [GG_EV_LEVEL] = "level",
        };
        CHECK(NAME[i] != nullptr, "event %d has no sound named for it", i);
        if (!NAME[i]) continue;

        SDL_snprintf(path, sizeof path, "%sfx_%s.wav",
                     gg_asset_path("sounds/"), NAME[i]);
        SDL_IOStream *io = SDL_IOFromFile(path, "rb");
        CHECK(io != nullptr, "no sound was baked for event %d (%s)", i, NAME[i]);
        if (io) {
            // A WAV, and not an empty one - a zero-length file would load and
            // play nothing, which is the same as having no sound at all.
            const Sint64 size = SDL_GetIOSize(io);
            CHECK(size > 44, "the sound for %s is %lld bytes, which is a header "
                  "and nothing else", NAME[i], (long long)size);
            SDL_CloseIO(io);
        }
    }

    // And every tune the world can ask for has a file too.
    for (int i = 0; i < gg_audio_tune_count(); i++) {
        char path[1024];
        SDL_snprintf(path, sizeof path, "%smus_%s.wav",
                     gg_asset_path("sounds/"), gg_audio_tune_name(i));
        SDL_IOStream *io = SDL_IOFromFile(path, "rb");
        CHECK(io != nullptr, "no tune was baked for %s", gg_audio_tune_name(i));
        if (io) SDL_CloseIO(io);
    }
}

// ---------------------------------------------------------------------------
// The editor
// ---------------------------------------------------------------------------
// The plan's own verification: a map authored in the editor, saved, and played
// in the game with no code change. Authored through exactly the calls the mouse
// makes - the editor's window is only a way of calling these.
static void a_map_authored_in_the_editor_can_be_played(void) {
    const char *path = gg_pref_file("test_authored.ggmap");

    gg_editor e;
    SDL_zero(e);
    CHECK(gg_edit_new(&e, 48, 40), "could not make a map");

    // A road across the middle.
    gg_edit_tool(&e, GG_TOOL_TERRAIN);
    while (e.terrain != GG_TILE_ROAD) gg_edit_brush(&e, 1);
    for (int x = 4; x < 44; x++) gg_edit_apply(&e, x, 20);

    // A lake, which must come out flagged as water or it can be walked on.
    while (e.terrain != GG_TILE_WATER) gg_edit_brush(&e, 1);
    for (int y = 4; y < 10; y++)
        for (int x = 4; x < 12; x++) gg_edit_apply(&e, x, y);
    const gg_cell *wet = gg_map_at_const(&e.map, 6, 6);
    CHECK(wet && (wet->flags & GG_CELL_WATER),
          "painted water that is not flagged as water");
    CHECK(!gg_map_walkable(&e.map, 6, 6), "the lake can be walked on");

    // A house, through the same placement the generator uses.
    gg_edit_tool(&e, GG_TOOL_PROP);
    while (e.prop != GG_PROP_HOUSE_BRICK_A) gg_edit_brush(&e, 1);
    gg_edit_apply(&e, 24, 30);
    CHECK(SDL_strstr(e.say, "not fit") == nullptr, "the house would not fit: %s",
          e.say);

    // Somebody to live in it, with a day.
    gg_edit_tool(&e, GG_TOOL_ACTOR);
    while (e.art != GG_ACTOR_MERCHANT) gg_edit_brush(&e, 1);
    gg_edit_apply(&e, 30, 20);
    CHECK(e.map.actors == 1, "expected one person, got %d", e.map.actors);
    CHECK(e.actor == 0, "the placed person was not selected");
    gg_edit_name_actor(&e, "Gwyneth");

    gg_edit_tool(&e, GG_TOOL_SCHEDULE);
    gg_edit_apply(&e, 30, 20);
    gg_edit_apply(&e, 34, 20);
    gg_edit_apply(&e, 34, 24);
    gg_edit_apply(&e, 30, 24);
    CHECK(e.map.actor[0].schedn == 4, "expected four hours, got %u",
          e.map.actor[0].schedn);

    // Something lying about.
    gg_edit_tool(&e, GG_TOOL_ITEM);
    while (e.item != GG_ITEM_GOLD) gg_edit_brush(&e, 1);
    e.item_count = 9;
    gg_edit_apply(&e, 20, 20);

    // A town, dragged out.
    gg_edit_tool(&e, GG_TOOL_REGION);
    while (e.region_kind != GG_REGION_TOWN) gg_edit_brush(&e, 1);
    gg_edit_drag_start(&e, 18, 16);
    gg_edit_drag_end(&e, 38, 34);
    CHECK(e.map.regions == 1, "expected one region, got %d", e.map.regions);
    SDL_strlcpy(e.map.region[0].name, "Wyndle", sizeof e.map.region[0].name);

    // And where a new game begins.
    gg_edit_tool(&e, GG_TOOL_START);
    gg_edit_apply(&e, 20, 21);
    SDL_strlcpy(e.map.name, "The Test Vale", sizeof e.map.name);

    // The editor is where a map is found to be broken, not the game.
    char problems[GG_EDIT_PROBLEMS_MAX][GG_EDIT_SAY_MAX];
    const int bad = gg_edit_check(&e, problems);
    for (int i = 0; i < bad && i < GG_EDIT_PROBLEMS_MAX; i++)
        CHECK(false, "the authored map has a problem: %s", problems[i]);
    CHECK(bad == 0, "the authored map has %d problems", bad);

    CHECK(gg_edit_save(&e, path), "could not save: %s", e.say);
    CHECK(!e.dirty, "the map is still dirty after saving");
    gg_edit_close(&e);

    // Now play it, with no code change - which is the whole claim.
    gg_game g;
    CHECK(gg_game_new_from_map(&g, path, "Author", 4242), "the game would not open it");

    CHECK(SDL_strcmp(g.map.name, "The Test Vale") == 0,
          "the map came back called '%s'", g.map.name);
    CHECK(g.map.w == 48 && g.map.h == 40, "the map came back %dx%d",
          g.map.w, g.map.h);

    const gg_actor *p = gg_player_const(&g);
    CHECK(p->x == 20 && p->y == 21, "the avatar began at %d,%d, not where the "
          "start was put", p->x, p->y);
    CHECK(SDL_strcmp(gg_game_place(&g), "Wyndle") == 0,
          "the avatar is in '%s', not the region that was drawn",
          gg_game_place(&g));

    // The person authored is in the world, under the name they were given, and
    // keeping the day they were given.
    int found = -1;
    for (int i = 0; i < g.actors; i++)
        if (SDL_strcmp(g.actor[i].name, "Gwyneth") == 0) found = i;
    CHECK(found >= 0, "the person authored is not in the world");
    if (found >= 0) {
        CHECK(g.actor[found].schedn == 4, "she came back with %u hours",
              g.actor[found].schedn);
        CHECK(g.actor[found].art == GG_ACTOR_MERCHANT, "she is wearing the "
              "wrong sprite");
    }

    // The generator's own eight are *not* there: a map that says who lives in
    // it is the whole of who lives in it.
    for (int i = 0; i < g.actors; i++)
        CHECK(SDL_strcmp(g.actor[i].name, "Iolo") != 0,
              "the generator's townsfolk turned up in an authored map");

    // What was left lying about is where it was left.
    const int loot = gg_ground_at(&g.map, 20, 20);
    CHECK(loot >= 0, "the gold that was placed is not there");
    if (loot >= 0)
        CHECK(g.map.ground[loot].count == 9, "%u coins, expected 9",
              g.map.ground[loot].count);

    // And it is a world that turns.
    for (int t = 0; t < 40; t++) gg_game_act(&g, GG_ACT_WAIT);
    CHECK(g.turn == 40, "the authored world stopped at turn %u", g.turn);

    gg_game_free(&g);
    SDL_RemovePath(path);
}

static void the_editor_rubs_out_what_it_draws(void) {
    gg_editor e;
    SDL_zero(e);
    CHECK(gg_edit_new(&e, 32, 32), "could not make a map");

    // A house leaves walls over a whole footprint, so rubbing it out has to
    // clear all of them - otherwise the map keeps invisible walls where a
    // building used to be, which is a bug nobody can see.
    gg_edit_tool(&e, GG_TOOL_PROP);
    while (e.prop != GG_PROP_HOUSE_BRICK_A) gg_edit_brush(&e, 1);
    gg_edit_apply(&e, 16, 20);

    int blocked = 0;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            if (!gg_map_walkable(&e.map, x, y)) blocked++;
    CHECK(blocked > 0, "placing a house blocked nothing");

    // Rubbed out by clicking a wall, not the anchor - which is what a person
    // would actually click.
    gg_edit_erase(&e, 15, 18);
    int still = 0;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            if (!gg_map_walkable(&e.map, x, y)) still++;
    CHECK(still == 0, "%d tiles are still blocked after rubbing out the house",
          still);

    // People, items and regions all rub out too.
    gg_edit_tool(&e, GG_TOOL_ACTOR);
    gg_edit_apply(&e, 5, 5);
    gg_edit_apply(&e, 7, 7);
    CHECK(e.map.actors == 2, "expected two people");
    gg_edit_erase(&e, 5, 5);
    CHECK(e.map.actors == 1, "expected one person left");
    CHECK(gg_edit_actor_at(&e, 7, 7) >= 0, "the wrong person was rubbed out");

    gg_edit_tool(&e, GG_TOOL_ITEM);
    gg_edit_apply(&e, 9, 9);
    CHECK(gg_ground_at(&e.map, 9, 9) >= 0, "nothing was left there");
    gg_edit_erase(&e, 9, 9);
    CHECK(gg_ground_at(&e.map, 9, 9) < 0, "it is still there");

    gg_edit_tool(&e, GG_TOOL_REGION);
    gg_edit_drag_start(&e, 2, 2);
    gg_edit_drag_end(&e, 6, 6);
    CHECK(e.map.regions == 1, "expected one region");
    gg_edit_erase(&e, 4, 4);
    CHECK(e.map.regions == 0, "the region is still there");

    gg_edit_close(&e);
}

// The editor is where a map should be found to be broken.
static void the_editor_says_what_is_wrong_with_a_map(void) {
    gg_editor e;
    SDL_zero(e);
    CHECK(gg_edit_new(&e, 32, 32), "could not make a map");
    SDL_strlcpy(e.map.name, "Somewhere", sizeof e.map.name);

    char problems[GG_EDIT_PROBLEMS_MAX][GG_EDIT_SAY_MAX];
    CHECK(gg_edit_check(&e, problems) == 0,
          "a blank map is already broken: %s", problems[0]);

    // A start inside a lake is the one that makes a map unplayable.
    gg_edit_tool(&e, GG_TOOL_TERRAIN);
    while (e.terrain != GG_TILE_WATER) gg_edit_brush(&e, 1);
    gg_edit_apply(&e, e.map.start_x, e.map.start_y);
    CHECK(gg_edit_check(&e, problems) > 0, "a start under water is not a problem");

    while (e.terrain != GG_TILE_GRASS) gg_edit_brush(&e, 1);
    gg_edit_apply(&e, e.map.start_x, e.map.start_y);
    CHECK(gg_edit_check(&e, problems) == 0, "still broken: %s", problems[0]);

    // Somebody sent somewhere solid stands and shoves at it all day, which is
    // exactly the failure the generator learned to avoid.
    gg_edit_tool(&e, GG_TOOL_ACTOR);
    gg_edit_apply(&e, 10, 10);
    gg_edit_name_actor(&e, "Someone");
    gg_edit_tool(&e, GG_TOOL_TERRAIN);
    while (e.terrain != GG_TILE_MOUNTAIN) gg_edit_brush(&e, 1);
    gg_edit_apply(&e, 20, 20);
    gg_edit_tool(&e, GG_TOOL_SCHEDULE);
    gg_edit_apply(&e, 20, 20);

    const int bad = gg_edit_check(&e, problems);
    CHECK(bad > 0, "a schedule point inside a mountain is not a problem");
    bool mentioned = false;
    for (int i = 0; i < bad && i < GG_EDIT_PROBLEMS_MAX; i++)
        if (SDL_strstr(problems[i], "solid")) mentioned = true;
    CHECK(mentioned, "the problem was not the one about being sent somewhere solid");

    gg_edit_close(&e);
}

static void a_failed_load_leaves_what_was_open_alone(void) {
    gg_editor e;
    SDL_zero(e);
    CHECK(gg_edit_new(&e, 24, 24), "could not make a map");
    SDL_strlcpy(e.map.name, "Kept", sizeof e.map.name);
    gg_edit_tool(&e, GG_TOOL_ACTOR);
    gg_edit_apply(&e, 5, 5);

    CHECK(!gg_edit_load(&e, gg_pref_file("test_no_such_map.ggmap")),
          "loading a map that is not there reported success");
    CHECK(e.open, "a failed load closed the map that was open");
    CHECK(SDL_strcmp(e.map.name, "Kept") == 0,
          "a failed load replaced the map with '%s'", e.map.name);
    CHECK(e.map.actors == 1, "a failed load lost the person who was placed");

    gg_edit_close(&e);
}

// The plan's own verification, stated as a rule the code has to keep: nothing
// in the simulation knows anybody's name. A townsperson added to the book turns
// up in the world, and one taken out of it does not.
static void who_lives_in_the_town_comes_out_of_the_book(void) {
    const char *path = write_dialogue(
        "person Wilkin\n"
        "art ELDER\n"
        "home Britain\n"
        "at 06 -3 -3\n"
        "at 12 3 -3\n"
        "at 18 3 3\n"
        "at 22 -3 3\n"
        "greet Wilkin, and glad of it.\n"
        "topic name\n"
        "  say Wilkin.\n"
        "person Voice\n"
        "greet I am only a voice.\n"
        "topic name\n"
        "  say Nobody you can meet.\n");
    CHECK(gg_dialogue_load(path), "the book did not load");
    CHECK(gg_dialogue_speakers() == 2, "expected two people in the book");

    const gg_speaker *w = gg_dialogue_find("Wilkin");
    CHECK(w != nullptr, "Wilkin is not in the book");
    CHECK(w && w->lives, "Wilkin has a sprite but is not marked as living here");
    CHECK(w && w->schedn == 4, "Wilkin has %d hours, expected 4", w ? w->schedn : -1);
    CHECK(w && w->art == GG_ACTOR_ELDER, "Wilkin is wearing the wrong sprite");

    const gg_speaker *v = gg_dialogue_find("Voice");
    CHECK(v && !v->lives, "somebody with no sprite is being placed in the world");

    gg_game g;
    CHECK(gg_game_new(&g, 71, "Bookkeeper"), "new game failed");

    int wilkins = 0, voices = 0, residents = 0;
    for (int i = 0; i < g.actors; i++) {
        if (i == g.player || !g.actor[i].active || g.actor[i].hostile) continue;
        residents++;
        if (SDL_strcmp(g.actor[i].name, "Wilkin") == 0) wilkins++;
        if (SDL_strcmp(g.actor[i].name, "Voice") == 0) voices++;
    }
    CHECK(wilkins == 1, "the town holds %d Wilkins, expected one", wilkins);
    CHECK(voices == 0, "a voice with no sprite was placed in the world");
    CHECK(residents == 1, "the town holds %d people, and the book names one who "
          "lives there", residents);

    // The one in the world keeps the day the file gave them, walked out of any
    // wall the generator happened to put there - and their greeting comes from
    // the same block rather than from a second table.
    for (int i = 0; i < g.actors; i++) {
        if (SDL_strcmp(g.actor[i].name, "Wilkin") != 0) continue;
        CHECK(g.actor[i].schedn == 4, "Wilkin came into the world with %u hours",
              g.actor[i].schedn);
        CHECK(g.actor[i].greeting && SDL_strstr(g.actor[i].greeting, "glad of it"),
              "Wilkin's greeting did not come from the book");
        for (int k = 0; k < g.actor[i].schedn; k++)
            CHECK(gg_map_walkable(&g.map, g.actor[i].sched[k].x,
                                  g.actor[i].sched[k].y),
                  "Wilkin is sent somewhere solid at %02u:00",
                  g.actor[i].sched[k].hour);
    }

    gg_game_free(&g);
    restore_dialogue();
    SDL_RemovePath(path);

    // And with the shipped book, the town is the eight it names.
    gg_game h;
    CHECK(gg_game_new(&h, 72, "Vale"), "new game failed");
    // Only the people whose home this town *is*. The book holds more than one
    // town now, and somebody who lives in Wyndle has no business standing in
    // Britain - which is the whole point of a person naming where they live.
    int shipped = 0, placed = 0;
    for (int i = 0; i < gg_dialogue_speakers(); i++) {
        const gg_speaker *d = gg_dialogue_speaker(i);
        if (d->lives && SDL_strcasecmp(d->home, "Britain") == 0) shipped++;
    }
    for (int i = 0; i < h.actors; i++)
        if (i != h.player && h.actor[i].active && !h.actor[i].hostile) placed++;
    CHECK(shipped > 0, "the shipped book names nobody who lives in Britain");
    CHECK(placed == shipped, "the book names %d of Britain and the town holds %d",
          shipped, placed);
    gg_game_free(&h);
}

// A person in the book with a sprite must have a day, or they stand on the
// square from dawn to dawn - which is the whole reason a schedule exists.
static void somebody_who_lives_here_needs_a_day(void) {
    const char *path = write_dialogue(
        "person Idle\n"
        "art GUARD\n"
        "home Britain\n"
        "greet I have nowhere to be.\n"
        "topic name\n"
        "  say Idle.\n");
    CHECK(!gg_dialogue_load(path),
          "somebody with a sprite and no day was accepted");
    CHECK(gg_dialogue_speakers() == 0, "the bad book left people behind");

    // And an `at` line that is missing a number is refused rather than read as
    // a zero, which would silently put somebody on the town square.
    const char *bad = write_dialogue(
        "person Half\n"
        "art GUARD\n"
        "home Britain\n"
        "at 06 -3\n"
        "greet Half a day.\n"
        "topic name\n"
        "  say Half.\n");
    CHECK(!gg_dialogue_load(bad), "a half-written schedule line was accepted");

    restore_dialogue();
    SDL_RemovePath(path);
    SDL_RemovePath(bad);
}

// ---------------------------------------------------------------------------
// The story
// ---------------------------------------------------------------------------
static const char *write_quests(const char *text) {
    const char *path = gg_pref_file("test_quests.txt");
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    CHECK(io != nullptr, "could not write a quest file");
    if (io) {
        const size_t n = SDL_strlen(text);
        CHECK(SDL_WriteIO(io, text, n) == n, "short write on the quest file");
        SDL_CloseIO(io);
    }
    return path;
}

static void restore_quests(void) {
    CHECK(gg_quests_load(gg_asset_path("quests.txt")),
          "could not put the shipped quests back");
}

// The plan's own verification: a two-stage quest completed, its journal
// updated, and the state surviving a save/load.
static void a_two_stage_quest_is_completed_and_remembered(void) {
    const char *who = "Questor";
    wipe_saves(who);

    const char *path = write_quests(
        "quest ERRAND\n"
        "  name A Small Errand\n"
        "  stage\n"
        "    when knows errand\n"
        "    journal Somebody has asked thee to fetch a loaf.\n"
        "    sets errand_begun\n"
        "  stage\n"
        "    when has BREAD 2\n"
        "    journal Thou hast the bread, and the errand is done.\n"
        "    sets errand_done\n");
    CHECK(gg_quests_load(path), "the quest file did not load");
    CHECK(gg_quests_count() == 1, "expected one quest, got %d", gg_quests_count());

    gg_game g;
    CHECK(gg_game_new(&g, 81, who), "new game failed");
    const int which = gg_quest_find("ERRAND");
    CHECK(which >= 0, "the errand is not in the book");

    // Not begun: the word has not been learned, so nothing is in the journal
    // however plainly the quest is written.
    g.packn = 0;
    gg_quests_tick(&g);
    CHECK(g.quest[which] == 0, "the quest began before it was given");
    CHECK(gg_journal_lines(&g) == 0, "the journal has %d entries already",
          gg_journal_lines(&g));
    CHECK(!gg_flag(&g, "errand_begun"), "a flag went up before its stage");

    // Stage one, by learning the word.
    gg_learn(&g, "errand");
    gg_quests_tick(&g);
    CHECK(g.quest[which] == 1, "the quest is at stage %u, expected 1",
          g.quest[which]);
    CHECK(gg_journal_lines(&g) == 1, "the journal has %d entries, expected 1",
          gg_journal_lines(&g));
    CHECK(gg_flag(&g, "errand_begun"), "entering a stage did not raise its flag");

    const char *quest_name = nullptr, *text = nullptr;
    bool done = false;
    CHECK(gg_journal_line(&g, 0, &quest_name, &text, &done),
          "the journal has no first line");
    CHECK(quest_name && SDL_strcmp(quest_name, "A Small Errand") == 0,
          "the entry is filed under '%s'", quest_name ? quest_name : "(none)");
    CHECK(text && SDL_strstr(text, "fetch a loaf"),
          "the entry does not say what the file says");
    CHECK(!done, "a quest with a stage left is already finished");

    // One loaf is not two: a condition is a condition.
    CHECK(gg_pack_add(&g, GG_ITEM_BREAD, 1) == 1, "could not take bread");
    gg_quests_tick(&g);
    CHECK(g.quest[which] == 1, "one loaf finished an errand that wants two");

    // Two, and it completes.
    CHECK(gg_pack_add(&g, GG_ITEM_BREAD, 1) == 1, "could not take more bread");
    gg_quests_tick(&g);
    CHECK(g.quest[which] == 2, "the quest is at stage %u, expected 2",
          g.quest[which]);
    CHECK(gg_journal_lines(&g) == 2, "the journal has %d entries, expected 2",
          gg_journal_lines(&g));
    CHECK(gg_flag(&g, "errand_done"), "the last stage did not raise its flag");

    CHECK(gg_journal_line(&g, 1, nullptr, &text, &done),
          "the journal has no second line");
    CHECK(text && SDL_strstr(text, "errand is done"), "the wrong second entry");
    CHECK(done, "a quest with no stages left is not marked finished");

    // It cannot run past its end however the world changes.
    gg_pack_add(&g, GG_ITEM_BREAD, 5);
    gg_quests_tick(&g);
    CHECK(g.quest[which] == 2, "the quest ran past its last stage to %u",
          g.quest[which]);

    // And all of it survives a save.
    CHECK(gg_save_write(&g, save_base(), who), "save failed");
    gg_game b;
    SDL_zero(b);
    CHECK(gg_save_read(&b, save_base(), who), "load failed");

    CHECK(b.quest[which] == 2, "the resumed game is at stage %u", b.quest[which]);
    CHECK(gg_journal_lines(&b) == 2, "the resumed journal has %d entries",
          gg_journal_lines(&b));
    CHECK(gg_flag(&b, "errand_begun") && gg_flag(&b, "errand_done"),
          "the resumed game forgot its flags");
    const char *btext = nullptr;
    CHECK(gg_journal_line(&b, 1, nullptr, &btext, nullptr) && btext &&
          SDL_strstr(btext, "errand is done"),
          "the resumed journal reads differently");

    gg_game_free(&b);
    gg_game_free(&g);
    restore_quests();
    SDL_RemovePath(path);
    wipe_saves(who);
}

// Stages are entered in order and one at a time, whatever the world looks like.
static void a_quest_cannot_skip_a_stage(void) {
    const char *path = write_quests(
        "quest LADDER\n"
        "  name Up A Ladder\n"
        "  stage\n"
        "    when knows one\n"
        "    journal First.\n"
        "  stage\n"
        "    when knows two\n"
        "    journal Second.\n"
        "  stage\n"
        "    when\n"
        "    journal Third, which follows at once.\n");
    CHECK(gg_quests_load(path), "the quest file did not load");

    gg_game g;
    CHECK(gg_game_new(&g, 82, "Climber"), "new game failed");
    const int which = gg_quest_find("LADDER");

    // The second word alone does nothing: the first stage has not been entered.
    gg_learn(&g, "two");
    gg_quests_tick(&g);
    CHECK(g.quest[which] == 0, "a quest began at its second stage");

    // With both, it walks up as far as it can in one tick - and the third
    // stage, whose condition is nothing at all, follows on the same turn.
    gg_learn(&g, "one");
    gg_quests_tick(&g);
    CHECK(g.quest[which] == 3, "the quest climbed to %u, expected all three",
          g.quest[which]);
    CHECK(gg_journal_lines(&g) == 3, "the journal has %d entries, expected 3",
          gg_journal_lines(&g));

    gg_game_free(&g);
    restore_quests();
    SDL_RemovePath(path);
}

// Quests interlock through flags without knowing about each other.
static void one_quest_can_open_another(void) {
    const char *path = write_quests(
        "quest FIRST\n"
        "  name The First Thing\n"
        "  stage\n"
        "    when knows go\n"
        "    journal It has begun.\n"
        "    sets the_first_thing_happened\n"
        "quest SECOND\n"
        "  name The Second Thing\n"
        "  stage\n"
        "    when flag the_first_thing_happened\n"
        "    journal And so this follows.\n");
    CHECK(gg_quests_load(path), "the quest file did not load");

    gg_game g;
    CHECK(gg_game_new(&g, 83, "Linker"), "new game failed");
    const int first = gg_quest_find("FIRST"), second = gg_quest_find("SECOND");
    CHECK(first >= 0 && second >= 0, "the pair are not both in the book");

    gg_quests_tick(&g);
    CHECK(g.quest[second] == 0, "the second began without the first");

    gg_learn(&g, "go");
    gg_quests_tick(&g);
    CHECK(g.quest[first] == 1, "the first did not begin");
    // The second may need one more tick, depending on the order they are
    // walked in - which is fine, and is what a turn is for.
    gg_quests_tick(&g);
    CHECK(g.quest[second] == 1, "the flag did not open the second quest");

    gg_game_free(&g);
    restore_quests();
    SDL_RemovePath(path);
}

// A quest condition that counts kills, which is the one number the story
// needed that the world was not already keeping.
static void killing_things_moves_a_quest_on(void) {
    const char *path = write_quests(
        "quest CULL\n"
        "  name A Thinning\n"
        "  stage\n"
        "    when slain 2\n"
        "    journal Two of them have fallen.\n");
    CHECK(gg_quests_load(path), "the quest file did not load");

    gg_game g;
    int cx = 0, cy = 0;
    CHECK(set_up_encounter(&g, 84, &cx, &cy), "setup failed");
    const int which = gg_quest_find("CULL");
    g.slain = 0;

    for (int k = 0; k < 2; k++) {
        const int foe = gg_spawn_named(&g, "BRIGAND", cx + 1 + k, cy);
        CHECK(foe >= 0, "no brigand");
        for (int i = 0; i < 80 && g.actor[foe].active; i++) {
            g.actor[foe].hp = 1;
            gg_strike(&g, g.player, foe);
        }
        CHECK(!g.actor[foe].active, "the brigand would not fall");
    }
    CHECK(g.slain == 2, "the world counted %u fallen, expected 2", g.slain);

    gg_quests_tick(&g);
    CHECK(g.quest[which] == 1, "two kills did not move the quest on");

    gg_game_free(&g);
    restore_quests();
    SDL_RemovePath(path);
}

static void a_quest_file_that_does_not_parse_loads_nothing(void) {
    static const char *const BAD[] = {
        "name A Thing\n",                                  // before any quest
        "quest X\n  stage\n    when knows a\n    journal Y.\n",  // no name
        "quest X\n  name A\n",                             // no stages
        "quest X\n  name A\n  stage\n    when knows a\n",  // stage says nothing
        "quest X\n  name A\n  stage\n    journal Y.\n",    // begins at once
        "quest X\n  name A\n  stage\n    when wibble\n    journal Y.\n",
        "quest X\n  name A\n  stage\n    when has NOTHING 1\n    journal Y.\n",
        "quest X\n  name A\n  stage\n    when slain\n    journal Y.\n",
        "quest X\n  name A\n  wibble 3\n",                 // unknown word
    };
    for (size_t i = 0; i < GG_COUNTOF(BAD); i++) {
        const char *path = write_quests(BAD[i]);
        CHECK(!gg_quests_load(path), "bad quest file %zu loaded anyway", i);
        CHECK(gg_quests_count() == 0,
              "bad quest file %zu left %d quests behind", i, gg_quests_count());
        SDL_RemovePath(path);
    }
    CHECK(!gg_quests_load(gg_pref_file("test_no_such_quests.txt")),
          "a missing quest file reported success");
    restore_quests();
}

// The shipped story has to be one the vale can actually tell.
// The vale ships with one person in the map file and seven more in the book,
// and the story is unwalkable unless all eight are standing in it: Iolo teaches
// CARAVAN, and it is Shamino, Nell, Nystul and Gwenno who carry it onward.
static void the_vale_is_peopled_by_the_book(void) {
    CHECK(gg_dialogue_load(gg_asset_path("dialogue.txt")), "no book");

    gg_game g;
    CHECK(gg_game_new_from_map(&g, gg_asset_path("maps/vale.map.txt"), "Villager", 4242),
          "the vale would not open");

    // Everybody the book says lives in Britain is here, once - and nobody who
    // lives anywhere else is.
    int residents = 0;
    for (int i = 0; i < gg_dialogue_speakers(); i++) {
        const gg_speaker *d = gg_dialogue_speaker(i);
        if (!d || !d->lives) continue;
        if (SDL_strcasecmp(d->home, "Britain") != 0) {
            int strangers = 0;
            for (int k = 0; k < g.actors; k++)
                if (g.actor[k].active && SDL_strcmp(g.actor[k].name, d->name) == 0)
                    strangers++;
            CHECK(strangers == 0, "%s lives in %s and is standing in the vale",
                  d->name, d->home);
            continue;
        }
        residents++;
        int here = 0;
        for (int k = 0; k < g.actors; k++)
            if (g.actor[k].active && SDL_strcmp(g.actor[k].name, d->name) == 0) here++;
        CHECK(here == 1, "the vale holds %d of %s, expected one", here, d->name);
    }
    CHECK(residents >= 8, "the book has %d residents, expected the vale's eight",
          residents);

    // Iolo is the map's own, not the book's copy of him: the map knows where
    // his stall is and the book only knows how far it is from the square.
    int iolo = -1;
    for (int i = 0; i < g.actors; i++)
        if (SDL_strcmp(g.actor[i].name, "Iolo") == 0) iolo = i;
    CHECK(iolo > 0, "Iolo is not in the vale");
    if (iolo > 0)
        CHECK(g.actor[iolo].x == 42 && g.actor[iolo].y == 44,
              "Iolo is at %d,%d, not where the map put him",
              g.actor[iolo].x, g.actor[iolo].y);

    // And they are standing somewhere real, not inside the scenery.
    for (int i = 0; i < g.actors; i++)
        if (g.actor[i].active)
            CHECK(gg_map_walkable(&g.map, g.actor[i].x, g.actor[i].y),
                  "%s is standing inside something at %d,%d", g.actor[i].name,
                  g.actor[i].x, g.actor[i].y);

    // The standing stones have no Britain in them and so have none of Britain's
    // people - the rule that keeps a town from following you over a hill.
    gg_game s;
    CHECK(gg_game_new_from_map(&s, gg_asset_path("maps/stones.map.txt"), "Walker", 4242),
          "the stones would not open");
    for (int i = 0; i < s.actors; i++)
        if (i != s.player && s.actor[i].active)
            CHECK(gg_dialogue_find(s.actor[i].name) == nullptr,
                  "%s followed the book out to the standing stones",
                  s.actor[i].name);
    gg_game_free(&s);
    gg_game_free(&g);
    restore_dialogue();
}

static void the_vale_has_a_story_that_can_be_reached(void) {
    CHECK(gg_dialogue_load(gg_asset_path("dialogue.txt")), "no dialogue");
    CHECK(gg_quests_load(gg_asset_path("quests.txt")), "no quests");
    CHECK(gg_quests_count() > 0, "the vale has no story at all");

    // Every word a quest waits on has to be one somebody teaches, or the quest
    // waits for ever.
    char taught[GG_KNOWN_MAX][GG_WORD_MAX];
    int n = 0;
    SDL_strlcpy(taught[n++], GG_WORD_NAME, GG_WORD_MAX);
    SDL_strlcpy(taught[n++], GG_WORD_JOB, GG_WORD_MAX);
    for (int i = 0; i < gg_dialogue_speakers(); i++) {
        const gg_speaker *s = gg_dialogue_speaker(i);
        for (int t = 0; t < s->topics; t++) {
            if (!s->topic[t].teach[0]) continue;
            bool have = false;
            for (int k = 0; k < n; k++)
                if (SDL_strcasecmp(taught[k], s->topic[t].teach) == 0) have = true;
            if (!have && n < GG_KNOWN_MAX)
                SDL_strlcpy(taught[n++], s->topic[t].teach, GG_WORD_MAX);
        }
    }

    // And every flag a quest waits on has to be one something raises - a stage
    // of some quest, or a topic somebody will answer to. Both are sources now
    // that handing a thing over is how the story is settled.
    char raised[GG_FLAGS_MAX][GG_FLAG_MAX];
    int r = 0;
    for (int i = 0; i < gg_quests_count(); i++) {
        const gg_quest *q = gg_quest_at(i);
        for (int k = 0; k < q->stages; k++)
            if (q->stage[k].sets[0] && r < GG_FLAGS_MAX)
                SDL_strlcpy(raised[r++], q->stage[k].sets, GG_FLAG_MAX);
    }
    for (int i = 0; i < gg_dialogue_speakers(); i++) {
        const gg_speaker *sp = gg_dialogue_speaker(i);
        for (int t = 0; t < sp->topics; t++)
            if (sp->topic[t].raises[0] && r < GG_FLAGS_MAX)
                SDL_strlcpy(raised[r++], sp->topic[t].raises, GG_FLAG_MAX);
    }

    for (int i = 0; i < gg_quests_count(); i++) {
        const gg_quest *q = gg_quest_at(i);
        CHECK(q->name[0] != '\0', "quest %d has no name", i);
        CHECK(q->stages > 0, "'%s' has no stages", q->name);
        for (int k = 0; k < q->stages; k++) {
            const gg_stage *s = &q->stage[k];
            CHECK(s->journal[0] != '\0', "stage %d of '%s' says nothing",
                  k + 1, q->name);

            if (s->what == GG_WHEN_KNOWS) {
                bool ok = false;
                for (int t = 0; t < n; t++)
                    if (SDL_strcasecmp(taught[t], s->word) == 0) ok = true;
                CHECK(ok, "'%s' waits on the word %s, which nobody teaches",
                      q->name, s->word);
            }
            if (s->what == GG_WHEN_FLAG) {
                bool ok = false;
                for (int t = 0; t < r; t++)
                    if (SDL_strcasecmp(raised[t], s->word) == 0) ok = true;
                CHECK(ok, "'%s' waits on the flag %s, which nothing raises",
                      q->name, s->word);
            }
            // A stage that waits on a place has to name a map that ships, or
            // it is a stage nobody can ever stand in.
            if (s->what == GG_WHEN_AT) {
                // A place, so either form of the map counts - that is the
                // point of naming places rather than files.
                gg_map m;
                SDL_zero(m);
                bool there = false;
                static const char *const FORMS[] = { "%s.ggmap", "%s.map.txt", "%s" };
                for (size_t f = 0; f < GG_COUNTOF(FORMS) && !there; f++) {
                    char leaf[GG_MAP_NAME_MAX + 16], rel[GG_MAP_NAME_MAX + 24];
                    SDL_snprintf(leaf, sizeof leaf, FORMS[f], s->where);
                    SDL_snprintf(rel, sizeof rel, "maps/%s", leaf);
                    there = gg_map_load(&m, gg_asset_path(rel));
                }
                CHECK(there, "'%s' waits in %s, which is not a map that ships",
                      q->name, s->where);
                if (there) {
                    // And at a tile inside it, or the story asks the player to
                    // stand off the edge of the world.
                    if (s->radius > 0)
                        CHECK(s->wx >= 0 && s->wx < m.w && s->wy >= 0 && s->wy < m.h,
                              "'%s' waits at %d,%d, which is outside %s",
                              q->name, s->wx, s->wy, s->where);
                    gg_map_free(&m);
                }
            }
        }
    }

    restore_quests();
    restore_dialogue();
}

// ---------------------------------------------------------------------------
// Many maps
// ---------------------------------------------------------------------------
// Authors a small map with a way out of it, and saves it. Returns the path.
static const char *author_linked_map(const char *leaf, const char *name,
                                     const char *to, int to_x, int to_y,
                                     int gate_x, int gate_y, const char *who)
{
    const char *path = gg_pref_file(leaf);

    gg_editor e;
    SDL_zero(e);
    CHECK(gg_edit_new(&e, 40, 32), "could not make a map");
    SDL_strlcpy(e.map.name, name, sizeof e.map.name);

    gg_edit_tool(&e, GG_TOOL_START);
    gg_edit_apply(&e, 8, 8);

    if (who) {
        gg_edit_tool(&e, GG_TOOL_ACTOR);
        gg_edit_apply(&e, 12, 8);
        gg_edit_name_actor(&e, who);
        gg_edit_tool(&e, GG_TOOL_SCHEDULE);
        gg_edit_apply(&e, 12, 8);
    }

    gg_edit_tool(&e, GG_TOOL_PORTAL);
    gg_edit_link_to(&e, to, to_x, to_y);
    gg_edit_apply(&e, gate_x, gate_y);
    CHECK(e.map.portals == 1, "%s has %d ways out, expected one", name,
          e.map.portals);

    char problems[GG_EDIT_PROBLEMS_MAX][GG_EDIT_SAY_MAX];
    const int bad = gg_edit_check(&e, problems);
    for (int i = 0; i < bad && i < GG_EDIT_PROBLEMS_MAX; i++)
        CHECK(false, "%s: %s", name, problems[i]);

    CHECK(gg_edit_save(&e, path), "could not save %s: %s", name, e.say);
    gg_edit_close(&e);
    return path;
}

// The plan's own verification: walking between two authored maps preserves
// party, clock and inventory.
static void walking_between_two_maps_takes_everything_with_you(void) {
    // Two maps, each with a gate into the other. Both authored through the
    // editor's own operations, so this is content and not a fixture.
    const char *east = author_linked_map("test_east.ggmap", "The East Field",
                                         "test_west.ggmap", 20, 12,
                                         30, 8, "Eadric");
    char east_path[1024];
    SDL_strlcpy(east_path, east, sizeof east_path);
    const char *west = author_linked_map("test_west.ggmap", "The West Wood",
                                         "test_east.ggmap", 9, 8,
                                         4, 8, "Wystan");
    char west_path[1024];
    SDL_strlcpy(west_path, west, sizeof west_path);

    gg_game g;
    CHECK(gg_game_new_from_map(&g, east_path, "Traveller", 4242), "the east would not open");
    CHECK(SDL_strcmp(g.map.name, "The East Field") == 0,
          "started in '%s'", g.map.name);

    // Something to carry, somebody to walk with, something learned, and a
    // quest under way - everything the crossing has to preserve.
    g.packn = 0;
    for (int s = 0; s < GG_SLOT_COUNT; s++) g.equipped[s] = -1;
    CHECK(gg_pack_add(&g, GG_ITEM_SILVER, 2) == 2, "could not take silver");
    CHECK(gg_pack_add(&g, GG_ITEM_TORCH, 1) == 1, "could not take a torch");
    g.pack_cursor = gg_pack_find(&g, GG_ITEM_TORCH);
    g.mode = GG_MODE_PACK;
    gg_game_act(&g, GG_ACT_EQUIP);
    g.mode = GG_MODE_PLAY;

    int eadric = -1;
    for (int i = 0; i < g.actors; i++)
        if (SDL_strcmp(g.actor[i].name, "Eadric") == 0) eadric = i;
    CHECK(eadric > 0, "Eadric is not in the east field");
    CHECK(gg_party_join(&g, eadric), "Eadric would not come along");

    gg_learn(&g, "somewhere");
    gg_raise_flag(&g, "been_east");
    gg_player(&g)->hp = 21;

    const int carried = gg_pack_count(&g, GG_ITEM_SILVER);
    const int held = gg_light_radius(&g);
    const int party = gg_party_size(&g);
    CHECK(party == 1, "the party is %d, expected one", party);

    // Walk onto the gate. The simulation only *asks* to travel - it names the
    // map and the frontend says where that map lives, which is the whole
    // reason src/core does not know where content is kept.
    gg_actor *p = gg_player(&g);
    p->x = 29;
    p->y = 8;
    gg_game_act(&g, GG_ACT_E);
    CHECK(g.want_travel, "stepping on a way out asked for nothing");
    CHECK(SDL_strcmp(g.travel_to, "test_west.ggmap") == 0,
          "it asks for '%s'", g.travel_to);

    // Snapshot after the step, not before it: walking onto the gate is an
    // ordinary move and costs an ordinary turn. What must not change is what
    // the *crossing* does, which is what these are compared against.
    const uint32_t minutes = g.minutes, day = g.day;
    const uint32_t rng = g.rng.s;
    const uint32_t turn = g.turn;

    CHECK(gg_game_travel(&g, west_path, g.travel_x, g.travel_y),
          "the crossing failed");
    CHECK(!g.want_travel, "the request was not cleared");

    // The world changed.
    CHECK(SDL_strcmp(g.map.name, "The West Wood") == 0,
          "arrived in '%s'", g.map.name);
    CHECK(gg_player_const(&g)->x == 20 && gg_player_const(&g)->y == 12,
          "arrived at %d,%d, not where the gate said",
          gg_player_const(&g)->x, gg_player_const(&g)->y);

    // Everything about the party did not.
    CHECK(gg_pack_count(&g, GG_ITEM_SILVER) == carried,
          "the silver did not come along (%d of %d)",
          gg_pack_count(&g, GG_ITEM_SILVER), carried);
    CHECK(gg_light_radius(&g) == held, "the readied torch was put out");
    CHECK(g.minutes == minutes && g.day == day,
          "the clock jumped to day %u %02u:%02u", g.day, g.minutes / 60,
          g.minutes % 60);
    CHECK(g.rng.s == rng, "the RNG was reseeded by walking through a gate");
    CHECK(g.turn == turn, "the crossing itself cost %u turns", g.turn - turn);
    CHECK(gg_party_size(&g) == party, "the party is %d, was %d",
          gg_party_size(&g), party);
    CHECK(gg_knows(&g, "somewhere"), "what was learned was forgotten");
    CHECK(gg_flag(&g, "been_east"), "a flag was lost in the crossing");
    CHECK(gg_player_const(&g)->hp == 21, "health came back as %d",
          gg_player_const(&g)->hp);

    // The companion came, and stands somewhere real rather than on top of you.
    int companion = -1;
    for (int i = 0; i < g.actors; i++)
        if (SDL_strcmp(g.actor[i].name, "Eadric") == 0) companion = i;
    CHECK(companion > 0, "Eadric did not come through");
    if (companion > 0) {
        CHECK(g.actor[companion].party != GG_NOT_IN_PARTY,
              "Eadric arrived but left the party");
        CHECK(gg_map_walkable(&g.map, g.actor[companion].x, g.actor[companion].y),
              "Eadric arrived inside something");
    }

    // And the new map's own people are here, while the old map's are not.
    int wystans = 0, eadrics = 0;
    for (int i = 0; i < g.actors; i++) {
        if (!g.actor[i].active) continue;
        if (SDL_strcmp(g.actor[i].name, "Wystan") == 0) wystans++;
        if (SDL_strcmp(g.actor[i].name, "Eadric") == 0) eadrics++;
    }
    CHECK(wystans == 1, "the west wood holds %d Wystans", wystans);
    CHECK(eadrics == 1, "there are %d Eadrics - one should have come along and "
          "none should have been left behind", eadrics);

    // And back again, which is the other half of "linked".
    p = gg_player(&g);
    p->x = 5;
    p->y = 8;
    gg_game_act(&g, GG_ACT_W);
    CHECK(g.want_travel, "the way back asked for nothing");
    CHECK(gg_game_travel(&g, east_path, g.travel_x, g.travel_y),
          "the way back failed");
    CHECK(SDL_strcmp(g.map.name, "The East Field") == 0,
          "came back to '%s'", g.map.name);
    CHECK(gg_pack_count(&g, GG_ITEM_SILVER) == carried,
          "the silver was lost on the way back");
    CHECK(gg_party_size(&g) == party, "the party was lost on the way back");

    // And the world still turns where it left off.
    const uint32_t before = g.turn;
    for (int i = 0; i < 10; i++) gg_game_act(&g, GG_ACT_WAIT);
    CHECK(g.turn == before + 10, "the world stopped turning after a crossing");

    gg_game_free(&g);
    SDL_RemovePath(east_path);
    SDL_RemovePath(west_path);
}

// The item's own verification: a map you leave keeps what you did in it.
// ---------------------------------------------------------------------------
// The whole story
// ---------------------------------------------------------------------------
// Stands the Avatar next to somebody and asks them every word they will answer
// to, once each. The real machinery throughout: the conversation is entered by
// facing them and talking, and each word is asked by putting the cursor on it
// and asking - which is what the two keys do.
static void ask_everything(gg_game *g, int who) {
    gg_actor *p = gg_player(g);
    const gg_actor *them = &g->actor[who];

    // Beside them and facing them. A test may put the Avatar down where it
    // likes; what it must not do is invent a conversation that the game would
    // not have started.
    p->x = (int16_t)them->x;
    p->y = (int16_t)(them->y + 1);
    p->from_x = p->x;
    p->from_y = p->y;
    p->facing = GG_FACE_UP;

    gg_game_act(g, GG_ACT_TALK);
    if (g->mode != GG_MODE_CONVERSE) return;

    // Every word once. The list is rebuilt whenever one of them teaches
    // something, so this walks it by index and re-reads the count each time.
    for (int i = 0; i < GG_TOPICS_MAX && i < g->askables; i++) {
        g->ask_cursor = i;
        gg_conversation_ask(g);
        if (g->mode != GG_MODE_CONVERSE) return;   // the story ended mid-word
    }
    if (g->mode == GG_MODE_CONVERSE) gg_game_act(g, GG_ACT_WAIT);
}

// Asks the whole town, twice over: what Nystul teaches is what makes Gwenno
// worth talking to, so one sweep is not enough for a rumour to cross a square.
static void ask_the_town(gg_game *g) {
    for (int sweep = 0; sweep < 3; sweep++)
        for (int i = 0; i < g->actors; i++) {
            if (i == g->player || !g->actor[i].active || g->actor[i].hostile)
                continue;
            if (!gg_dialogue_find(g->actor[i].name)) continue;
            ask_everything(g, i);
            if (g->mode == GG_MODE_ENDING) return;
        }
}

// "Playable" has to mean winnable, not merely reachable: a story that walks the
// player to a man who kills them every time is a story with no ending in it.
//
// So this fights the fight, twenty times over on twenty seeds, with what the
// road actually hands you - a hammer off a brigand, a shield off an outlaw,
// and the two companions the vale offers - and counts how often the party is
// left standing. Every number in it is a line in bestiary.txt.
// Fights Rugar twenty times over on twenty seeds and returns how many the
// party is left standing after. `prepared` is the difference between a player
// who fought their way up the north road - a hammer off a brigand, a shield
// off an outlaw, and the two companions the vale offers - and one who ran
// past everything.
static int fights_won_against_rugar(bool prepared) {
    int won = 0;
    for (uint32_t seed = 1; seed <= 20; seed++) {
        gg_game g;
        CHECK(gg_game_new_from_map(&g, gg_asset_path("maps/vale.map.txt"),
                                   "Fighter", 4242), "the vale would not open");

        if (prepared) {
            // Recruited the way a player recruits: by asking everybody in the
            // vale everything, which is what makes the two with a `joins`
            // topic come along.
            ask_the_town(&g);
            CHECK(gg_party_size(&g) == 2, "the vale sent %d along, expected two",
                  gg_party_size(&g));

            // And armed the way a player is armed: with two things a brigand
            // and an outlaw drop, put in the pack and readied through the
            // pack's own action rather than granted as a stat.
            gg_pack_add(&g, GG_ITEM_HAMMER, 1);
            gg_pack_add(&g, GG_ITEM_SHIELD, 1);
            g.pack_cursor = gg_pack_find(&g, GG_ITEM_HAMMER);
            g.mode = GG_MODE_PACK;
            gg_game_act(&g, GG_ACT_EQUIP);
            g.pack_cursor = gg_pack_find(&g, GG_ITEM_SHIELD);
            gg_game_act(&g, GG_ACT_EQUIP);
            g.mode = GG_MODE_PLAY;
            CHECK(gg_attack_power(&g, g.player) >= 5,
                  "the hammer is not in hand (attack %d)",
                  gg_attack_power(&g, g.player));
            CHECK(gg_guard_power(&g, g.player) >= 3,
                  "the shield is not on the arm (guard %d)",
                  gg_guard_power(&g, g.player));
        }

        // Up the road, through the real way out.
        gg_actor *p = gg_player(&g);
        p->x = (int16_t)g.map.portal[0].x;
        p->y = (int16_t)(g.map.portal[0].y + 1);
        p->from_x = p->x;
        p->from_y = p->y;
        gg_game_act(&g, GG_ACT_N);
        CHECK(gg_game_travel(&g, gg_asset_path("maps/stones.map.txt"),
                             g.travel_x, g.travel_y), "the crossing failed");

        // A seed apiece, so twenty fights are twenty fights and not one fight
        // twenty times.
        gg_rng_seed(&g.rng, seed * 7919u);

        int rugar = -1;
        for (int i = 0; i < g.actors; i++)
            if (g.actor[i].active && g.actor[i].hostile) rugar = i;
        CHECK(rugar > 0, "Rugar is not in the ring");
        if (rugar < 0) { gg_game_free(&g); break; }

        // Walk at him and keep walking: bumping an enemy is a blow, so this is
        // a player who closes and fights, which is the fight the story sets up.
        // Through the pathfinder, because the ring of stones is between them.
        int turns = 0;
        while (turns < 400 && g.mode == GG_MODE_PLAY && g.actor[rugar].active) {
            const gg_actor *me = gg_player_const(&g);
            int nx = 0, ny = 0;
            if (!gg_step_toward(&g, g.player, g.actor[rugar].x,
                                g.actor[rugar].y, &nx, &ny)) break;
            gg_game_act(&g, gg_action_toward(nx - me->x, ny - me->y));
            turns++;
        }
        CHECK(turns < 400, "a fight ran to the bound without ending");

        if (!g.actor[rugar].active && g.mode != GG_MODE_GAMEOVER) won++;
        gg_game_free(&g);
    }
    return won;
}

// "Playable" has to mean winnable, not merely reachable: a story that walks the
// player up to a man who kills them every time is a story with no ending in it.
// And it has to mean *earned*, or the road between is decoration.
//
// Both halves are measured rather than argued, on the shipped content, and
// every number they depend on is a line in assets/bestiary.txt.
static void a_party_that_walked_the_road_can_beat_the_man_at_the_end_of_it(void) {
    CHECK(gg_bestiary_load(gg_asset_path("bestiary.txt")), "no bestiary");
    CHECK(gg_dialogue_load(gg_asset_path("dialogue.txt")), "no book");

    const int prepared = fights_won_against_rugar(true);
    const int alone    = fights_won_against_rugar(false);

    // Reported whether or not they pass: these are the two numbers that say
    // whether the story is beatable and whether beating it takes anything, and
    // a green tick that hides them is not much use to whoever next changes a
    // line in the bestiary.
    SDL_Log("gigantima: Rugar loses %d of 20 to a party that came prepared, "
            "and %d of 20 to one that did not", prepared, alone);

    // Not "always" either. A fight nobody can lose is not a fight, and the
    // first thing levelling did was turn this one into 20 of 20 without a line
    // of the bestiary changing - which is the sort of thing a one-sided check
    // lets through.
    CHECK(prepared <= 18, "a prepared party won %d of 20; the man at the end of "
          "the road is not a fight", prepared);

    // Most of the time, not always: a fight nobody can lose is not a fight.
    CHECK(prepared >= 14, "a prepared party won only %d of 20; the man at the "
          "end of the road is too strong", prepared);
    // And walking up unarmed and alone should not be the same story.
    CHECK(alone <= prepared - 4, "coming prepared is worth %d fights of 20, "
          "which is not enough for the road between to mean anything",
          prepared - alone);

    restore_bestiary();
    restore_dialogue();
}

// ---------------------------------------------------------------------------
// Levelling
// ---------------------------------------------------------------------------
// Experience was counted, saved, and spent on nothing: the Avatar ended the
// story at level one. This is what it buys now.
static void what_the_party_learns_makes_it_stronger(void) {
    gg_game g;
    CHECK(gg_game_new(&g, 77, "Learner"), "new game failed");

    const gg_actor *me = gg_player_const(&g);
    CHECK(me->level == 1, "a journey begins at level %u", me->level);
    CHECK(g.exp == 0, "a journey begins with %d experience", g.exp);

    // The thresholds are arithmetic a player can feel the shape of: 100 to
    // reach 2, another 200 to reach 3, another 300 to reach 4.
    CHECK(gg_level_cost(1) == 0, "level one costs something");
    CHECK(gg_level_cost(2) == 100, "level two costs %d", gg_level_cost(2));
    CHECK(gg_level_cost(3) == 300, "level three costs %d", gg_level_cost(3));
    CHECK(gg_level_cost(4) == 600, "level four costs %d", gg_level_cost(4));

    // Not enough is not enough.
    const int was_max = me->hp_max;
    CHECK(gg_gain(&g, 99) == 0, "99 was enough for a level");
    CHECK(gg_player_const(&g)->level == 1, "levelled on 99");
    CHECK(gg_player_const(&g)->hp_max == was_max, "health rose without a level");

    // And one more is.
    gg_player(&g)->hp = 5;
    CHECK(gg_gain(&g, 1) == 1, "100 was not enough for a level");
    CHECK(gg_player_const(&g)->level == 2, "level is %u after 100",
          gg_player_const(&g)->level);
    CHECK(gg_player_const(&g)->hp_max == was_max + GG_LEVEL_HEALTH,
          "health went from %d to %d", was_max, gg_player_const(&g)->hp_max);
    CHECK(gg_player_const(&g)->hp == 5 + GG_LEVEL_HEALTH,
          "a level healed by %d, not %d", gg_player_const(&g)->hp - 5,
          GG_LEVEL_HEALTH);

    // Enough at once for two levels is two levels, not one.
    const int before = gg_player_const(&g)->level;
    CHECK(gg_gain(&g, 1000) >= 2, "a windfall bought fewer than two levels");
    CHECK(gg_player_const(&g)->level > before + 1, "level is %u, was %u",
          gg_player_const(&g)->level, before);

    // And it stops at the top rather than running away.
    gg_gain(&g, 1000000);
    CHECK(gg_player_const(&g)->level == GG_LEVEL_MAX, "level ran to %u",
          gg_player_const(&g)->level);

    gg_game_free(&g);
}

// Everybody walking with the Avatar rises together. A companion who fell behind
// is a companion you stop bringing, and this game is about bringing people.
static void a_companion_rises_with_the_avatar(void) {
    CHECK(gg_dialogue_load(gg_asset_path("dialogue.txt")), "no book");

    gg_game g;
    CHECK(gg_game_new_from_map(&g, gg_asset_path("maps/vale.map.txt"), "Party", 4242),
          "the vale would not open");
    ask_the_town(&g);
    CHECK(gg_party_size(&g) == 2, "the vale sent %d along", gg_party_size(&g));

    int who = gg_party_at(&g, 1);
    CHECK(who > 0, "nobody is in the first slot");
    const int was_level = g.actor[who].level;
    const int was_max = g.actor[who].hp_max;

    // Somebody who is *not* walking with the party, to show that this is the
    // party's and not everybody's.
    int stranger = -1;
    for (int i = 0; i < g.actors; i++)
        if (i != g.player && g.actor[i].active &&
            g.actor[i].party == GG_NOT_IN_PARTY) stranger = i;
    CHECK(stranger > 0, "nobody in the vale is outside the party");
    const int stranger_max = stranger > 0 ? g.actor[stranger].hp_max : 0;

    CHECK(gg_gain(&g, gg_level_cost(3)) == 2, "that should have been two levels");

    CHECK(g.actor[who].level == gg_player_const(&g)->level,
          "the companion is level %u and the Avatar %u", g.actor[who].level,
          gg_player_const(&g)->level);
    CHECK(g.actor[who].level > was_level, "the companion did not rise at all");
    CHECK(g.actor[who].hp_max == was_max + 2 * GG_LEVEL_HEALTH,
          "the companion's health went from %d to %d", was_max,
          g.actor[who].hp_max);
    if (stranger > 0)
        CHECK(g.actor[stranger].hp_max == stranger_max,
              "somebody who is not in the party levelled too");

    // And somebody recruited *after* all that arrives able to keep up, rather
    // than being permanently behind the party they just joined.
    if (stranger > 0 && gg_party_size(&g) < GG_PARTY_MAX) {
        const int was = g.actor[stranger].hp_max;
        CHECK(gg_party_join(&g, stranger), "the stranger would not come along");
        CHECK(g.actor[stranger].level == gg_player_const(&g)->level,
              "a late companion is level %u against the party's %u",
              g.actor[stranger].level, gg_player_const(&g)->level);
        CHECK(g.actor[stranger].hp_max > was,
              "a late companion caught up in level but not in health");
    }

    gg_game_free(&g);
    restore_dialogue();
}

// The whole of it, on the shipped content: killing things and working the story
// out both teach, and a journey that did them is stronger than one that did
// not - which is the item's own verification.
static void a_journey_that_fights_its_way_north_arrives_stronger(void) {
    CHECK(gg_bestiary_load(gg_asset_path("bestiary.txt")), "no bestiary");
    CHECK(gg_dialogue_load(gg_asset_path("dialogue.txt")), "no book");
    CHECK(gg_quests_load(gg_asset_path("quests.txt")), "no quests");

    // Every creature is worth something, and says so out of the file.
    for (int i = 0; i < gg_bestiary_count(); i++) {
        const gg_beast *b = gg_bestiary_at(i);
        CHECK(b->worth > 0, "%s teaches nothing", b->id);
    }

    gg_game g;
    CHECK(gg_game_new_from_map(&g, gg_asset_path("maps/vale.map.txt"), "Fighter", 4242),
          "the vale would not open");

    const int start_level = gg_player_const(&g)->level;
    const int start_max = gg_player_const(&g)->hp_max;

    // Asking the vale what is wrong is worth something on its own.
    ask_the_town(&g);
    gg_quests_tick(&g);
    CHECK(g.exp > 0, "working the story out taught the party nothing");
    const int by_talking = g.exp;

    // What a *kill* teaches, on its own and to the number. With the quests
    // cleared, because killing things also advances the brigand quest, whose
    // stages teach as well - so "experience went up after a fight" passes
    // whether or not a kill is worth anything, which is how the first version
    // of this test passed with the reward removed.
    {
        gg_quests_clear();
        gg_game q;
        CHECK(gg_game_new(&q, 5, "Killer"), "new game failed");
        q.exp = 0;

        const int kind = gg_bestiary_find("BRIGAND");
        CHECK(kind >= 0, "there is no brigand");
        const int worth = kind >= 0 ? gg_bestiary_at(kind)->worth : 0;

        const gg_actor *p = gg_player_const(&q);
        const int foe = gg_spawn_foe(&q, kind, p->x + 1, p->y);
        CHECK(foe > 0, "no brigand could be placed");
        if (foe > 0) {
            gg_player(&q)->level = 20;
            for (int i = 0; i < 400 && q.actor[foe].active; i++) {
                gg_actor *me = gg_player(&q);
                me->hp = me->hp_max;
                me->x = (int16_t)(q.actor[foe].x - 1);
                me->y = q.actor[foe].y;
                me->facing = GG_FACE_RIGHT;
                gg_game_act(&q, GG_ACT_FIGHT);
            }
            CHECK(!q.actor[foe].active, "the brigand would not fall");
            CHECK(q.exp == worth, "killing a brigand taught %d, and it is "
                  "worth %d", q.exp, worth);
        }
        gg_game_free(&q);
        CHECK(gg_quests_load(gg_asset_path("quests.txt")), "no quests");
    }

    // And then the road. Everything hostile in the vale, killed properly.
    gg_player(&g)->level = 20;              // so this ends inside the bound
    for (int i = 0; i < g.actors; i++) {
        if (!g.actor[i].active || !g.actor[i].hostile) continue;
        for (int swing = 0; swing < 400 && g.actor[i].active; swing++) {
            gg_actor *me = gg_player(&g);
            me->hp = me->hp_max;
            me->x = g.actor[i].x;
            me->y = (int16_t)(g.actor[i].y + 1);
            me->facing = GG_FACE_UP;
            gg_game_act(&g, GG_ACT_FIGHT);
        }
    }
    CHECK(g.exp > by_talking, "killing everything in the vale taught nothing");
    CHECK(g.slain > 0, "nothing was killed");

    // The level was forced up for the fighting, so what is checked here is the
    // experience and the health it bought - which is what the player feels.
    CHECK(gg_player_const(&g)->hp_max > start_max,
          "a journey that fought its way north is no harder to kill (%d, was %d)",
          gg_player_const(&g)->hp_max, start_max);
    CHECK(gg_player_const(&g)->level > start_level, "still level %d",
          gg_player_const(&g)->level);

    // And it survives being put down.
    const char *who = "Fighter";
    wipe_saves(who);
    CHECK(gg_save_write(&g, save_base(), who), "the save failed");
    gg_game back;
    SDL_zero(back);
    CHECK(gg_save_read(&back, save_base(), who), "the load failed");
    CHECK(back.exp == g.exp, "experience came back as %d, not %d", back.exp, g.exp);
    CHECK(gg_player_const(&back)->level == gg_player_const(&g)->level,
          "level came back as %u", gg_player_const(&back)->level);
    CHECK(gg_player_const(&back)->hp_max == gg_player_const(&g)->hp_max,
          "health came back as %d", gg_player_const(&back)->hp_max);
    gg_game_free(&back);
    wipe_saves(who);

    gg_game_free(&g);
    restore_bestiary();
    restore_dialogue();
    restore_quests();
}

// A map is drawn by hand; what is *in* it is rolled. Two journeys through the
// same vale should not be the same journey - and the same seed should still
// give back the same one, or a bug report that names a seed is worthless.
static void two_journeys_through_one_map_are_not_the_same_journey(void) {
    CHECK(gg_bestiary_load(gg_asset_path("bestiary.txt")), "no bestiary");
    CHECK(gg_dialogue_load(gg_asset_path("dialogue.txt")), "no book");

    // Where trouble is, in a world built from `seed`.
    #define TROUBLE(seed, into) do {                                          \
        gg_game w;                                                            \
        CHECK(gg_game_new_from_map(&w, gg_asset_path("maps/vale.map.txt"),      \
                                   "Rolls", (seed)), "the vale would not open"); \
        (into) = 0;                                                           \
        for (int i = 0; i < w.actors; i++) {                                  \
            if (!w.actor[i].active || !w.actor[i].hostile) continue;          \
            (into) = (into) * 31 + w.actor[i].x * 1000 + w.actor[i].y;        \
        }                                                                     \
        CHECK((into) != 0, "seed %u put nothing hostile in the vale", (seed)); \
        gg_game_free(&w);                                                     \
    } while (0)

    long a = 0, b = 0, again = 0;
    TROUBLE(11u, a);
    TROUBLE(12u, b);
    TROUBLE(11u, again);

    CHECK(a != b, "two seeds put the trouble in exactly the same places - "
                  "every journey through this map is the same journey");
    CHECK(a == again, "the same seed gave a different world the second time");

    #undef TROUBLE

    // And the world remembers which seed it was, so a bug report can name it
    // and a replay can rebuild it.
    gg_game g;
    CHECK(gg_game_new_from_map(&g, gg_asset_path("maps/vale.map.txt"), "Rolls", 909u),
          "the vale would not open");
    CHECK(g.map.seed == 909u, "the world says it was seeded %u", g.map.seed);

    // Through a save, too - resuming must not reroll anything.
    const char *who = "Rolls";
    wipe_saves(who);
    CHECK(gg_save_write(&g, save_base(), who), "the save failed");
    gg_game back;
    SDL_zero(back);
    CHECK(gg_save_read(&back, save_base(), who), "the load failed");
    CHECK(back.map.seed == 909u, "a resumed world says it was seeded %u",
          back.map.seed);
    CHECK(back.rng.s == g.rng.s, "a resumed world's dice are somewhere else");
    gg_game_free(&back);
    wipe_saves(who);

    gg_game_free(&g);
    restore_bestiary();
    restore_dialogue();
}

// Writes a text map where the tests can reach it.
static const char *write_text_map(const char *text) {
    const char *path = gg_pref_file("test_hand.map.txt");
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    CHECK(io != nullptr, "could not write a map file");
    if (io) {
        const size_t n = SDL_strlen(text);
        CHECK(SDL_WriteIO(io, text, n) == n, "short write on the map file");
        SDL_CloseIO(io);
    }
    return path;
}

// ---------------------------------------------------------------------------
// A map as text
// ---------------------------------------------------------------------------
// The binary map was the only form a map could have, and it cost two things: a
// map could only be authored by a person with a mouse, and a map in the
// repository was a blob that a format change would strand. This is the same map
// as something anybody can read, and the claim is that it is the *same* map.
static void a_map_written_as_text_is_the_same_map(void) {
    static const char *const SHIPPED[] = {
        "maps/vale.map.txt", "maps/stones.map.txt", "maps/fells.map.txt",
        "maps/deep.map.txt", "maps/wyndle.map.txt",
    };

    for (size_t i = 0; i < GG_COUNTOF(SHIPPED); i++) {
        gg_map from;
        SDL_zero(from);
        CHECK(gg_map_load(&from, gg_asset_path(SHIPPED[i])), "%s would not load",
              SHIPPED[i]);
        if (!from.cell) continue;

        char text[1024];
        SDL_strlcpy(text, gg_pref_file("test_map.map.txt"), sizeof text);
        CHECK(gg_map_write_text(&from, text), "%s would not write as text",
              SHIPPED[i]);

        gg_map back;
        SDL_zero(back);
        CHECK(gg_map_read_text(&back, text), "%s would not read back",
              SHIPPED[i]);

        // The same map, field for field.
        CHECK(back.w == from.w && back.h == from.h, "%s came back %dx%d",
              SHIPPED[i], back.w, back.h);
        CHECK(SDL_strcmp(back.name, from.name) == 0, "%s came back called '%s'",
              SHIPPED[i], back.name);
        CHECK(back.seed == from.seed, "%s came back seeded %u", SHIPPED[i],
              back.seed);
        CHECK(back.start_x == from.start_x && back.start_y == from.start_y,
              "%s starts at %d,%d now", SHIPPED[i], back.start_x, back.start_y);
        CHECK(SDL_memcmp(back.cell, from.cell,
                         (size_t)from.w * (size_t)from.h * sizeof *from.cell) == 0,
              "%s came back with different ground", SHIPPED[i]);
        CHECK(back.regions == from.regions, "%s has %d regions now", SHIPPED[i],
              back.regions);
        CHECK(back.grounds == from.grounds, "%s has %d piles now", SHIPPED[i],
              back.grounds);
        CHECK(back.actors == from.actors, "%s has %d people now", SHIPPED[i],
              back.actors);
        CHECK(back.portals == from.portals, "%s has %d ways out now", SHIPPED[i],
              back.portals);

        for (int k = 0; k < from.actors && k < back.actors; k++) {
            CHECK(SDL_strcmp(back.actor[k].name, from.actor[k].name) == 0,
                  "a person came back called '%s'", back.actor[k].name);
            CHECK(back.actor[k].schedn == from.actor[k].schedn,
                  "%s came back with %u hours", back.actor[k].name,
                  back.actor[k].schedn);
        }

        // And the whole thing, byte for byte, through the binary writer - which
        // is the only comparison that leaves nothing to argue about.
        char one[1024], two[1024];
        SDL_strlcpy(one, gg_pref_file("test_rt_a.ggmap"), sizeof one);
        SDL_strlcpy(two, gg_pref_file("test_rt_b.ggmap"), sizeof two);
        CHECK(gg_map_save(&from, one) && gg_map_save(&back, two),
              "could not write %s out again", SHIPPED[i]);

        size_t na = 0, nb = 0;
        void *a = SDL_LoadFile(one, &na);
        void *b = SDL_LoadFile(two, &nb);
        CHECK(a && b, "could not read the two back");
        CHECK(na == nb, "%s is %zu bytes and its round trip is %zu", SHIPPED[i],
              na, nb);
        if (a && b && na == nb)
            CHECK(SDL_memcmp(a, b, na) == 0,
                  "%s does not survive being written as text and read back",
                  SHIPPED[i]);
        SDL_free(a);
        SDL_free(b);
        SDL_RemovePath(one);
        SDL_RemovePath(two);
        SDL_RemovePath(text);

        gg_map_free(&back);
        gg_map_free(&from);
    }
}

// A map that never was a binary: written by hand, and played.
static void a_map_written_by_hand_is_playable(void) {
    const char *text = write_text_map(
        "map 12 8\n"
        "name Handwritten\n"
        "seed 3\n"
        "start 2 2\n"
        "\n"
        "legend . GRASS\n"
        "legend # MOUNTAIN BLOCKED\n"
        "legend , ROAD\n"
        "\n"
        "row ############\n"
        "row #..........#\n"
        "row #..,,,,....#\n"
        "row #..,....,..#\n"
        "row #..,,,,,,..#\n"
        "row #..........#\n"
        "row #..........#\n"
        "row ############\n"
        "\n"
        "region TOWN 1 1 10 6 Wyndle\n"
        "item GOLD 7 4 4\n"
        "person ELDER 5 3 Wilkin\n"
        "  at 06 5 3\n"
        "  at 18 6 4\n"
        "portal 10 6 40 8 vale.ggmap\n");

    gg_map m;
    SDL_zero(m);
    CHECK(gg_map_read_text(&m, text), "the handwritten map would not load");
    CHECK(m.w == 12 && m.h == 8, "it came out %dx%d", m.w, m.h);
    CHECK(SDL_strcmp(m.name, "Handwritten") == 0, "it is called '%s'", m.name);

    // The picture is the ground: the border is wall and the middle is not.
    CHECK(!gg_map_walkable(&m, 0, 0), "the wall in the picture is not a wall");
    CHECK(gg_map_walkable(&m, 2, 2), "the open ground in the picture is not open");
    const gg_cell *road = gg_map_at_const(&m, 3, 2);
    CHECK(road && road->terrain == GG_TILE_ROAD, "the road is not a road");

    CHECK(m.regions == 1 && m.grounds == 1 && m.actors == 1 && m.portals == 1,
          "it came out with %d regions, %d piles, %d people and %d ways out",
          m.regions, m.grounds, m.actors, m.portals);
    CHECK(m.actor[0].schedn == 2, "Wilkin has %u hours", m.actor[0].schedn);
    gg_map_free(&m);

    // And the game plays it, which is the whole point of a map.
    gg_game g;
    CHECK(gg_game_new_from_map(&g, text, "Handwriter", 9), "the game would not "
          "open a map that was never a binary");
    CHECK(SDL_strcmp(g.map.name, "Handwritten") == 0, "it opened '%s'",
          g.map.name);
    CHECK(gg_player_const(&g)->x == 2 && gg_player_const(&g)->y == 2,
          "the Avatar began at %d,%d", gg_player_const(&g)->x,
          gg_player_const(&g)->y);
    int wilkins = 0;
    for (int i = 0; i < g.actors; i++)
        if (SDL_strcmp(g.actor[i].name, "Wilkin") == 0) wilkins++;
    CHECK(wilkins == 1, "the map holds %d Wilkins", wilkins);
    for (int t = 0; t < 30; t++) gg_game_act(&g, GG_ACT_WAIT);
    CHECK(g.turn == 30, "the handwritten world stopped at turn %u", g.turn);
    gg_game_free(&g);

    SDL_RemovePath(text);
}

// Nine ways for a text map to be wrong, each refused with the line it is on.
static void a_map_that_does_not_parse_loads_nothing(void) {
    static const char *const BAD[] = {
        "name Nowhere\nrow ..\n",                       // rows before the size
        "map 4 2\nlegend . GRASS\nrow ....\n",          // too few rows
        "map 4 2\nlegend . GRASS\nrow ....\nrow ....\nrow ....\n",  // too many
        "map 4 2\nlegend . GRASS\nrow ...\nrow ....\n", // a row of the wrong width
        "map 4 2\nlegend . GRASS\nrow ..x.\nrow ....\n", // a character with no legend
        "map 4 2\nlegend . NOTHING\nrow ....\nrow ....\n",  // no such ground
        "map 4 2\nlegend . GRASS FLYING\nrow ....\nrow ....\n",  // no such flag
        "map 4 2\nlegend . GRASS\nrow ....\nrow ....\nprop NOTHING 1 1\n",
        "map 4 2\nlegend . GRASS\nrow ....\nrow ....\nperson NOBODY 1 1 Nobody\n",
    };

    for (size_t i = 0; i < GG_COUNTOF(BAD); i++) {
        const char *path = write_text_map(BAD[i]);
        gg_map m;
        SDL_zero(m);
        CHECK(!gg_map_read_text(&m, path), "malformed map %zu was accepted", i);
        CHECK(m.cell == nullptr, "malformed map %zu left a map behind", i);
        SDL_RemovePath(path);
    }
}

// Where a map of that name lives, in either form. The frontend has the same
// rule; a test that hard-coded one extension would stop noticing the other.
static const char *gg_asset_path_for_place(const char *place) {
    static char buf[1024];
    char bare[GG_MAP_NAME_MAX];
    SDL_strlcpy(bare, place, sizeof bare);
    char *dot = SDL_strchr(bare, '.');
    if (dot) *dot = '\0';

    static const char *const FORMS[] = { "maps/%s.ggmap", "maps/%s.map.txt" };
    for (size_t f = 0; f < GG_COUNTOF(FORMS); f++) {
        char rel[GG_MAP_NAME_MAX + 24];
        SDL_snprintf(rel, sizeof rel, FORMS[f], bare);
        SDL_strlcpy(buf, gg_asset_path(rel), sizeof buf);
        SDL_IOStream *io = SDL_IOFromFile(buf, "rb");
        if (io) { SDL_CloseIO(io); return buf; }
    }
    return buf;
}

// ---------------------------------------------------------------------------
// The world, as a whole
// ---------------------------------------------------------------------------
// Every map that ships, and every way between them. A world of five places is
// only a world if you can get from any of them to the rest and back, and if
// each is somewhere rather than an empty field.
static void every_shipped_map_is_a_place_you_can_walk_to(void) {
    CHECK(gg_bestiary_load(gg_asset_path("bestiary.txt")), "no bestiary");
    CHECK(gg_dialogue_load(gg_asset_path("dialogue.txt")), "no book");

    static const char *const PLACES[] = { "vale", "stones", "fells", "deep", "wyndle" };

    // Which places lead where, so the links can be checked both ways round.
    bool leads[GG_COUNTOF(PLACES)][GG_COUNTOF(PLACES)];
    SDL_zeroa(leads);

    for (size_t i = 0; i < GG_COUNTOF(PLACES); i++) {
        gg_game g;
        CHECK(gg_game_new_from_map(&g, gg_asset_path_for_place(PLACES[i]),
                                   "Walker", 31u + (uint32_t)i),
              "%s would not open", PLACES[i]);
        if (!g.map.cell) continue;

        CHECK(g.map.name[0] != '\0', "%s has no name", PLACES[i]);
        CHECK(gg_map_walkable(&g.map, g.map.start_x, g.map.start_y),
              "%s starts inside something at %d,%d", PLACES[i], g.map.start_x,
              g.map.start_y);
        CHECK(g.map.regions > 0, "%s is in no region, so it has no name to show",
              PLACES[i]);

        // Somebody or something is in it: a place with nothing in it is a
        // field. A town has people; the country has trouble.
        int people = 0, trouble = 0;
        for (int k = 0; k < g.actors; k++) {
            if (k == g.player || !g.actor[k].active) continue;
            if (g.actor[k].hostile) trouble++;
            else people++;
        }
        CHECK(people + trouble > 0, "%s holds nobody at all", PLACES[i]);

        // Every way out leads somewhere that exists, and lands on ground you
        // can stand on.
        CHECK(g.map.portals > 0, "%s has no way out of it", PLACES[i]);
        for (int k = 0; k < g.map.portals; k++) {
            const gg_portal *w = &g.map.portal[k];
            CHECK(gg_map_walkable(&g.map, w->x, w->y),
                  "%s has a way out at %d,%d that cannot be stood on",
                  PLACES[i], w->x, w->y);

            gg_map to;
            SDL_zero(to);
            const bool there = gg_map_load(&to, gg_asset_path_for_place(w->to));
            CHECK(there, "%s leads to %s, which is not a map that ships",
                  PLACES[i], w->to);
            if (!there) continue;
            CHECK(gg_map_walkable(&to, w->to_x, w->to_y),
                  "%s lands in %s at %d,%d, which cannot be stood on",
                  PLACES[i], w->to, w->to_x, w->to_y);
            gg_map_free(&to);

            for (size_t t = 0; t < GG_COUNTOF(PLACES); t++)
                if (SDL_strcasecmp(PLACES[t], w->to) == 0) leads[i][t] = true;
        }
        gg_game_free(&g);
    }

    // Both ways round. A door you can only go through one way is a trap, and
    // the one time this game does that on purpose it will say so here.
    for (size_t i = 0; i < GG_COUNTOF(PLACES); i++)
        for (size_t k = 0; k < GG_COUNTOF(PLACES); k++)
            if (leads[i][k])
                CHECK(leads[k][i], "%s leads to %s and %s does not lead back",
                      PLACES[i], PLACES[k], PLACES[k]);

    // And the whole of it hangs together: every place reachable from the vale,
    // walking only through ways out.
    bool seen[GG_COUNTOF(PLACES)];
    SDL_zeroa(seen);
    seen[0] = true;
    for (size_t pass = 0; pass < GG_COUNTOF(PLACES); pass++)
        for (size_t i = 0; i < GG_COUNTOF(PLACES); i++)
            if (seen[i])
                for (size_t k = 0; k < GG_COUNTOF(PLACES); k++)
                    if (leads[i][k]) seen[k] = true;
    for (size_t i = 0; i < GG_COUNTOF(PLACES); i++)
        CHECK(seen[i], "%s cannot be reached from the vale at all", PLACES[i]);

    restore_bestiary();
    restore_dialogue();
}

// The country is stocked with what the bestiary says haunts it, and a town is
// not stocked with anything that would eat the townsfolk.
static void each_place_holds_what_the_bestiary_says_it_does(void) {
    CHECK(gg_bestiary_load(gg_asset_path("bestiary.txt")), "no bestiary");
    CHECK(gg_dialogue_load(gg_asset_path("dialogue.txt")), "no book");

    static const struct { const char *place; const char *kind; bool people; }
    WANT[] = {
        { "fells",  "HILLMAN", false },
        { "deep",   "LURKER",  false },
        { "wyndle", nullptr,   true  },
    };

    for (size_t i = 0; i < GG_COUNTOF(WANT); i++) {
        gg_game g;
        CHECK(gg_game_new_from_map(&g, gg_asset_path_for_place(WANT[i].place),
                                   "Stocked", 55u), "%s would not open",
              WANT[i].place);
        if (!g.map.cell) continue;

        int of_kind = 0, people = 0, hostile = 0;
        for (int k = 0; k < g.actors; k++) {
            if (k == g.player || !g.actor[k].active) continue;
            if (g.actor[k].hostile) hostile++; else people++;
            const gg_beast *b = gg_bestiary_at(g.actor[k].beast);
            if (WANT[i].kind && b && g.actor[k].hostile &&
                SDL_strcasecmp(b->id, WANT[i].kind) == 0) of_kind++;
        }

        if (WANT[i].kind) {
            CHECK(of_kind > 0, "%s holds none of what the bestiary says haunts "
                  "it (%s)", WANT[i].place, WANT[i].kind);
            CHECK(people == 0, "%s holds %d people, and nobody lives there",
                  WANT[i].place, people);
        }
        if (WANT[i].people) {
            CHECK(people > 0, "%s is a town with nobody in it", WANT[i].place);
            CHECK(hostile == 0, "%s is a town with %d hostiles standing in it",
                  WANT[i].place, hostile);
        }
        gg_game_free(&g);
    }

    restore_bestiary();
    restore_dialogue();
}

// The plan's own verification for the storyline: playable start to finish.
//
// Every step is the game's own - words are learned by asking, the crossing is
// the crossing, the fight is the fight and the ending is a topic handed over.
// The Avatar is teleported between them, because walking forty tiles is
// already pinned by its own tests and doing it again here would only make this
// slow and flaky.
static void the_whole_story_can_be_played_from_start_to_finish(void) {
    CHECK(gg_dialogue_load(gg_asset_path("dialogue.txt")), "no book");
    CHECK(gg_quests_load(gg_asset_path("quests.txt")), "no quests");
    CHECK(gg_bestiary_load(gg_asset_path("bestiary.txt")), "no bestiary");

    gg_game g;
    CHECK(gg_game_new_from_map(&g, gg_asset_path("maps/vale.map.txt"), "Hero", 4242),
          "the vale would not open");

    const int caravan = gg_quest_find("CARAVAN");
    CHECK(caravan >= 0, "there is no caravan quest");
    if (caravan < 0) { gg_game_free(&g); return; }
    CHECK(g.quest[caravan] == 0, "the story had begun before it began");

    // --- the town ---------------------------------------------------------
    ask_the_town(&g);
    gg_quests_tick(&g);

    CHECK(gg_knows(&g, "caravan"), "nobody in the vale mentioned the caravan");
    CHECK(gg_knows(&g, "north"), "the road north was never named");
    CHECK(gg_knows(&g, "silver"), "nobody said what to look for");
    CHECK(gg_flag(&g, "caravan_understood"),
          "asking the whole town did not add up to a reason to leave");
    CHECK(gg_party_size(&g) > 0, "nobody in the vale would come along");

    // The vale arms whoever asks: a hammer from Iolo and a shield from the
    // gate. One each, though the whole town was asked three times over - a
    // topic that gives is not a purse that never empties.
    CHECK(gg_pack_count(&g, GG_ITEM_HAMMER) == 1,
          "asking around produced %d hammers",
          gg_pack_count(&g, GG_ITEM_HAMMER));
    CHECK(gg_pack_count(&g, GG_ITEM_SHIELD) == 1,
          "asking around produced %d shields",
          gg_pack_count(&g, GG_ITEM_SHIELD));

    // Standing in the vale is not standing at the stones. Without this the
    // rest of the story would advance while the Avatar is still in the market.
    CHECK(!gg_flag(&g, "stood_among_the_stones"),
          "the stones were reached without leaving the vale");

    // --- the road north ---------------------------------------------------
    CHECK(g.map.portals > 0, "the vale has no way out");
    gg_actor *p = gg_player(&g);
    p->x = (int16_t)g.map.portal[0].x;
    p->y = (int16_t)(g.map.portal[0].y + 1);
    p->from_x = p->x;
    p->from_y = p->y;
    gg_game_act(&g, GG_ACT_N);
    CHECK(g.want_travel, "the way out of the vale asked for nothing");
    CHECK(gg_game_travel(&g, gg_asset_path("maps/stones.map.txt"),
                         g.travel_x, g.travel_y), "the crossing failed");
    gg_quests_tick(&g);
    CHECK(gg_flag(&g, "stood_among_the_stones"),
          "arriving at the stones went unremarked");
    // The way in is twenty tiles from the ring, and the stage that waits in
    // the ring has to still be waiting.
    CHECK(g.quest[caravan] == 6, "the story is at stage %u on arrival; the "
          "meeting in the ring did not wait to be walked to",
          g.quest[caravan]);

    // --- the man in the ring ---------------------------------------------
    int rugar = -1;
    for (int i = 0; i < g.actors; i++)
        if (g.actor[i].active && g.actor[i].beast &&
            gg_bestiary_at(g.actor[i].beast) &&
            SDL_strcasecmp(gg_bestiary_at(g.actor[i].beast)->id, "CHIEF") == 0)
            rugar = i;
    CHECK(rugar > 0, "there is nobody in the ring to answer for the caravan");
    if (rugar < 0) { gg_game_free(&g); restore_dialogue(); restore_quests();
                     restore_bestiary(); return; }

    // Near enough to see him, which is a stage of its own.
    p = gg_player(&g);
    p->x = (int16_t)g.actor[rugar].x;
    p->y = (int16_t)(g.actor[rugar].y + 2);
    p->from_x = p->x;
    p->from_y = p->y;
    gg_quests_tick(&g);
    CHECK(g.quest[caravan] >= 7, "the meeting in the ring was not reached "
          "(stage %u)", g.quest[caravan]);

    // The fight, struck properly - every blow through gg_game_act, and Rugar
    // striking back on his own turns. The Avatar is kept on his feet between
    // swings and given the level to land them: this pins that the story can be
    // played through, not that it is winnable at level one, which is a matter
    // of balance and belongs to a test about balance.
    gg_player(&g)->level = 20;
    for (int swing = 0; swing < 600 && g.actor[rugar].active; swing++) {
        gg_actor *me = gg_player(&g);
        me->hp = me->hp_max;
        me->x = (int16_t)g.actor[rugar].x;
        me->y = (int16_t)(g.actor[rugar].y + 1);
        me->facing = GG_FACE_UP;
        gg_game_act(&g, GG_ACT_FIGHT);
    }
    CHECK(!g.actor[rugar].active, "Rugar could not be brought down");

    // What he was carrying, off the ground and into the pack.
    const int where = gg_ground_at(&g.map, g.actor[rugar].x, g.actor[rugar].y);
    CHECK(where >= 0, "Rugar left nothing where he fell");
    gg_player(&g)->x = g.actor[rugar].x;
    gg_player(&g)->y = g.actor[rugar].y;
    for (int i = 0; i < 4; i++) gg_game_act(&g, GG_ACT_GET);
    CHECK(gg_pack_count(&g, GG_ITEM_SILVER) >= 3,
          "the caravan's silver is not in the pack (%d of it)",
          gg_pack_count(&g, GG_ITEM_SILVER));
    gg_quests_tick(&g);
    CHECK(gg_flag(&g, "caravan_avenged"), "taking the silver settled nothing");

    // --- home again -------------------------------------------------------
    CHECK(g.map.portals > 0, "there is no way back from the stones");
    p = gg_player(&g);
    p->x = (int16_t)g.map.portal[0].x;
    p->y = (int16_t)(g.map.portal[0].y - 1);
    p->from_x = p->x;
    p->from_y = p->y;
    gg_game_act(&g, GG_ACT_S);
    CHECK(g.want_travel, "the way back asked for nothing");
    CHECK(gg_game_travel(&g, gg_asset_path("maps/vale.map.txt"),
                         g.travel_x, g.travel_y), "the way back failed");
    gg_quests_tick(&g);

    int iolo = -1;
    for (int i = 0; i < g.actors; i++)
        if (SDL_strcmp(g.actor[i].name, "Iolo") == 0) iolo = i;
    CHECK(iolo > 0, "Iolo is not at his stall");
    if (iolo > 0) {
        CHECK(g.mode != GG_MODE_ENDING,
              "the story ended by walking into town rather than by finishing it");
        ask_everything(&g, iolo);
    }

    // --- the end ----------------------------------------------------------
    CHECK(g.story_over, "the story did not end");
    CHECK(g.mode == GG_MODE_ENDING, "the world is in mode %d, not the ending",
          (int)g.mode);
    CHECK(gg_pack_count(&g, GG_ITEM_SILVER) == 0,
          "the silver was handed over and is still in the pack");
    CHECK(gg_flag(&g, "caravan_returned"), "the last stage raised nothing");

    const char *quest = nullptr, *words = nullptr;
    CHECK(gg_ending(&g, &quest, &words), "there are no closing words");
    CHECK(quest && SDL_strcmp(quest, "The Missing Caravan") == 0,
          "the tale that ended is '%s'", quest ? quest : "(none)");
    CHECK(words && words[0] != '\0', "the ending says nothing");

    // A world that has ended takes no more orders.
    const uint32_t turn = g.turn;
    for (int i = 0; i < 10; i++) gg_game_act(&g, GG_ACT_N);
    CHECK(g.turn == turn, "the world kept turning after it ended");

    // And it is still over tomorrow.
    const char *who = "Hero";
    wipe_saves(who);
    CHECK(gg_save_write(&g, save_base(), who), "the save failed");
    gg_game back;
    SDL_zero(back);
    CHECK(gg_save_read(&back, save_base(), who), "the load failed");
    CHECK(back.story_over, "a finished story came back unfinished");
    CHECK(back.mode == GG_MODE_ENDING, "a finished world came back playable");
    CHECK(gg_ending(&back, &quest, &words) && words && words[0],
          "the closing words did not survive being put down");
    gg_game_free(&back);
    wipe_saves(who);

    gg_game_free(&g);
    restore_dialogue();
    restore_quests();
    restore_bestiary();
}

// Every content file, with the line endings a Windows editor writes.
//
// A blank line in a CRLF file is not empty: it holds a carriage return. Left
// on, that reads as a keyword nobody has heard of, and the whole file is
// refused - which is what the shipped bestiary, dialogue and quests all did on
// Windows and nowhere else, because a checkout there converts them and a
// checkout here does not. Every one of these formats is a file a player may
// open in Notepad, so this is a rule about the formats and not about CI.
static void a_content_file_written_on_windows_still_loads(void) {
    // The same text twice, once with "\n" and once with "\r\n" - including the
    // blank lines, which are the ones that matter.
    static const char *const BOOK =
        "# a book\n"
        "\n"
        "person Wilkin\n"
        "art ELDER\n"
        "home Britain\n"
        "at 06 -3 -3\n"
        "\n"
        "greet Wilkin, and glad of it.\n"
        "topic name\n"
        "  say Wilkin.\n";
    static const char *const BEASTS =
        "# a bestiary\n"
        "\n"
        "creature RAT\n"
        "  name a rat\n"
        "  art OUTLAW\n"
        "\n"
        "  damage 1\n"
        "  haunts 1\n";
    static const char *const QUESTS =
        "# a story\n"
        "\n"
        "quest ONE\n"
        "  name The Only Quest\n"
        "\n"
        "  stage\n"
        "    when knows name\n"
        "    journal It has begun.\n";
    static const char *const SPELLS =
        "# some words\n"
        "\n"
        "rune MANI life\n"
        "\n"
        "spell MANI\n"
        "  name Heal\n"
        "  circle 1\n"
        "  costs ginseng 1\n"
        "  effect heal 10\n"
        "  say Thou art mended.\n";

    char crlf[2048];

    #define AS_CRLF(text) do {                                                \
        size_t o = 0;                                                         \
        for (const char *r = (text); *r && o < sizeof crlf - 3; r++) {        \
            if (*r == '\n') crlf[o++] = '\r';                                 \
            crlf[o++] = *r;                                                   \
        }                                                                     \
        crlf[o] = '\0';                                                       \
    } while (0)

    // The book.
    CHECK(gg_dialogue_load(write_dialogue(BOOK)), "the book will not load at all");
    const int people = gg_dialogue_speakers();
    AS_CRLF(BOOK);
    CHECK(gg_dialogue_load(write_dialogue(crlf)),
          "a dialogue file written on Windows was refused");
    CHECK(gg_dialogue_speakers() == people, "it came back with %d people, not %d",
          gg_dialogue_speakers(), people);
    const gg_speaker *w = gg_dialogue_find("Wilkin");
    CHECK(w != nullptr, "Wilkin is not in the Windows book");
    CHECK(w && SDL_strcmp(w->greet, "Wilkin, and glad of it.") == 0,
          "his greeting came back as '%s'", w ? w->greet : "");
    restore_dialogue();

    // The bestiary.
    CHECK(gg_bestiary_load(write_bestiary(BEASTS)), "the bestiary will not load");
    AS_CRLF(BEASTS);
    CHECK(gg_bestiary_load(write_bestiary(crlf)),
          "a bestiary written on Windows was refused");
    const int rat = gg_bestiary_find("RAT");
    CHECK(rat >= 0, "the rat is not in the Windows bestiary");
    if (rat >= 0)
        CHECK(SDL_strcmp(gg_bestiary_at(rat)->name, "a rat") == 0,
              "it came back called '%s'", gg_bestiary_at(rat)->name);
    restore_bestiary();

    // The quests.
    CHECK(gg_quests_load(write_quests(QUESTS)), "the quests will not load");
    AS_CRLF(QUESTS);
    CHECK(gg_quests_load(write_quests(crlf)),
          "a quest file written on Windows was refused");
    const int one = gg_quest_find("ONE");
    CHECK(one >= 0, "the quest is not in the Windows file");
    if (one >= 0)
        CHECK(SDL_strcmp(gg_quest_at(one)->name, "The Only Quest") == 0,
              "it came back called '%s'", gg_quest_at(one)->name);
    restore_quests();

    // The spells.
    CHECK(gg_magic_load(write_spells(SPELLS)), "the spells will not load");
    AS_CRLF(SPELLS);
    CHECK(gg_magic_load(write_spells(crlf)),
          "a spell file written on Windows was refused");
    CHECK(gg_magic_runes() == 1 && gg_magic_spells() == 1,
          "the Windows file gave %d runes and %d spells", gg_magic_runes(),
          gg_magic_spells());
    restore_spells();

    #undef AS_CRLF
}

static void a_map_you_leave_is_as_you_left_it(void) {
    const char *near = author_linked_map("test_mem_a.ggmap", "The Near Field",
                                         "test_mem_b.ggmap", 20, 12,
                                         30, 8, "Aldith");
    char near_path[1024];
    SDL_strlcpy(near_path, near, sizeof near_path);
    const char *far = author_linked_map("test_mem_b.ggmap", "The Far Field",
                                        "test_mem_a.ggmap", 9, 8,
                                        4, 8, "Beorn");
    char far_path[1024];
    SDL_strlcpy(far_path, far, sizeof far_path);

    gg_game g;
    CHECK(gg_game_new_from_map(&g, near_path, "Rememberer", 4242),
          "the near field would not open");

    // Three things done in the near field, one of each kind the promise
    // covers: something left on the floor, something taken off it, and
    // somebody killed on it.
    CHECK(gg_ground_drop(&g.map, 12, 9, GG_ITEM_SILVER, 3),
          "the silver would not go on the ground");
    CHECK(gg_ground_drop(&g.map, 13, 9, GG_ITEM_BREAD, 1),
          "the bread would not go on the ground");

    gg_actor *p = gg_player(&g);
    p->x = 13;
    p->y = 9;
    gg_game_act(&g, GG_ACT_GET);
    CHECK(gg_ground_at(&g.map, 13, 9) < 0, "the bread was not picked up");
    const int loaves = gg_pack_count(&g, GG_ITEM_BREAD);
    CHECK(loaves >= 1, "the bread did not reach the pack");

    const int brigand = gg_spawn_named(&g, "BRIGAND", 15, 9);
    CHECK(brigand > 0, "no brigand could be placed");
    if (brigand > 0) {
        // Named apart from the ones already haunting the field, so the check
        // on the way back is about this one and not about a head count.
        SDL_strlcpy(g.actor[brigand].name, "the marked brigand",
                    sizeof g.actor[brigand].name);
        // Killed properly, through the fight the game actually has, rather
        // than by switching a flag off behind its back.
        p = gg_player(&g);
        p->x = 14;
        p->y = 9;
        p->level = 20;                  // so this ends inside the bound below
        for (int swing = 0; swing < 400 && g.actor[brigand].active; swing++) {
            g.actor[brigand].x = 15;
            g.actor[brigand].y = 9;
            gg_strike(&g, g.player, brigand);
        }
        CHECK(!g.actor[brigand].active, "the brigand would not fall");
    }

    // And somebody left alive, wounded, standing where they were last seen
    // rather than where the map says they live.
    int aldith = -1;
    for (int i = 0; i < g.actors; i++)
        if (SDL_strcmp(g.actor[i].name, "Aldith") == 0) aldith = i;
    CHECK(aldith > 0, "Aldith is not in the near field");
    if (aldith > 0) {
        g.actor[aldith].x = 11;
        g.actor[aldith].y = 14;
        g.actor[aldith].hp = 7;
        g.actor[aldith].schedn = 0;     // so nothing walks them off again
    }

    // Somebody to leave alive, put there on purpose. An authored map holds
    // what it is written to hold - the wandering creatures belong to worlds the
    // generator builds - so a test that wants a survivor has to place one.
    const int spared = gg_spawn_named(&g, "BRIGAND", 17, 9);
    CHECK(spared > 0, "no second brigand could be placed");

    // How many are still on their feet here, so the return can be asked for
    // the same number: what this remembers is not "everybody" and not
    // "nobody".
    int standing = 0;
    for (int i = 0; i < g.actors; i++)
        if (g.actor[i].active && g.actor[i].hostile) standing++;
    CHECK(standing > 0, "nothing hostile was left alive to be remembered");

    // Out and back.
    p = gg_player(&g);
    p->x = 29;
    p->y = 8;
    gg_game_act(&g, GG_ACT_E);
    CHECK(g.want_travel, "the way out asked for nothing");
    CHECK(gg_game_travel(&g, far_path, g.travel_x, g.travel_y),
          "the crossing failed");
    // A map is called what its file is called, without the extension: the
    // same map in either form is the same place - see place_of.
    CHECK(SDL_strcmp(g.here, "test_mem_b") == 0,
          "the far field thinks it is '%s'", g.here);

    // Something left in the far field too, so each map is asked to keep its
    // own and not merely the last one.
    CHECK(gg_ground_drop(&g.map, 6, 6, GG_ITEM_GOLD, 9),
          "the gold would not go on the ground");

    p = gg_player(&g);
    p->x = 5;
    p->y = 8;
    gg_game_act(&g, GG_ACT_W);
    CHECK(g.want_travel, "the way back asked for nothing");
    CHECK(gg_game_travel(&g, near_path, g.travel_x, g.travel_y),
          "the way back failed");
    CHECK(SDL_strcmp(g.map.name, "The Near Field") == 0,
          "came back to '%s'", g.map.name);

    // The verification, in the plan's own words: the silver is where it was
    // left.
    const int pile = gg_ground_at(&g.map, 12, 9);
    CHECK(pile >= 0, "the silver was not where it was left");
    if (pile >= 0) {
        CHECK(g.map.ground[pile].kind == GG_ITEM_SILVER &&
              g.map.ground[pile].count == 3,
              "the pile came back as %d of kind %d",
              g.map.ground[pile].count, g.map.ground[pile].kind);
    }
    CHECK(gg_ground_at(&g.map, 13, 9) < 0,
          "the bread grew back on the floor after it was picked up");
    CHECK(gg_pack_count(&g, GG_ITEM_BREAD) == loaves,
          "the bread that was picked up did not stay picked up");

    int risen = 0, still_up = 0, aldiths = 0;
    int found_aldith = -1;
    for (int i = 0; i < g.actors; i++) {
        if (!g.actor[i].active) continue;
        if (g.actor[i].hostile) still_up++;
        if (SDL_strcmp(g.actor[i].name, "the marked brigand") == 0) risen++;
        if (SDL_strcmp(g.actor[i].name, "Aldith") == 0) {
            aldiths++;
            found_aldith = i;
        }
    }
    CHECK(risen == 0, "the brigand that was killed is back on his feet");

    // Nor is he carried back as an empty slot. A death leaves a hole in the
    // actor array while the map is underfoot, and a world that remembered
    // those would fill up with corpses one crossing at a time.
    int ghosts = 0;
    for (int i = 0; i < g.actors; i++)
        if (!g.actor[i].active) ghosts++;
    CHECK(ghosts == 0, "%d of the dead came back as empty slots", ghosts);
    CHECK(still_up == standing,
          "%d hostiles were left alive here and %d came back", standing, still_up);
    CHECK(aldiths == 1, "the near field holds %d Aldiths", aldiths);
    if (found_aldith >= 0) {
        CHECK(g.actor[found_aldith].x == 11 && g.actor[found_aldith].y == 14,
              "Aldith came back at %d,%d, not where she was left",
              g.actor[found_aldith].x, g.actor[found_aldith].y);
        CHECK(g.actor[found_aldith].hp == 7,
              "Aldith's wound healed itself while nobody was looking (%d)",
              g.actor[found_aldith].hp);
    }

    // A world's memory has to survive being put down. Saved here, loaded into
    // a fresh game, and asked the same questions - including about the map
    // that is not underfoot.
    const char *who = "Rememberer";
    wipe_saves(who);
    CHECK(gg_save_write(&g, save_base(), who), "the save failed");

    gg_game back;
    SDL_zero(back);
    CHECK(gg_save_read(&back, save_base(), who), "the load failed");

    const char *why = "";
    CHECK(games_match(&g, &back, &why),
          "the resumed world differs in %s", why);
    CHECK(back.visiteds == 1, "%d maps came back remembered, expected one",
          back.visiteds);

    // And the far field's gold is still there after the round trip through a
    // file, which no amount of comparing two live games would prove.
    p = gg_player(&back);
    p->x = 29;
    p->y = 8;
    gg_game_act(&back, GG_ACT_E);
    CHECK(gg_game_travel(&back, far_path, back.travel_x, back.travel_y),
          "the crossing failed after loading");
    const int gold = gg_ground_at(&back.map, 6, 6);
    CHECK(gold >= 0, "the gold dropped in the far field did not survive a save");
    if (gold >= 0)
        CHECK(back.map.ground[gold].kind == GG_ITEM_GOLD &&
              back.map.ground[gold].count == 9,
              "the gold came back as %d of kind %d",
              back.map.ground[gold].count, back.map.ground[gold].kind);

    gg_game_free(&back);
    gg_game_free(&g);
    wipe_saves(who);
    SDL_RemovePath(near_path);
    SDL_RemovePath(far_path);
}

static void a_way_out_that_leads_nowhere_is_refused(void) {
    const char *path = author_linked_map("test_broken.ggmap", "Nowhere",
                                         "test_no_such_map.ggmap", 5, 5,
                                         20, 8, nullptr);
    char map_path[1024];
    SDL_strlcpy(map_path, path, sizeof map_path);

    gg_game g;
    CHECK(gg_game_new_from_map(&g, map_path, "Lost", 4242), "the map would not open");

    const char *was = g.map.name;
    (void)was;
    gg_actor *p = gg_player(&g);
    p->x = 19;
    p->y = 8;
    gg_game_act(&g, GG_ACT_E);
    CHECK(g.want_travel, "stepping on the gate asked for nothing");

    // The frontend cannot find it, and the world is left exactly as it was
    // rather than half-changed.
    CHECK(!gg_game_travel(&g, gg_pref_file("test_no_such_map.ggmap"),
                          g.travel_x, g.travel_y),
          "travelling into a map that is not there reported success");
    CHECK(SDL_strcmp(g.map.name, "Nowhere") == 0,
          "a failed crossing left the world as '%s'", g.map.name);
    CHECK(g.map.cell != nullptr, "a failed crossing freed the map");
    CHECK(!g.want_travel, "a failed crossing left the request standing");

    // Still playable.
    for (int i = 0; i < 5; i++) gg_game_act(&g, GG_ACT_WAIT);
    CHECK(g.turn > 0, "the world stopped after a failed crossing");

    gg_game_free(&g);
    SDL_RemovePath(map_path);
}

// The editor refuses a way out nobody could take.
static void the_editor_refuses_a_gate_nobody_can_reach(void) {
    gg_editor e;
    SDL_zero(e);
    CHECK(gg_edit_new(&e, 24, 24), "could not make a map");
    SDL_strlcpy(e.map.name, "Gated", sizeof e.map.name);

    gg_edit_tool(&e, GG_TOOL_PORTAL);
    gg_edit_apply(&e, 5, 5);
    CHECK(e.map.portals == 0, "a way out to nowhere was placed");
    CHECK(SDL_strstr(e.say, "lead to") != nullptr, "it did not say why: %s", e.say);

    gg_edit_link_to(&e, "somewhere.ggmap", 3, 3);
    gg_edit_apply(&e, 5, 5);
    CHECK(e.map.portals == 1, "the way out was not placed");
    gg_edit_apply(&e, 5, 5);
    CHECK(e.map.portals == 1, "two ways out were stacked on one tile");

    // Wall it in, and the editor says so.
    char problems[GG_EDIT_PROBLEMS_MAX][GG_EDIT_SAY_MAX];
    CHECK(gg_edit_check(&e, problems) == 0, "already broken: %s", problems[0]);

    gg_edit_tool(&e, GG_TOOL_TERRAIN);
    while (e.terrain != GG_TILE_MOUNTAIN) gg_edit_brush(&e, 1);
    gg_edit_apply(&e, 5, 5);
    const int bad = gg_edit_check(&e, problems);
    CHECK(bad > 0, "a way out inside a mountain is not a problem");
    bool said = false;
    for (int i = 0; i < bad && i < GG_EDIT_PROBLEMS_MAX; i++)
        if (SDL_strstr(problems[i], "way out")) said = true;
    CHECK(said, "the problem was not the one about the way out");

    // And it rubs out.
    gg_edit_tool(&e, GG_TOOL_PORTAL);
    gg_edit_erase(&e, 5, 5);
    CHECK(e.map.portals == 0, "the way out would not be closed");

    gg_edit_close(&e);
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
    if (!gg_dialogue_load(gg_asset_path("dialogue.txt")))
        SDL_Log("gigantima: tests could not load the dialogue");
    if (!gg_quests_load(gg_asset_path("quests.txt")))
        SDL_Log("gigantima: tests could not load the quests");

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
    RUN(every_verb_can_be_reached_with_a_pad);
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

    RUN(a_recorded_session_replays_to_the_same_world);
    RUN(the_state_hash_notices_every_part_of_the_world);

    RUN(a_key_can_be_moved_and_the_world_hears_the_new_one);
    RUN(what_makes_the_game_reachable_survives_being_put_down);

    RUN(the_tune_follows_where_you_are_and_what_hour_it_is);
    RUN(the_world_says_what_it_did_and_forgets_it);
    RUN(every_event_has_a_sound_baked_for_it);

    RUN(a_map_authored_in_the_editor_can_be_played);
    RUN(the_editor_rubs_out_what_it_draws);
    RUN(the_editor_says_what_is_wrong_with_a_map);
    RUN(a_failed_load_leaves_what_was_open_alone);

    RUN(who_lives_in_the_town_comes_out_of_the_book);
    RUN(somebody_who_lives_here_needs_a_day);

    RUN(a_two_stage_quest_is_completed_and_remembered);
    RUN(a_quest_cannot_skip_a_stage);
    RUN(one_quest_can_open_another);
    RUN(killing_things_moves_a_quest_on);
    RUN(a_quest_file_that_does_not_parse_loads_nothing);
    RUN(the_vale_is_peopled_by_the_book);
    RUN(the_vale_has_a_story_that_can_be_reached);

    RUN(walking_between_two_maps_takes_everything_with_you);
    RUN(a_party_that_walked_the_road_can_beat_the_man_at_the_end_of_it);
    RUN(every_shipped_map_is_a_place_you_can_walk_to);
    RUN(each_place_holds_what_the_bestiary_says_it_does);
    RUN(a_map_written_as_text_is_the_same_map);
    RUN(a_map_written_by_hand_is_playable);
    RUN(a_map_that_does_not_parse_loads_nothing);
    RUN(two_journeys_through_one_map_are_not_the_same_journey);
    RUN(what_the_party_learns_makes_it_stronger);
    RUN(a_companion_rises_with_the_avatar);
    RUN(a_journey_that_fights_its_way_north_arrives_stronger);
    RUN(the_whole_story_can_be_played_from_start_to_finish);
    RUN(a_content_file_written_on_windows_still_loads);
    RUN(a_map_you_leave_is_as_you_left_it);
    RUN(a_way_out_that_leads_nowhere_is_refused);
    RUN(the_editor_refuses_a_gate_nobody_can_reach);

    RUN(every_terrain_and_item_has_a_name);
    RUN(every_prop_has_a_plausible_footprint);
    RUN(a_props_atlas_rect_matches_the_size_it_declares);

    SDL_Log("%s", "");
    SDL_Log("%d checks, %d failures", g_checks, g_failures);
    SDL_Quit();
    return g_failures ? 1 : 0;
}
