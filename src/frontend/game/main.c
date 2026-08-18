// main.c - SDL3 callback entry points, the frame loop, and the two windows.
//
// The simulation is turn-based; the presentation is not. So the loop here runs
// a fixed 60 Hz animation tick regardless of whether the world advanced, and
// the world advances only when gg_input_take hands back an action. That split
// is the reason a walk cycle can play while the town waits for you.
//
// Note the SDL3 error convention - most calls return bool, true on success,
// the opposite of SDL2's "0 means OK".
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "core/gg_common.h"
#include "core/gg_game.h"
#include "gfx/gg_render.h"
#include "gfx/gg_font.h"
#include "ui/gg_ui.h"
#include "platform/gg_input.h"
#include "platform/gg_paths.h"
#include "debug/gg_debug.h"
#include "core/gg_save.h"
#include "core/gg_replay.h"
#include "core/gg_dialogue.h"
#include "core/gg_combat.h"
#include "core/gg_magic.h"
#include "core/gg_bestiary.h"
#include "core/gg_quest.h"
#include "audio/gg_audio.h"
#include "ui/gg_screens.h"
#include "platform/gg_settings.h"

#define WINDOW_SCALE 1

typedef struct {
    SDL_Window   *win;
    SDL_Renderer *ren;

    // The debug view gets its own window so it never draws over the game.
    SDL_Window   *dbg_win;
    SDL_Renderer *dbg_ren;
    bool          debug;

    gg_game   game;
    gg_input  in;

    uint64_t last_ns, accum_ns;
    uint64_t frames;             // free-running, for title-screen animation

    // Which screen is showing. `started` used to be a bool for "past the
    // title", which stopped being enough the moment there was more than one
    // screen to be on.
    gg_screens   screens;
    gg_settings  settings;
    bool         have_game;      // a world exists to draw or return to
    bool         started;        // kept: --play and --shot still mean "in the world"
    bool         quit;

    // Borderless fullscreen, and the windowed geometry to come back to.
    bool faux_fs;
    int  pre_fs_x, pre_fs_y, pre_fs_w, pre_fs_h;

    // Headless capture: --shot <file.bmp> [--shot-at <turn>] runs the world
    // forward, writes one frame and exits. For eyeballing changes without a
    // window, and for the CI smoke test.
    const char *shot_path;
    uint32_t    shot_at;

    // The same idea for the ears: --music <file.wav> walks every tune, writes
    // one recording of the lot and exits. A thing you cannot capture is a
    // thing you cannot check, and a crossfade is not visible in a screenshot.
    const char *music_path;

    uint32_t    seed;
    const char *profile;
    const char *map_path;         // --map FILE: play an authored map

    // --at X,Y drops the avatar somewhere specific. A development flag, and an
    // earned one: verifying a change to how water is drawn means getting to
    // water, and walking there by hand is not a workflow.
    int  at_x, at_y;
    bool at_set;

    // --time HH:MM sets the world clock. Also a development flag, and earned
    // the same way: checking that a lit room differs from the street at night
    // means being able to get to night without walking there.
    int  clock_min;
    bool clock_set;

    bool force_new;      // --new: start over even if this profile has a save

    // --turns N plays N turns and quits, through the ordinary exit path - so
    // it saves the way a real session does. --shot deliberately does not save,
    // because a screenshot must never overwrite somebody's game, which left no
    // way to exercise the save-on-exit path at all.
    uint32_t turn_limit;
    bool     turn_limit_set;

    // --listen MS keeps a headless run alive afterwards so the audio device is
    // fed. See the note where it is used.
    uint32_t listen_ms;

    // --record writes every action the world is given to a file, and --replay
    // plays one back and says whether it ended on the same world. See
    // core/gg_replay.h; the point of it is that a bug report can be a file.
    const char *record_path;
    const char *replay_path;
    gg_recorder rec;

    // --pad-loop plays a whole journey with a *virtual* gamepad and no
    // keyboard at all. See pad_bot below for what it is for.
    bool          pad_loop;
    SDL_Joystick *pad_stick;
    int           bot_phase;
    int           bot_held;      // the virtual button currently down, or -1
    int           bot_hold;      // frames left to hold it
    int           bot_wait;      // frames to let the world settle
    int           bot_walked;    // steps taken in the world
    int           bot_presses;
    int           bot_name_len;  // letters to type before "that will do"
    bool          bot_saved;
    bool          bot_stuck;
    bool          bot_done;

    // --screen NAME opens on a named screen, so --shot can photograph each of
    // them. Without it only the world and the title were reachable from the
    // command line, and "a frame per screen" was not a check anybody could run.
    const char *screen_name;
    bool loaded;         // this session began by resuming a save
    bool saved_on_exit;
} gg_app;

// ---------------------------------------------------------------------------
// Fullscreen, the long way round.
//
// SDL_SetWindowFullscreen is the obvious call and it does not work under WSLg:
// the real window becomes the size of the display, SDL is never told, and the
// renderer goes on drawing at the old size - which lands the view in a corner.
// A borderless window stretched over the display avoids the whole problem,
// because we set the size ourselves, so SDL knows it, so the renderer is right.
// ---------------------------------------------------------------------------
static void toggle_fullscreen(gg_app *app) {
    if (!app->win) return;

    if (app->faux_fs) {
        // Let the decoration change land before resizing: stacking the two
        // together loses the border under WSLg, and a window with no title bar
        // and no way to get one back is worse than no fullscreen at all.
        SDL_SetWindowBordered(app->win, true);
        SDL_SyncWindow(app->win);
        SDL_SetWindowSize(app->win, app->pre_fs_w, app->pre_fs_h);
        SDL_SetWindowPosition(app->win, app->pre_fs_x, app->pre_fs_y);
        SDL_SyncWindow(app->win);
        app->faux_fs = false;
    } else {
        SDL_Rect b;
        const SDL_DisplayID id = SDL_GetDisplayForWindow(app->win);
        if (!id || !SDL_GetDisplayBounds(id, &b)) return;

        SDL_GetWindowPosition(app->win, &app->pre_fs_x, &app->pre_fs_y);
        SDL_GetWindowSize(app->win, &app->pre_fs_w, &app->pre_fs_h);

        SDL_SetWindowBordered(app->win, false);
        SDL_SyncWindow(app->win);
        SDL_SetWindowPosition(app->win, b.x, b.y);
        SDL_SetWindowSize(app->win, b.w, b.h);
        SDL_SyncWindow(app->win);
        app->faux_fs = true;
    }
}

// ---------------------------------------------------------------------------
// Debug window
// ---------------------------------------------------------------------------
static void debug_open(gg_app *app) {
    if (app->dbg_win) return;

    if (!SDL_CreateWindowAndRenderer("Gigantima - debug",
                                     GG_DBG_W, GG_DBG_H,
                                     SDL_WINDOW_RESIZABLE,
                                     &app->dbg_win, &app->dbg_ren)) {
        SDL_Log("gigantima: could not open the debug window: %s", SDL_GetError());
        app->dbg_win = nullptr;
        app->dbg_ren = nullptr;
        app->debug = false;
        return;
    }
    SDL_SetRenderVSync(app->dbg_ren, 1);
    SDL_SetRenderLogicalPresentation(app->dbg_ren, GG_DBG_W, GG_DBG_H,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    gg_font_init(app->dbg_ren);
    gg_debug_init(app->dbg_ren);
}

static void debug_close(gg_app *app) {
    if (!app->dbg_win) return;
    gg_debug_quit(app->dbg_ren);
    gg_font_quit_renderer(app->dbg_ren);
    SDL_DestroyRenderer(app->dbg_ren);
    SDL_DestroyWindow(app->dbg_win);
    app->dbg_ren = nullptr;
    app->dbg_win = nullptr;
}

// Reads back the current render target. Must run before SDL_RenderPresent.
static bool save_shot(SDL_Renderer *ren, const char *path) {
    SDL_Surface *surf = SDL_RenderReadPixels(ren, nullptr);
    if (!surf) {
        SDL_Log("gigantima: RenderReadPixels failed: %s", SDL_GetError());
        return false;
    }
    const bool ok = SDL_SaveBMP(surf, path);
    if (!ok) SDL_Log("gigantima: SaveBMP failed: %s", SDL_GetError());
    SDL_DestroySurface(surf);
    return ok;
}

// ---------------------------------------------------------------------------
// Composing a frame
// ---------------------------------------------------------------------------
static void draw(gg_app *app) {
    SDL_SetRenderDrawColor(app->ren, 0, 0, 0, 255);
    SDL_RenderClear(app->ren);

    // The pause menu is drawn over the world, so the world goes down first.
    const bool world_visible = app->screens.id == GG_SCREEN_PLAY ||
                               app->screens.id == GG_SCREEN_PAUSE;
    if (world_visible && app->have_game) {
        gg_render_world(&app->game, app->ren);
        gg_ui_hud(&app->game, app->ren);
        if (app->game.mode == GG_MODE_CONVERSE)
            gg_ui_converse(&app->game, app->ren);
        else if (app->game.mode == GG_MODE_PACK)
            gg_ui_pack(&app->game, app->ren, gg_render_items());
        else if (app->game.mode == GG_MODE_SPELL)
            gg_ui_spells(&app->game, app->ren);
        else if (app->game.mode == GG_MODE_JOURNAL)
            gg_ui_journal(&app->game, app->ren);
        else if (app->game.mode == GG_MODE_SHEET)
            gg_ui_sheet(&app->game, app->ren);
        else if (app->game.mode == GG_MODE_ENDING ||
                 app->game.mode == GG_MODE_GAMEOVER)
            gg_ui_ending(&app->game, app->ren);
    }

    if (app->screens.id != GG_SCREEN_PLAY)
        gg_screens_draw(&app->screens, app->ren, app->frames);
}

// ---------------------------------------------------------------------------
// Screens
//
// The screens own their menus and their drawing; this owns what a chosen row
// actually does. Keeping the transitions in one place is the whole point -
// they are the part that tangles.
// ---------------------------------------------------------------------------
static void screen_go(gg_app *app, gg_screen_id id) {
    gg_screens_enter(&app->screens, id, gg_pref_path(), &app->settings,
                     &app->game, app->have_game);
    app->started = (id == GG_SCREEN_PLAY);

    // The button that took us off the last screen must not also act on this
    // one - "Resume" on the pause menu is A, and so is "talk".
    gg_input_forget(&app->in);

    // Text input is a mode the platform has to be put into and taken out of -
    // on a phone it raises a keyboard, and on a desktop it is what turns key
    // presses into characters the layout agrees with. Tied to the screen so it
    // cannot be left on.
    if (app->win) {
        if (id == GG_SCREEN_NAME) SDL_StartTextInput(app->win);
        else                      SDL_StopTextInput(app->win);
    }
}

static bool start_world(gg_app *app, const char *profile, bool fresh) {
    gg_game fresh_game;
    SDL_zero(fresh_game);

    const bool built = fresh
        ? gg_game_new(&fresh_game, (uint32_t)SDL_GetPerformanceCounter(), profile)
        : gg_save_read(&fresh_game, gg_pref_path(), profile);
    if (!built) {
        SDL_Log("gigantima: could not %s %s",
                fresh ? "begin" : "resume", profile);
        return false;
    }

    gg_game_free(&app->game);
    app->game = fresh_game;
    app->have_game = true;

    SDL_strlcpy(app->settings.last_profile, profile,
                sizeof app->settings.last_profile);
    gg_settings_save(&app->settings, gg_pref_file(GG_SETTINGS_FILE));
    return true;
}

static bool save_now(gg_app *app) {
    if (!app->have_game || !app->game.map.cell) return false;
    return gg_save_write(&app->game, gg_pref_path(), app->game.profile);
}

// A direction pushed while a menu is showing. Both halves are offered to the
// screen: only the naming grid reads dx as movement, and only a row holding a
// value reads it as an adjustment, so exactly one of these ever does anything.
static void screen_nav(gg_app *app, int dx, int dy) {
    gg_screens_move(&app->screens, dx, dy);
    gg_screens_adjust(&app->screens, dx, &app->settings);
}

// Returns false when the application should exit.
static bool screen_act(gg_app *app, gg_screen_result r) {
    switch (r.action) {
    case GG_ACTION_NONE:
        return true;

    case GG_ACTION_GO:
        // Applying the options on the way out of the page is what makes them
        // feel applied, rather than needing a separate "apply" row.
        if (app->screens.id == GG_SCREEN_OPTIONS ||
            app->screens.id == GG_SCREEN_KEYS) {
            gg_settings_save(&app->settings, gg_pref_file(GG_SETTINGS_FILE));
            app->in.no_rumble = !app->settings.rumble;
            gg_input_bind(&app->in, &app->settings);
            gg_font_scale(app->settings.text_scale);
            gg_debug_plain_colours(app->settings.plain_colours);
            gg_audio_volumes(app->settings.music, app->settings.effects);
            if (app->settings.fullscreen != app->faux_fs) toggle_fullscreen(app);
            if (app->win && !app->faux_fs)
                SDL_SetWindowSize(app->win, GG_SCREEN_W * app->settings.scale,
                                  GG_SCREEN_H * app->settings.scale);
        }
        screen_go(app, r.next);
        return true;

    case GG_ACTION_CONTINUE:
        if (start_world(app, r.name, false)) screen_go(app, GG_SCREEN_PLAY);
        return true;

    case GG_ACTION_NEW_GAME:
        if (start_world(app, r.name, true)) screen_go(app, GG_SCREEN_PLAY);
        return true;

    case GG_ACTION_DELETE:
        gg_profile_delete(gg_pref_path(), r.name);
        screen_go(app, GG_SCREEN_PROFILES);
        return true;

    case GG_ACTION_SAVE:
        gg_log(&app->game, save_now(app) ? "Thy journey is recorded."
                                         : "The journey could not be recorded.");
        screen_go(app, GG_SCREEN_PLAY);
        return true;

    case GG_ACTION_QUIT_TO_TITLE:
        save_now(app);
        screen_go(app, GG_SCREEN_TITLE);
        return true;

    case GG_ACTION_QUIT:
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// A pad with nobody holding it
//
// The plan's verification for controller-only playability is "a full
// new-game-to-save loop with the keyboard unplugged", and the only honest way
// to run that here is to unplug the keyboard - so this attaches a **virtual
// gamepad** through SDL and plays the game with it. No synthetic actions and no
// shortcuts into the screens: the bot pushes virtual buttons, SDL turns them
// into ordinary gamepad events, and they arrive at gg_input_event exactly as a
// real pad's would.
//
// It navigates by reading the menu it is looking at rather than by following a
// list of presses, so a row moving does not silently turn this into a test of
// something else.
// ---------------------------------------------------------------------------
static bool pad_attach(gg_app *app) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "gigantima virtual pad";

    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (!id) {
        SDL_Log("gigantima: no virtual pad: %s", SDL_GetError());
        return false;
    }
    app->pad_stick = SDL_OpenJoystick(id);
    if (!app->pad_stick) {
        SDL_Log("gigantima: the virtual pad would not open: %s", SDL_GetError());
        return false;
    }
    app->bot_held = -1;
    app->bot_name_len = 3;
    SDL_Log("gigantima: playing with a virtual pad and no keyboard");
    return true;
}

// The row of `menu` whose label starts with `label`, or -1.
static int menu_row(const gg_menu *m, const char *label) {
    const size_t n = SDL_strlen(label);
    for (int i = 0; i < m->n; i++)
        if (SDL_strncasecmp(m->item[i].label, label, n) == 0) return i;
    return -1;
}

// Held for a few frames, not one. A face button arrives as an event and would
// be seen either way, but the d-pad is *sampled* once a frame - and a virtual
// button set and cleared inside the same frame is a button SDL never had time
// to notice. That is what left this pressing "down" four hundred times on the
// naming screen without the cursor moving once.
#define GG_BOT_HOLD 3

static void pad_press(gg_app *app, int button) {
    SDL_SetJoystickVirtualButton(app->pad_stick, button, true);
    app->bot_held = button;
    app->bot_hold = GG_BOT_HOLD;
    app->bot_presses++;
}

// A direction, pushed on the left stick rather than the d-pad. A virtual
// joystick's d-pad is a *hat*, so setting virtual buttons 11 to 14 moves
// nothing at all - which is what left this pressing "down" three hundred times
// on the naming screen with the cursor exactly where it started. The stick is
// also what most players actually use.
static void pad_push(gg_app *app, int dx, int dy) {
    SDL_SetJoystickVirtualAxis(app->pad_stick, SDL_GAMEPAD_AXIS_LEFTX,
                               (int16_t)(dx * 24000));
    SDL_SetJoystickVirtualAxis(app->pad_stick, SDL_GAMEPAD_AXIS_LEFTY,
                               (int16_t)(dy * 24000));
    app->bot_held = SDL_GAMEPAD_BUTTON_INVALID;   // an axis, not a button
    app->bot_hold = GG_BOT_HOLD;
    app->bot_presses++;
}

// Walks the cursor toward a row and chooses it once it is there. Returns true
// when it has been chosen.
static bool pad_choose_row(gg_app *app, const char *label) {
    const int want = menu_row(&app->screens.menu, label);
    if (want < 0) return false;
    if (app->screens.menu.cursor == want) {
        pad_press(app, SDL_GAMEPAD_BUTTON_SOUTH);
        return true;
    }
    pad_push(app, 0, app->screens.menu.cursor < want ? 1 : -1);
    return false;
}

// One frame of it. Presses are one frame long with a gap after, because a
// button held across frames is a held button and this is pressing, not holding.
static void pad_bot(gg_app *app) {
    if (!app->pad_stick) return;

    // A bound, because a bot that cannot find its way is a test that hangs
    // rather than one that fails - and a hung job is the worst possible way to
    // be told something is wrong.
    if (app->bot_presses > 300) {
        SDL_Log("gigantima: the pad gave up after %d presses on screen %d",
                app->bot_presses, (int)app->screens.id);
        app->bot_stuck = true;
        return;
    }

    if (app->bot_hold > 0) {
        if (--app->bot_hold > 0) return;
        if (app->bot_held >= 0)
            SDL_SetJoystickVirtualButton(app->pad_stick, app->bot_held, false);
        SDL_SetJoystickVirtualAxis(app->pad_stick, SDL_GAMEPAD_AXIS_LEFTX, 0);
        SDL_SetJoystickVirtualAxis(app->pad_stick, SDL_GAMEPAD_AXIS_LEFTY, 0);
        app->bot_held = -1;
        app->bot_wait = GG_BOT_HOLD;     // and let the release be seen
        return;
    }
    if (app->bot_wait > 0) { app->bot_wait--; return; }

    switch (app->screens.id) {
    case GG_SCREEN_TITLE:
        // Back at the title with a journey saved behind it: that is the whole
        // loop, and the bot stops rather than beginning another one. Without
        // this it went round again, and again - which is how sixteen profiles
        // called AAAAAAAAAA came to exist.
        if (app->bot_saved) {
            // Checked, not assumed: the pause menu said it saved, and this is
            // whether a file for that journey is actually on disk.
            const bool real = gg_save_exists(gg_pref_path(), app->game.profile);
            SDL_Log("gigantima: the pad played a whole journey and %s it - "
                    "%s, %d presses, keyboard never touched",
                    real ? "saved" : "FAILED TO SAVE",
                    app->game.profile, app->bot_presses);
            app->bot_done = real;
            app->bot_stuck = !real;
            return;
        }
        pad_choose_row(app, "New journey");
        return;

    case GG_SCREEN_NAME:
        // A refusal - the name is taken, or is not a name - is answered by
        // typing one more letter and trying again. A bot that pressed the same
        // button at the same message forever would be a hang rather than a
        // report, and "AAA already has a journey" is what it met on its second
        // ever run.
        if (app->screens.notice[0]) {
            SDL_Log("gigantima: the pad was told '%s'", app->screens.notice);
            app->bot_name_len++;
            pad_push(app, 0, -1);        // off the Begin key, back to letters
            return;
        }
        // The alphabet has to be touched before A means "type this letter"
        // rather than "that will do" - which is what lets a keyboard player
        // type a name and press Enter without ever seeing it.
        if (app->screens.key_row < 0)
            pad_push(app, 0, 1);
        else if ((int)SDL_strlen(app->screens.typed) < app->bot_name_len)
            pad_press(app, SDL_GAMEPAD_BUTTON_SOUTH);
        else
            pad_press(app, SDL_GAMEPAD_BUTTON_START);
        return;

    case GG_SCREEN_PLAY: {
        // A few of everything the world has: walk, look, open the pack and
        // close it, pick something up, read the journal, strike at nothing.
        static const int VERBS[] = {
            SDL_GAMEPAD_BUTTON_WEST,           // look
            SDL_GAMEPAD_BUTTON_BACK,           // the pack
            SDL_GAMEPAD_BUTTON_EAST,           // and close it
            SDL_GAMEPAD_BUTTON_NORTH,          // open a door
            SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, // strike
            SDL_GAMEPAD_BUTTON_SOUTH,          // talk
            SDL_GAMEPAD_BUTTON_EAST,           // leave whatever that opened
        };
        if (app->bot_walked < 12) {
            static const int WAY[4][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } };
            pad_push(app, WAY[app->bot_walked % 4][0], WAY[app->bot_walked % 4][1]);
            app->bot_walked++;
        } else if (app->bot_phase < (int)GG_COUNTOF(VERBS)) {
            pad_press(app, VERBS[app->bot_phase++]);
        } else {
            pad_press(app, SDL_GAMEPAD_BUTTON_START);   // pause
        }
        return;
    }

    case GG_SCREEN_PAUSE:
        // Save, and then leave. Both rows are found by name, so this is a test
        // of the pause menu rather than of where its rows happen to sit.
        if (!app->bot_saved) {
            if (pad_choose_row(app, "Save")) app->bot_saved = true;
        } else {
            pad_choose_row(app, "Leave for the title");
        }
        return;

    case GG_SCREEN_PROFILES:
    case GG_SCREEN_OPTIONS:
        pad_choose_row(app, "Back");
        return;

    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
// Where a map lives. The simulation names the map it wants and this is the only
// thing that knows where content is kept: maps ship in assets/maps/, and one
// saved by the editor sits beside the profiles, so both are tried and a map you
// authored yourself is playable without installing it.
static const char *gg_asset_path_for_map(const char *leaf) {
    // A way out names a *place* - `stones`, or `stones.ggmap` from content
    // written before that was true. Which file that is is this function's
    // business: the binary first, then the text, under the assets and then
    // beside the saved journeys. A map authored as text alone is a place the
    // world can lead to, with nothing to install and nothing to convert.
    static char place[GG_MAP_NAME_MAX];
    SDL_strlcpy(place, leaf, sizeof place);
    char *dot = SDL_strchr(place, '.');
    if (dot) *dot = '\0';

    static const char *const FORMS[] = { "%s.ggmap", "%s.map.txt" };
    for (size_t f = 0; f < GG_COUNTOF(FORMS); f++) {
        char name[GG_MAP_NAME_MAX + 16], rel[GG_MAP_NAME_MAX + 24];
        SDL_snprintf(name, sizeof name, FORMS[f], place);
        SDL_snprintf(rel, sizeof rel, "maps/%s", name);

        const char *where = gg_asset_path(rel);
        SDL_IOStream *probe = SDL_IOFromFile(where, "rb");
        if (probe) { SDL_CloseIO(probe); return where; }

        where = gg_pref_file(name);
        probe = SDL_IOFromFile(where, "rb");
        if (probe) { SDL_CloseIO(probe); return where; }
    }
    // Nothing found: hand back what was asked for, so the failure names it.
    return gg_pref_file(leaf);
}

// **The one way this file gives the world an action.** Everything below goes
// through here, so a recording is complete by construction rather than by
// somebody remembering to add a line beside each call - which is exactly the
// kind of promise that rots. See core/gg_replay.h.
static void act(gg_app *app, gg_action a) {
    gg_record_act(&app->rec, a);
    gg_game_act(&app->game, a);
}

// A scripted player, for the staged screens below and for the smoke test that
// plays the whole story through in one run.
//
// The world it walks through is a living one - somebody is standing on the
// road, a brigand has noticed you - so a plain "act north until it stops
// working" walks two tiles and gives up, which is what it did the first time
// the vale had people in it. Everything here goes through gg_game_act: walking
// into somebody is a conversation, walking into a brigand is a blow, and this
// only has to notice which happened and keep going.

// One step toward a tile, round whatever is between. Returns false when there
// is nowhere to go at all.
static bool step_toward(gg_app *app, int tx, int ty) {
    gg_game *g = &app->game;

    // Out of anything the last step opened. A conversation swallows the
    // direction keys, so a walk that walked into somebody would steer their
    // topic list from here on.
    if (g->mode == GG_MODE_CONVERSE) act(app, GG_ACT_WAIT);
    if (g->mode != GG_MODE_PLAY) return false;

    const int px = gg_player_const(g)->x, py = gg_player_const(g)->y;
    int nx = 0, ny = 0;
    // The pathfinder, not a greedy step: the first version drifted one tile
    // going round a townsperson and then walked straight past the gate it was
    // aiming at, and no greedy step gets inside a ring of standing stones.
    if (!gg_step_toward(g, g->player, tx, ty, &nx, &ny)) return false;

    const uint32_t was = g->turn;
    act(app, gg_action_toward(nx - px, ny - py));
    gg_game_animate(g);
    if (g->mode == GG_MODE_CONVERSE) return true;    // greeted somebody
    if (g->turn != was) return true;                 // stepped, or struck

    // Blocked by something that is not a turn. One turn of standing still, so
    // whoever is in the way has a chance to move on.
    act(app, GG_ACT_WAIT);
    gg_game_animate(g);
    return g->turn != was;
}

// Walks until within `near` tiles of a spot, picking up whatever it walks over.
static bool walk_to(gg_app *app, int tx, int ty, int near, int patience) {
    gg_game *g = &app->game;
    for (int i = 0; i < patience; i++) {
        const gg_actor *me = gg_player_const(g);
        if (gg_dist_cheb(me->x, me->y, tx, ty) <= near) return true;
        if (g->want_travel) return true;
        if (!step_toward(app, tx, ty)) return false;

        // Anything underfoot goes in the pack. A player picks up what they
        // walk over, and on this road what they walk over is what the last
        // brigand was carrying.
        me = gg_player_const(g);
        if (g->mode == GG_MODE_PLAY && gg_ground_at(&g->map, me->x, me->y) >= 0)
            act(app, GG_ACT_GET);
    }
    const gg_actor *me = gg_player_const(g);
    return gg_dist_cheb(me->x, me->y, tx, ty) <= near;
}

// Readies the best weapon and the best armour in the pack, through the pack's
// own action. What is best is what the item table says, so a better hammer
// found later is readied without this knowing there is such a thing.
static void ready_the_best(gg_app *app) {
    gg_game *g = &app->game;

    if (g->mode != GG_MODE_PLAY) return;
    act(app, GG_ACT_PACK);
    if (g->mode != GG_MODE_PACK) return;

    for (int slot = 0; slot < GG_SLOT_COUNT; slot++) {
        int best = -1, best_worth = 0;
        for (int i = 0; i < g->packn; i++) {
            const gg_item_def *d = &GG_ITEM[g->pack[i].kind];
            if (d->slot != slot) continue;
            const int worth = slot == GG_SLOT_WEAPON ? d->damage : d->guard;
            if (worth > best_worth) { best_worth = worth; best = i; }
        }
        if (best < 0 || g->equipped[slot] == best) continue;

        // Walked to with the direction keys, for the same reason the phial is.
        for (int guard = 0; guard < GG_PACK_MAX && g->pack_cursor != best; guard++)
            act(app, GG_ACT_S);
        if (g->pack_cursor == best) act(app, GG_ACT_EQUIP);
    }
    act(app, GG_ACT_PACK);
}

// Drinks something if there is something to drink and it is needed. A player
// carrying a phial and dying with it in their pack is not a player; it is a
// script that does not know what it is carrying.
static void drink_if_hurt(gg_app *app) {
    gg_game *g = &app->game;
    const gg_actor *me = gg_player_const(g);
    if (me->hp * 2 > me->hp_max) return;

    int want = -1;
    for (int i = 0; i < g->packn; i++)
        if (GG_ITEM[g->pack[i].kind].use != GG_USE_NONE &&
            GG_ITEM[g->pack[i].kind].heal > 0) { want = i; break; }
    if (want < 0) return;

    // Opened, walked to and used with the keys a player would press. The pack
    // cursor is world state and moving it by hand would move the world without
    // an action - see `act` above.
    if (g->mode != GG_MODE_PACK) act(app, GG_ACT_PACK);
    if (g->mode != GG_MODE_PACK) return;
    for (int guard = 0; guard < GG_PACK_MAX && g->pack_cursor != want; guard++)
        act(app, GG_ACT_S);
    if (g->pack_cursor == want) act(app, GG_ACT_USE);
    if (g->mode == GG_MODE_PACK) act(app, GG_ACT_PACK);
}

// Hunts what is in the hills until there is a weapon in hand, or `most` of
// them have fallen. The road arms you: a brigand drops a hammer and an outlaw
// drops a shield, and a scripted player that walks past all of them arrives at
// the end of the story with its fists - which is exactly what this did the
// first time it was run.
static void hunt_for_gear(gg_app *app, int most) {
    gg_game *g = &app->game;

    for (int hunt = 0; hunt < most; hunt++) {
        if (gg_attack_power(g, g->player) > 0 &&
            gg_guard_power(g, g->player) > 0) return;

        int prey = -1, near = 0;
        const gg_actor *me = gg_player_const(g);
        for (int i = 0; i < g->actors; i++) {
            if (!g->actor[i].active || !g->actor[i].hostile) continue;
            const int d = gg_dist_cheb(me->x, me->y, g->actor[i].x, g->actor[i].y);
            if (prey < 0 || d < near) { prey = i; near = d; }
        }
        if (prey < 0) return;

        for (int i = 0; i < 300 && g->actor[prey].active &&
                        g->mode == GG_MODE_PLAY; i++)
            if (!step_toward(app, g->actor[prey].x, g->actor[prey].y)) break;
        if (g->mode != GG_MODE_PLAY) return;
        drink_if_hurt(app);

        // Onto the tile they fell on, and take what is there.
        walk_to(app, g->actor[prey].x, g->actor[prey].y, 0, 60);
        for (int i = 0; i < 4; i++) act(app, GG_ACT_GET);
        ready_the_best(app);
    }
}

// Walks up to somebody and asks them every word they will answer to, once
// each - the two keys a player presses, in a loop.
static void ask_everything(gg_app *app, int who) {
    gg_game *g = &app->game;
    if (who < 0 || who >= g->actors || !g->actor[who].active) return;
    if (!walk_to(app, g->actor[who].x, g->actor[who].y, 1, 300)) return;

    if (g->mode != GG_MODE_CONVERSE) {
        const gg_actor *me = gg_player_const(g);
        act(app, gg_action_toward(g->actor[who].x - me->x,
                                        g->actor[who].y - me->y));
    }
    if (g->mode != GG_MODE_CONVERSE) return;

    // Every word once, reached the way a player reaches it: the cursor is
    // walked down the list with the direction keys and the word is asked with
    // the talk key. Setting `ask_cursor` from here would be quicker and would
    // move the world without an action, which is the one thing that would make
    // a recorded session unreplayable.
    for (int i = 0; i < GG_TOPICS_MAX && i < g->askables; i++) {
        for (int guard = 0; guard < GG_TOPICS_MAX && g->ask_cursor != i; guard++)
            act(app, GG_ACT_S);
        if (g->ask_cursor != i) break;
        act(app, GG_ACT_TALK);
        if (g->mode != GG_MODE_CONVERSE) return;   // the story ended mid-word
    }
    act(app, GG_ACT_WAIT);
}

// Walks to the first way out of the map underfoot and steps on it.
static bool walk_to_the_way_out(gg_app *app) {
    if (app->game.map.portals < 1) return false;
    walk_to(app, app->game.map.portal[0].x, app->game.map.portal[0].y, 0, 600);
    return app->game.want_travel;
}

// Crosses, wherever the way out leads. The frontend half of a crossing, which
// the frame loop does for a player and this has to do for itself.
static bool cross_over(gg_app *app) {
    if (!app->game.want_travel) return false;
    char leaf[GG_MAP_NAME_MAX];
    SDL_strlcpy(leaf, app->game.travel_to, sizeof leaf);
    const int tx = app->game.travel_x, ty = app->game.travel_y;
    if (!gg_game_travel(&app->game, gg_asset_path_for_map(leaf), tx, ty))
        return false;
    gg_record_travel(&app->rec, leaf, tx, ty);
    SDL_Log("gigantima: walked into %s", app->game.map.name);
    return true;
}

static void usage(void) {
    SDL_Log("usage: gigantima [--profile NAME] [--seed N] [--play] [--debug]\n"
            "                 [--scale N] [--fullscreen] [--no-rumble]\n"
            "                 [--new] [--turns N] [--screen NAME] [--map FILE.ggmap]\n"
            "                 [--at X,Y] [--time HH:MM]\n"
            "                 [--shot FILE.bmp] [--shot-at TURN]\n"
            "                 [--record FILE.ggreplay] [--replay FILE.ggreplay]\n"
            "                 [--pad-loop]");
}

// Plays a recorded session back and says whether it ended on the same world.
//
// The whole point of the exercise: the simulation is integer-only and seeded
// and only moves inside gg_game_act, so this has to end on the number the
// recording ends on. When it does not, the divergence *is* the bug, and the
// step it happened on is where to look.
static SDL_AppResult run_replay(gg_app *app) {
    gg_replay r;
    if (!gg_replay_load(&r, app->replay_path)) return SDL_APP_FAILURE;

    const bool built = r.map[0]
        ? gg_game_new_from_map(&app->game, gg_asset_path_for_map(r.map),
                               r.profile, r.seed)
        : gg_game_new(&app->game, r.seed, r.profile);
    if (!built) {
        SDL_Log("gigantima: the replay's world could not be built");
        gg_replay_free(&r);
        return SDL_APP_FAILURE;
    }
    app->have_game = true;
    app->started = true;

    int crossings = 0, acts = 0;
    for (int i = 0; i < r.steps; i++) {
        if (r.step[i].kind == GG_STEP_TRAVEL) {
            if (!gg_game_travel(&app->game, gg_asset_path_for_map(r.step[i].leaf),
                                r.step[i].x, r.step[i].y)) {
                SDL_Log("gigantima: step %d could not walk into %s", i,
                        r.step[i].leaf);
                gg_replay_free(&r);
                return SDL_APP_FAILURE;
            }
            crossings++;
            continue;
        }
        gg_game_act(&app->game, (gg_action)r.step[i].act);
        gg_game_animate(&app->game);
        acts++;
    }

    const uint64_t now = gg_state_hash(&app->game);
    SDL_Log("gigantima: replayed %d actions and %d crossings to turn %u",
            acts, crossings, app->game.turn);

    if (!r.has_hash) {
        SDL_Log("gigantima: the replay carries no hash to compare against; "
                "this world is %016llX", (unsigned long long)now);
        gg_replay_free(&r);
        return SDL_APP_SUCCESS;
    }
    const uint64_t was = r.hash;
    gg_replay_free(&r);

    if (now != was) {
        SDL_Log("gigantima: DIVERGED - the recording ended on %016llX and this "
                "run on %016llX", (unsigned long long)was,
                (unsigned long long)now);
        return SDL_APP_FAILURE;
    }
    SDL_Log("gigantima: identical - %016llX", (unsigned long long)now);
    return SDL_APP_SUCCESS;
}

// --- capturing what the game sounds like ------------------------------------
// One recording of every tune in turn, with the crossfades between them, taken
// out of the mixer rather than off a sound card: no device is opened, so the
// file is the same on a machine with speakers and one without, and nothing is
// lost to whatever a card happened to take while the file was being written.
static bool write_wav(const char *path, const int16_t *pcm, int frames, int rate) {
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) {
        SDL_Log("gigantima: cannot write %s: %s", path, SDL_GetError());
        return false;
    }
    const uint32_t data = (uint32_t)frames * 2u;
    const uint8_t head[] = {
        'R','I','F','F', 0,0,0,0, 'W','A','V','E', 'f','m','t',' ',
        16,0,0,0, 1,0, 1,0, 0,0,0,0, 0,0,0,0, 2,0, 16,0,
        'd','a','t','a', 0,0,0,0,
    };
    uint8_t h[sizeof head];
    SDL_memcpy(h, head, sizeof head);
    const uint32_t riff = 36u + data, byterate = (uint32_t)rate * 2u;
    SDL_memcpy(h + 4,  &riff, 4);
    SDL_memcpy(h + 24, &rate, 4);
    SDL_memcpy(h + 28, &byterate, 4);
    SDL_memcpy(h + 40, &data, 4);

    bool ok = SDL_WriteIO(io, h, sizeof h) == sizeof h;
    ok = ok && SDL_WriteIO(io, pcm, data) == data;
    SDL_CloseIO(io);
    return ok;
}

#define CAPTURE_SECONDS 8

static SDL_AppResult capture_music(gg_app *app) {
    if (!gg_audio_load(gg_asset_path("sounds/"))) {
        SDL_Log("gigantima: no sounds to record");
        return SDL_APP_FAILURE;
    }
    gg_audio_volumes(10, 0);

    const int rate = gg_audio_rate();
    const int tunes = gg_audio_tune_count();
    const int frames = rate * CAPTURE_SECONDS;
    int16_t *pcm = SDL_malloc((size_t)frames * (size_t)tunes * sizeof *pcm);
    if (!pcm) return SDL_APP_FAILURE;

    for (int i = 0; i < tunes; i++) {
        gg_audio_music_play(i);
        gg_audio_render(pcm + (size_t)i * (size_t)frames, frames);
        SDL_Log("gigantima: recorded %s", gg_audio_tune_name(i));
    }

    const bool ok = write_wav(app->music_path, pcm, frames * tunes, rate);
    if (ok)
        SDL_Log("gigantima: wrote %s - %d tunes, %d seconds each, %d Hz",
                app->music_path, tunes, CAPTURE_SECONDS, rate);
    SDL_free(pcm);
    gg_audio_quit();
    return ok ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    SDL_SetAppMetadata("Gigantima", "0.1.0", "dev.gavin.gigantima");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("gigantima: SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    gg_app *app = SDL_calloc(1, sizeof *app);
    if (!app) return SDL_APP_FAILURE;
    *appstate = app;

    app->shot_at = 40;
    app->profile = "Avatar";
    int scale = WINDOW_SCALE;
    bool want_fs = false, no_rumble = false;
    bool seed_set = false;

    for (int i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            app->seed = (uint32_t)SDL_strtoul(argv[++i], nullptr, 10);
            seed_set = true;   // so --seed 0 means 0, not "unset"
        } else if (SDL_strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
            app->profile = argv[++i];
        } else if (SDL_strcmp(argv[i], "--music") == 0 && i + 1 < argc) {
            app->music_path = argv[++i];
        } else if (SDL_strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            app->shot_path = argv[++i];
        } else if (SDL_strcmp(argv[i], "--shot-at") == 0 && i + 1 < argc) {
            app->shot_at = (uint32_t)SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--screen") == 0 && i + 1 < argc) {
            app->screen_name = argv[++i];
        } else if (SDL_strcmp(argv[i], "--turns") == 0 && i + 1 < argc) {
            app->turn_limit = (uint32_t)SDL_atoi(argv[++i]);
            app->turn_limit_set = true;
        } else if (SDL_strcmp(argv[i], "--pad-loop") == 0) {
            app->pad_loop = true;
        } else if (SDL_strcmp(argv[i], "--record") == 0 && i + 1 < argc) {
            app->record_path = argv[++i];
        } else if (SDL_strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            app->replay_path = argv[++i];
        } else if (SDL_strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            app->listen_ms = (uint32_t)SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--new") == 0) {
            app->force_new = true;
        } else if (SDL_strcmp(argv[i], "--map") == 0 && i + 1 < argc) {
            app->map_path = argv[++i];
        } else if (SDL_strcmp(argv[i], "--time") == 0 && i + 1 < argc) {
            int hh = 0, mm = 0;
            if (SDL_sscanf(argv[++i], "%d:%d", &hh, &mm) == 2 &&
                hh >= 0 && hh < 24 && mm >= 0 && mm < 60) {
                app->clock_min = hh * 60 + mm;
                app->clock_set = true;
            } else {
                SDL_Log("gigantima: --time wants HH:MM, got '%s'", argv[i]);
                return SDL_APP_FAILURE;
            }
        } else if (SDL_strcmp(argv[i], "--at") == 0 && i + 1 < argc) {
            if (SDL_sscanf(argv[++i], "%d,%d", &app->at_x, &app->at_y) == 2) {
                app->at_set = true;
            } else {
                SDL_Log("gigantima: --at wants X,Y (no spaces), got '%s'", argv[i]);
                return SDL_APP_FAILURE;
            }
        } else if (SDL_strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            scale = gg_clampi(SDL_atoi(argv[++i]), 1, 4);
        } else if (SDL_strcmp(argv[i], "--debug") == 0) {
            app->debug = true;
        } else if (SDL_strcmp(argv[i], "--play") == 0) {
            app->started = true;
        } else if (SDL_strcmp(argv[i], "--fullscreen") == 0) {
            want_fs = true;
        } else if (SDL_strcmp(argv[i], "--no-rumble") == 0) {
            no_rumble = true;
        } else if (SDL_strcmp(argv[i], "--help") == 0) {
            usage();
            return SDL_APP_SUCCESS;
        } else {
            SDL_Log("gigantima: unknown option '%s'", argv[i]);
            usage();
            return SDL_APP_FAILURE;
        }
    }

    // --shot captures whatever screen the other flags select, so `--shot`
    // alone photographs the title and `--play --shot` photographs the world.
    // That is what lets every screen be checked from a headless run, which is
    // the only way a UI screen gets verified without someone looking at it.

    if (!SDL_CreateWindowAndRenderer("Gigantima",
                                     GG_SCREEN_W * scale, GG_SCREEN_H * scale,
                                     SDL_WINDOW_RESIZABLE,
                                     &app->win, &app->ren)) {
        SDL_Log("gigantima: could not create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderVSync(app->ren, 1);

    // Everything draws in one fixed logical space and SDL scales it to the
    // window. LETTERBOX rather than INTEGER_SCALE: at 800x672 an integer-only
    // scale would jump straight from 1x to 2x and letterbox most of a 1080p
    // screen, which is a worse trade than a slightly soft edge.
    if (!SDL_SetRenderLogicalPresentation(app->ren, GG_SCREEN_W, GG_SCREEN_H,
                                          SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
        SDL_Log("gigantima: logical presentation failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!gg_assets_found()) {
        SDL_Log("gigantima: cannot find the art. Run tools/make_atlas.py, or set "
                "GIGANTIMA_ASSETS to the directory holding atlas_tiles.png.");
        return SDL_APP_FAILURE;
    }
    if (!gg_render_init(app->ren) || !gg_font_init(app->ren))
        return SDL_APP_FAILURE;

    gg_input_init(&app->in, no_rumble);

    // The content, **before** the world: who lives in it comes out of the book
    // and what haunts it comes out of the bestiary, so a world built while
    // those are still empty is a world with nobody and nothing in it. A world
    // with no book still runs - everybody greets and nobody has anything more
    // to add - so a missing file is a log line rather than a failure, the same
    // as a missing gamepad.
    if (!gg_dialogue_load(gg_asset_path("dialogue.txt")))
        SDL_Log("gigantima: no dialogue loaded; the town will be quiet");
    if (!gg_magic_load(gg_asset_path("spells.txt")))
        SDL_Log("gigantima: no spells loaded; nothing will answer to a word");
    if (!gg_bestiary_load(gg_asset_path("bestiary.txt")))
        SDL_Log("gigantima: no bestiary loaded; the hills will be empty");
    if (!gg_quests_load(gg_asset_path("quests.txt")))
        SDL_Log("gigantima: no quests loaded; nothing will be asked of anybody");

    // A replay decides everything about the world it plays into, so it runs
    // before one is built and leaves by the front door either way.
    if (app->replay_path) return run_replay(app);

    const uint32_t seed = seed_set ? app->seed
                                   : (uint32_t)SDL_GetPerformanceCounter();
    app->seed = seed;
    // Resume by default. A player who names their profile and runs the game
    // expects to be where they left off; starting over is the thing that has
    // to be asked for, not the thing that happens by accident.
    bool built = false;
    if (!app->force_new && !app->map_path &&
        gg_save_exists(gg_pref_path(), app->profile)) {
        built = gg_save_read(&app->game, gg_pref_path(), app->profile);
        app->loaded = built;
        if (!built)
            SDL_Log("gigantima: %s has a save that could not be read; "
                    "starting a new world", app->profile);
    }
    if (!built) {
        built = app->map_path
            ? gg_game_new_from_map(&app->game, app->map_path, app->profile, seed)
            : gg_game_new(&app->game, seed, app->profile);
    }
    if (!built) {
        SDL_Log("gigantima: could not build a world%s%s",
                app->map_path ? " from " : "", app->map_path ? app->map_path : "");
        return SDL_APP_FAILURE;
    }

    // Nothing below this needs a world, a window or a device.
    if (app->music_path) return capture_music(app);

    if (app->clock_set) {
        app->game.minutes = (uint32_t)app->clock_min;
        SDL_Log("gigantima: clock set to %02d:%02d",
                gg_game_hour(&app->game), gg_game_minute(&app->game));
    }

    if (app->at_set) {
        gg_actor *p = gg_player(&app->game);
        p->x = (int16_t)gg_clampi(app->at_x, 0, app->game.map.w - 1);
        p->y = (int16_t)gg_clampi(app->at_y, 0, app->game.map.h - 1);
        p->step = 0;
        SDL_Log("gigantima: avatar placed at %d,%d%s", p->x, p->y,
                gg_map_walkable(&app->game.map, p->x, p->y) ? "" : " (in terrain)");
    }

    // A silent game is a playable one, so a device that will not open is a log
    // line and not a failure - the same call the gamepad gets.
    if (!gg_audio_init(gg_asset_path("sounds/")))
        SDL_Log("gigantima: no audio; the world will be silent");

    gg_settings_load(&app->settings, gg_pref_file(GG_SETTINGS_FILE));
    // The keys, from the settings rather than from a switch in the input
    // layer. Called again by the options page whenever one is changed.
    gg_input_bind(&app->in, &app->settings);
    gg_font_scale(app->settings.text_scale);
    gg_debug_plain_colours(app->settings.plain_colours);
    gg_audio_volumes(app->settings.music, app->settings.effects);
    if (app->settings.rumble == false) app->in.no_rumble = true;
    if (no_rumble) app->settings.rumble = false;

    if (app->pad_loop && !pad_attach(app)) return SDL_APP_FAILURE;

    // Started here, after the world exists and before anything is given to it,
    // so the recording begins at the same state a replay will build.
    if (app->record_path) {
        const char *leaf = app->game.here[0] ? app->game.here : nullptr;
        if (gg_record_begin(&app->rec, app->record_path, &app->game, leaf))
            SDL_Log("gigantima: recording to %s", app->record_path);
    }

    // --play and --shot go straight into the world; anything else opens on
    // the title, which is where a player without command-line flags starts.
    app->have_game = true;
    gg_screen_id opening = app->started ? GG_SCREEN_PLAY : GG_SCREEN_TITLE;
    if (app->screen_name) {
        static const struct { const char *name; gg_screen_id id; } NAMED[] = {
            { "title",    GG_SCREEN_TITLE },
            { "profiles", GG_SCREEN_PROFILES },
            { "name",     GG_SCREEN_NAME },
            { "options",  GG_SCREEN_OPTIONS },
            { "keys",     GG_SCREEN_KEYS },
            { "pause",    GG_SCREEN_PAUSE },
            { "play",     GG_SCREEN_PLAY },
            // The pack is a mode of the world rather than a screen of its own,
            // but it is a page the player looks at, so --shot has to be able to
            // photograph it like the rest.
            { "pack",     GG_SCREEN_PLAY },
            // Like `pack`: a mode of the world rather than a screen, but a
            // page the player reads, so --shot has to be able to photograph it.
            { "talk",     GG_SCREEN_PLAY },
            { "party",    GG_SCREEN_PLAY },
            { "fight",    GG_SCREEN_PLAY },
            { "spells",   GG_SCREEN_PLAY },
            { "magic",    GG_SCREEN_PLAY },
            { "sheet",    GG_SCREEN_PLAY },
            { "journal",  GG_SCREEN_PLAY },
            { "travel",   GG_SCREEN_PLAY },
            { "return",   GG_SCREEN_PLAY },
            { "ending",   GG_SCREEN_PLAY },
        };
        bool known = false;
        for (size_t k = 0; k < GG_COUNTOF(NAMED); k++)
            if (SDL_strcmp(app->screen_name, NAMED[k].name) == 0) {
                opening = NAMED[k].id;
                known = true;
            }
        if (!known) {
            SDL_Log("gigantima: no screen called '%s'", app->screen_name);
            return SDL_APP_FAILURE;
        }
    }
    screen_go(app, opening);

    if (want_fs || (app->settings.fullscreen && !app->shot_path))
        want_fs = true;
    if (want_fs) toggle_fullscreen(app);
    if (app->debug) debug_open(app);

    app->last_ns = SDL_GetTicksNS();

    const char *rname = SDL_GetRendererName(app->ren);
    SDL_Log("gigantima: renderer '%s', %dx%d logical, seed %u",
            rname ? rname : "?", GG_SCREEN_W, GG_SCREEN_H, seed);
    SDL_Log("gigantima: map %dx%d, %d actors, start %d,%d",
            app->game.map.w, app->game.map.h, app->game.actors,
            app->game.map.start_x, app->game.map.start_y);
    if (app->loaded)
        SDL_Log("gigantima: resumed %s on day %u at %02d:%02d, turn %u",
                app->game.profile, app->game.day, gg_game_hour(&app->game),
                gg_game_minute(&app->game), app->game.turn);
    return SDL_APP_CONTINUE;
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    gg_app *app = appstate;

    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;

    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        // Closing the debug window puts the view away; closing the game
        // window quits.
        if (app->dbg_win && event->window.windowID == SDL_GetWindowID(app->dbg_win)) {
            app->debug = false;
            debug_close(app);
            return SDL_APP_CONTINUE;
        }
        return SDL_APP_SUCCESS;

    case SDL_EVENT_TEXT_INPUT:
        // Only ever collected while a name is being typed; SDL text input is
        // started and stopped with the screen.
        if (app->screens.id == GG_SCREEN_NAME)
            for (const char *c = event->text.text; *c; c++)
                if ((unsigned char)*c >= ' ' && (unsigned char)*c < 127)
                    gg_screens_type(&app->screens, *c);
        return SDL_APP_CONTINUE;

    case SDL_EVENT_KEY_DOWN:
        if (event->key.repeat && app->screens.id != GG_SCREEN_NAME) break;

        // The keys page is waiting to be told which key. Every key belongs to
        // it while it is - including the ones that would otherwise navigate,
        // because a player rebinding "walk north" has to be able to choose the
        // down arrow.
        if (app->screens.binding >= 0 && app->screens.id == GG_SCREEN_KEYS) {
            if (gg_screens_bind(&app->screens, &app->settings,
                                event->key.scancode)) {
                gg_settings_save(&app->settings, gg_pref_file(GG_SETTINGS_FILE));
                gg_input_bind(&app->in, &app->settings);
            }
            return SDL_APP_CONTINUE;
        }

        // A menu is showing: it takes the navigation keys, and the world does
        // not see them.
        if (app->screens.id != GG_SCREEN_PLAY) {
            // W and S steer a menu, but on the naming screen they are letters
            // somebody is trying to type - so there only the arrows navigate.
            const bool naming = app->screens.id == GG_SCREEN_NAME;
            switch (event->key.scancode) {
            case SDL_SCANCODE_UP:
                screen_nav(app, 0, -1); return SDL_APP_CONTINUE;
            case SDL_SCANCODE_DOWN:
                screen_nav(app, 0, 1);  return SDL_APP_CONTINUE;
            case SDL_SCANCODE_LEFT:
                screen_nav(app, -1, 0); return SDL_APP_CONTINUE;
            case SDL_SCANCODE_RIGHT:
                screen_nav(app, 1, 0);  return SDL_APP_CONTINUE;
            case SDL_SCANCODE_W:
                if (!naming) screen_nav(app, 0, -1);
                return SDL_APP_CONTINUE;
            case SDL_SCANCODE_S:
                if (!naming) screen_nav(app, 0, 1);
                return SDL_APP_CONTINUE;
            case SDL_SCANCODE_BACKSPACE:
                gg_screens_type(&app->screens, '\b'); return SDL_APP_CONTINUE;
            case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER:
                if (event->key.mod & SDL_KMOD_ALT) { toggle_fullscreen(app); break; }
                if (!screen_act(app, gg_screens_choose(&app->screens,
                        gg_pref_path(), &app->settings, app->have_game)))
                    return SDL_APP_SUCCESS;
                return SDL_APP_CONTINUE;
            case SDL_SCANCODE_ESCAPE:
                if (!screen_act(app, gg_screens_back(&app->screens)))
                    return SDL_APP_SUCCESS;
                return SDL_APP_CONTINUE;
            case SDL_SCANCODE_F11:
                toggle_fullscreen(app); return SDL_APP_CONTINUE;
            default:
                return SDL_APP_CONTINUE;
            }
        }

        // The world has ended, one way or the other. Any key leaves it, and
        // there is nothing else to do here - the world takes no more orders,
        // so a key that is not a way out is a key that does nothing at all.
        if (app->game.mode == GG_MODE_ENDING ||
            app->game.mode == GG_MODE_GAMEOVER) {
            if (event->key.scancode == SDL_SCANCODE_F11) {
                toggle_fullscreen(app);
                return SDL_APP_CONTINUE;
            }
            screen_go(app, GG_SCREEN_TITLE);
            return SDL_APP_CONTINUE;
        }

        switch (event->key.scancode) {
        case SDL_SCANCODE_ESCAPE:
            // Escape backs out of whatever is open before it reaches for the
            // pause menu: a conversation or the pack first, and only then the
            // menu. Losing a session to a stray keypress is not acceptable now
            // that there is a session to lose, and neither is having to press
            // a different key to close each thing.
            if (app->game.mode == GG_MODE_CONVERSE ||
                app->game.mode == GG_MODE_PACK ||
                app->game.mode == GG_MODE_SPELL ||
                app->game.mode == GG_MODE_JOURNAL ||
                app->game.mode == GG_MODE_SHEET) {
                act(app, GG_ACT_WAIT);
                return SDL_APP_CONTINUE;
            }
            screen_go(app, GG_SCREEN_PAUSE);
            return SDL_APP_CONTINUE;
        case SDL_SCANCODE_F11:
            toggle_fullscreen(app);
            return SDL_APP_CONTINUE;
        case SDL_SCANCODE_F5:
            if (app->started) {
                if (gg_save_write(&app->game, gg_pref_path(), app->game.profile))
                    gg_log(&app->game, "Thy journey is recorded.");
                else
                    gg_log(&app->game, "The journey could not be recorded.");
            }
            return SDL_APP_CONTINUE;
        case SDL_SCANCODE_F1:
            app->debug = !app->debug;
            if (app->debug) debug_open(app);
            else            debug_close(app);
            return SDL_APP_CONTINUE;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            if (event->key.mod & SDL_KMOD_ALT) {
                toggle_fullscreen(app);
                return SDL_APP_CONTINUE;
            }
            break;
        default:
            break;
        }
        break;

    default:
        break;
    }

    // Anything the frontend did not claim belongs to the input layer.
    gg_input_event(&app->in, event);
    return SDL_APP_CONTINUE;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
// The pad, in a menu. The keyboard is handled in SDL_AppEvent because it
// arrives as discrete key presses; the pad has to be sampled, because holding a
// stick has to repeat on a timer rather than fire every frame.
//
// Returns false when the application should exit.
static bool pad_menu(gg_app *app) {
    const gg_nav nav = gg_input_nav(&app->in);

    switch (nav) {
    case GG_NAV_UP:     screen_nav(app, 0, -1); break;
    case GG_NAV_DOWN:   screen_nav(app, 0, 1);  break;
    case GG_NAV_LEFT:   screen_nav(app, -1, 0); break;
    case GG_NAV_RIGHT:  screen_nav(app, 1, 0);  break;
    case GG_NAV_ERASE:  gg_screens_type(&app->screens, '\b'); break;

    case GG_NAV_ACCEPT:
    case GG_NAV_CHOOSE:
        // Both choose; they differ only on the naming screen, where A types the
        // letter under the cursor and Start means "that is the name", wherever
        // in the alphabet the cursor happens to be sitting.
        if (nav == GG_NAV_ACCEPT) gg_screens_ready(&app->screens);
        return screen_act(app, gg_screens_choose(&app->screens, gg_pref_path(),
                                                 &app->settings, app->have_game));
    case GG_NAV_BACK:
        return screen_act(app, gg_screens_back(&app->screens));

    case GG_NAV_NONE:
        break;
    }
    return true;
}

// Returns false when the application should exit.
static bool step_once(gg_app *app) {
    app->frames++;
    if (app->pad_loop) {
        pad_bot(app);
        if (app->bot_done || app->bot_stuck) return false;   // by the front door
    }
    gg_input_tick(&app->in);

    if (app->screens.id != GG_SCREEN_PLAY) return pad_menu(app);

    // Start pauses. The pad has no Escape, and reaching for the keyboard to put
    // a controller game down is exactly the seam this is meant to close.
    if (gg_input_take_pause(&app->in)) {
        screen_go(app, GG_SCREEN_PAUSE);
        return true;
    }

    // The world has ended. Anything on the pad leaves it, the same as anything
    // on the keyboard - a controller must never be the one that cannot get out
    // of a screen.
    if (app->started && (app->game.mode == GG_MODE_ENDING ||
                         app->game.mode == GG_MODE_GAMEOVER)) {
        if (gg_input_take(&app->in) != GG_ACT_NONE)
            screen_go(app, GG_SCREEN_TITLE);
        gg_game_animate(&app->game);
        return true;
    }

    // A way out was stepped on. The simulation named the map; this is where
    // maps live, which is knowledge src/core is not allowed to have. Maps ship
    // in assets/maps/, and one saved by the editor sits beside the profiles -
    // both are tried, so a map you authored is playable without installing it.
    if (app->started && app->game.want_travel) {
        const char *leaf = app->game.travel_to;
        const int tx = app->game.travel_x, ty = app->game.travel_y;
        if (gg_game_travel(&app->game, gg_asset_path_for_map(leaf), tx, ty)) {
            SDL_Log("gigantima: walked into %s (%s)", app->game.map.name, leaf);
            gg_record_travel(&app->rec, leaf, tx, ty);
        } else {
            SDL_Log("gigantima: cannot walk into %s", leaf);
        }
        app->game.want_travel = false;
    }

    if (app->started) {
        // Whatever the world had to say since the last tick. The simulation
        // does not know audio exists; this is the only place the two meet.
        gg_event heard[GG_EVENTS_MAX];
        const int n = gg_events_drain(&app->game, heard, GG_EVENTS_MAX);
        for (int i = 0; i < n; i++) gg_audio_play(heard[i]);
        if (n > 0 || app->frames % 30 == 0)
            gg_audio_music_for(&app->game);

        const gg_action a = gg_input_take(&app->in);
        if (a != GG_ACT_NONE) {
            act(app, a);
            // The other half of the sheet's scroll. How far it can go depends
            // on the font, which is this side of the line and not the
            // simulation's business - see gg_game_act.
            if (app->game.mode == GG_MODE_SHEET) {
                const int most = gg_ui_sheet_rows(&app->game) - gg_ui_sheet_fits();
                if (app->game.sheet_cursor > most)
                    app->game.sheet_cursor = most > 0 ? most : 0;
            }
            if (app->game.blocked_bump) {
                app->game.blocked_bump = false;
                gg_input_rumble(&app->in, 0x2000, 0x1000, 60);
            }
        }
        gg_game_animate(&app->game);
    }
    return true;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    gg_app *app = appstate;

    // Capture mode: run the world forward, draw once, write, exit. A menu
    // screen has no world to run forward, so the loop below simply does not
    // turn - `started` is false on every screen but the world.
    if (app->shot_path) {
        // Walk east so the shot is not always the spawn tile, through the
        // simulation rather than around it, so the capture exercises the same
        // path a player would.
        //
        // A blocked move costs no turn - deliberately, so a player cannot
        // starve against a rock - which means "walk east until turn N" never
        // terminates the moment something is in the way. It spun forever the
        // first time a seed put a wall to the east. So: fall back to waiting,
        // which always advances, and bound the loop regardless.
        uint32_t guard = app->shot_at * 4 + 64;
        while (app->started && app->game.turn < app->shot_at && guard-- > 0) {
            const uint32_t before = app->game.turn;
            act(app, GG_ACT_E);
            if (app->game.turn == before) act(app, GG_ACT_WAIT);
            gg_game_animate(&app->game);
        }
        if (app->started && app->game.turn < app->shot_at)
            SDL_Log("gigantima: capture stalled at turn %u of %u",
                    app->game.turn, app->shot_at);
        // Opened after the turns have been played, not before: the loop above
        // acts on the world, and in the pack or a conversation those same
        // actions would close it again.
        if (app->screen_name && SDL_strcmp(app->screen_name, "pack") == 0)
            app->game.mode = GG_MODE_PACK;

        // Walks to the way out and steps on it, so a crossing can be
        // photographed. The walk is the ordinary one and the crossing is the
        // ordinary crossing; only the destination is chosen.
        if (app->screen_name && SDL_strcmp(app->screen_name, "travel") == 0) {
            if (walk_to_the_way_out(app)) cross_over(app);
        }

        // Out and back again, which is a different claim from `travel`: not
        // that the crossing works, but that the map behind you kept what you
        // did in it. Something is put on the floor, the Avatar walks out of the
        // map and back into it, and the pile has to still be lying where it was
        // left. The item's own verification, run on the shipped world in the
        // real binary rather than on a fixture.
        if (app->screen_name && SDL_strcmp(app->screen_name, "return") == 0) {
            int dx = -1, dy = -1;
            for (int leg = 0; leg < 2; leg++) {
                if (!walk_to_the_way_out(app)) {
                    SDL_Log("gigantima: leg %d found no way out", leg + 1);
                    break;
                }
                // On the way out, something is left on the last tile before
                // the gate - which is the tile the way back comes out beside,
                // so the frame taken at the end has it in it.
                if (leg == 0) {
                    const gg_actor *p = gg_player_const(&app->game);
                    dx = p->from_x;
                    dy = p->from_y;
                    gg_ground_drop(&app->game.map, dx, dy, GG_ITEM_SILVER, 3);
                    SDL_Log("gigantima: left 3 silver at %d,%d in %s",
                            dx, dy, app->game.map.name);
                }
                if (!cross_over(app)) break;
            }

            // A step clear of the gate, because the way back comes out
            // directly below the tile the silver is on and the Avatar's head
            // would be standing in front of it in the photograph.
            act(app, GG_ACT_S);
            for (int i = 0; i < GG_STEP_TICKS + 2; i++)
                gg_game_animate(&app->game);

            const int pile = dx >= 0 ? gg_ground_at(&app->game.map, dx, dy) : -1;
            if (pile >= 0)
                SDL_Log("gigantima: found %d silver still at %d,%d",
                        app->game.map.ground[pile].count, dx, dy);
            else
                SDL_Log("gigantima: the silver left at %d,%d is gone", dx, dy);
        }

        // The whole story, played through in one run of the real game: ask the
        // vale what is wrong, walk the north road, kill the man at the end of
        // it, carry the silver home and hand it back. Nothing here reaches into
        // the world - it walks, talks, fights, picks things up and hands one
        // over, all through gg_game_act - so a story that cannot be finished by
        // a player cannot be finished by this either.
        if (app->screen_name && SDL_strcmp(app->screen_name, "ending") == 0) {
            gg_game *g = &app->game;

            // Ask everybody, three times over: what Nystul teaches is what
            // makes Gwenno worth talking to, so one sweep is not enough for a
            // rumour to cross a square.
            for (int sweep = 0; sweep < 3; sweep++)
                for (int i = 0; i < g->actors; i++) {
                    if (i == g->player || !g->actor[i].active ||
                        g->actor[i].hostile) continue;
                    if (!gg_dialogue_find(g->actor[i].name)) continue;
                    ask_everything(app, i);
                }
            SDL_Log("gigantima: %d words learned, %d walking with the Avatar",
                    g->knownn, gg_party_size(g));

            // Whatever the vale handed over, in hand - and only then the
            // hills, and only if the vale handed over nothing. Hunting first
            // meant fighting the first brigand bare-handed with a hammer in
            // the pack, which is how this arrived at the stones on four
            // health.
            ready_the_best(app);
            hunt_for_gear(app, 6);
            drink_if_hurt(app);
            SDL_Log("gigantima: leaving the vale with attack %d, guard %d, "
                    "%d/%d health", gg_attack_power(g, g->player),
                    gg_guard_power(g, g->player), gg_player_const(g)->hp,
                    gg_player_const(g)->hp_max);

            if (walk_to_the_way_out(app) && cross_over(app)) {
                ready_the_best(app);
                SDL_Log("gigantima: attack %d, guard %d",
                        gg_attack_power(g, g->player),
                        gg_guard_power(g, g->player));

                // The man in the ring. Walking at him is striking him.
                int rugar = -1;
                for (int i = 0; i < g->actors; i++)
                    if (g->actor[i].active && g->actor[i].hostile) rugar = i;
                if (rugar > 0) {
                    for (int i = 0; i < 400 && g->actor[rugar].active &&
                                    g->mode == GG_MODE_PLAY; i++) {
                        drink_if_hurt(app);
                        if (!step_toward(app, g->actor[rugar].x,
                                         g->actor[rugar].y)) break;
                    }
                    SDL_Log("gigantima: Rugar %s",
                            g->actor[rugar].active ? "is still standing"
                                                   : "has fallen");

                    // What he was carrying.
                    walk_to(app, g->actor[rugar].x, g->actor[rugar].y, 0, 40);
                    for (int i = 0; i < 4; i++) act(app, GG_ACT_GET);
                    SDL_Log("gigantima: %d silver in the pack",
                            gg_pack_count(g, GG_ITEM_SILVER));
                }

                // Home with it, and hand it over.
                if (walk_to_the_way_out(app) && cross_over(app)) {
                    int iolo = -1;
                    for (int i = 0; i < g->actors; i++)
                        if (SDL_strcmp(g->actor[i].name, "Iolo") == 0) iolo = i;
                    if (iolo > 0) ask_everything(app, iolo);
                }
            }

            const char *quest = nullptr, *words = nullptr;
            if (gg_ending(g, &quest, &words))
                SDL_Log("gigantima: the story ended - %s: %s", quest, words);
            else
                SDL_Log("gigantima: the story did not end (mode %d, %u turns)",
                        (int)g->mode, g->turn);
        }

        // Learns what the vale has to teach and kills something, so the
        // journal has a story in it to photograph. Everything after the
        // learning is the ordinary machine: the stages advance because their
        // conditions hold, not because anything was told to advance them.
        if (app->screen_name && SDL_strcmp(app->screen_name, "journal") == 0) {
            static const char *const HEARD[] = {
                "caravan", "road", "stones", "light", "north",
                "runes", "mani", "in", "lor", "vas", "flam", "nox", "an",
            };
            for (size_t k = 0; k < GG_COUNTOF(HEARD); k++)
                gg_learn(&app->game, HEARD[k]);
            app->game.slain = 5;
            for (int i = 0; i < app->game.actors; i++)
                if (i != app->game.player && app->game.actor[i].active &&
                    !app->game.actor[i].hostile) {
                    gg_party_join(&app->game, i);
                    break;
                }
            gg_quests_tick(&app->game);
            app->game.mode = GG_MODE_JOURNAL;
        }

        // Learns every rune and takes a few reagents, so the book can be
        // photographed with something in it. What it then shows is the
        // ordinary book: only spells whose runes are known, and the prices
        // coloured by what is actually in the pack.
        if (app->screen_name && SDL_strcmp(app->screen_name, "spells") == 0) {
            for (int i = 0; i < gg_magic_runes(); i++)
                gg_learn(&app->game, gg_magic_rune(i)->word);
            gg_pack_add(&app->game, GG_ITEM_GINSENG, 2);
            gg_pack_add(&app->game, GG_ITEM_ASH, 1);
            gg_pack_add(&app->game, GG_ITEM_NIGHTSHADE, 2);
            gg_pack_add(&app->game, GG_ITEM_BLOODMOSS, 2);
            act(app, GG_ACT_CAST);
        }

        // Puts two brigands within arm's reach and swings once, so a fight can
        // be photographed. Everything after the placing is the ordinary
        // simulation, so the frame shows a real exchange of blows.
        if (app->screen_name && SDL_strcmp(app->screen_name, "fight") == 0) {
            const gg_actor *pl = gg_player_const(&app->game);
            gg_spawn_named(&app->game, "BRIGAND", pl->x + 1, pl->y);
            gg_spawn_named(&app->game, "SLINGER", pl->x + 3, pl->y - 1);
            act(app, GG_ACT_FIGHT);
            act(app, GG_ACT_FIGHT);
        }

        // Who the Avatar is, with something on every part of the page: two
        // companions under different orders, a hand and an arm with something
        // in them, a light burning, and a vocabulary worth scrolling.
        if (app->screen_name && SDL_strcmp(app->screen_name, "sheet") == 0) {
            int took = 0;
            for (int i = 0; i < app->game.actors && took < 2; i++)
                if (i != app->game.player && app->game.actor[i].active &&
                    gg_party_join(&app->game, i)) {
                    if (took == 1) app->game.actor[i].stance = GG_STANCE_BACK;
                    took++;
                }
            for (int i = 0; i < gg_magic_runes(); i++)
                gg_learn(&app->game, gg_magic_rune(i)->word);
            gg_pack_add(&app->game, GG_ITEM_SWORD, 1);
            gg_pack_add(&app->game, GG_ITEM_SHIELD, 1);
            gg_pack_add(&app->game, GG_ITEM_ASH, 1);
            static const gg_item_id READY[] = { GG_ITEM_SWORD, GG_ITEM_SHIELD };
            for (size_t k = 0; k < GG_COUNTOF(READY); k++) {
                app->game.pack_cursor = gg_pack_find(&app->game, READY[k]);
                app->game.mode = GG_MODE_PACK;
                act(app, GG_ACT_EQUIP);
            }
            app->game.mode = GG_MODE_PLAY;
            for (int i = 0; i < gg_magic_spells(); i++)
                if (gg_magic_spell(i)->effect == GG_SPELL_LIGHT)
                    gg_cast(&app->game, i);
            act(app, GG_ACT_SHEET);
        }

        // Two brigands, a ward and a word that puts one of them down. Both of
        // the effects that have to be *seen* rather than read are in this one
        // frame: the sleeping one is drawn dim, and the ward says so in the
        // band. Cast through the ordinary casting, so the frame is real.
        if (app->screen_name && SDL_strcmp(app->screen_name, "magic") == 0) {
            // Wherever there is room for them - the vale is full of houses,
            // and a brigand that would not fit is a brigand that is not there.
            const gg_actor *pl = gg_player_const(&app->game);
            static const int NEAR[][2] = { {2,0}, {2,-2}, {0,2}, {-2,0},
                                           {2,2}, {0,-2}, {-2,2}, {-2,-2} };
            int put = 0;
            for (size_t k = 0; k < GG_COUNTOF(NEAR) && put < 2; k++)
                if (gg_spawn_named(&app->game, "BRIGAND",
                                   pl->x + NEAR[k][0], pl->y + NEAR[k][1]) >= 0)
                    put++;
            for (int i = 0; i < gg_magic_runes(); i++)
                gg_learn(&app->game, gg_magic_rune(i)->word);
            gg_pack_add(&app->game, GG_ITEM_GINSENG, 2);
            gg_pack_add(&app->game, GG_ITEM_BLOODMOSS, 2);
            gg_pack_add(&app->game, GG_ITEM_NIGHTSHADE, 2);
            gg_player(&app->game)->level = 4;
            app->game.exp = gg_level_cost(4);
            for (int i = 0; i < gg_magic_spells(); i++) {
                const gg_spell *sp = gg_magic_spell(i);
                if (sp->effect == GG_SPELL_WARD || sp->effect == GG_SPELL_SLEEP)
                    gg_cast(&app->game, i);
            }
        }

        // Takes the two nearest townsfolk along, so the line and the party's
        // rows in the HUD can be photographed. The walk that follows is the
        // ordinary one, so what the frame shows is real following.
        if (app->screen_name && SDL_strcmp(app->screen_name, "party") == 0) {
            int took = 0;
            for (int i = 0; i < app->game.actors && took < 2; i++)
                if (i != app->game.player && app->game.actor[i].active &&
                    gg_party_join(&app->game, i)) {
                    // The second of them under an order, so the frame shows
                    // what one looks like. A player gives it by asking; this is
                    // a photograph, and there is nobody here to ask.
                    if (took == 1) app->game.actor[i].stance = GG_STANCE_STAND;
                    took++;
                }
            // Walk until something is in the way rather than into it: a
            // blocked move costs no turn, so the frame would otherwise carry a
            // log of nothing but "a brick wall bars the way".
            static const gg_action WAY[] = { GG_ACT_S, GG_ACT_E };
            for (size_t d = 0; d < GG_COUNTOF(WAY); d++)
                for (int i = 0; i < 10; i++) {
                    const uint32_t was = app->game.turn;
                    act(app, WAY[d]);
                    if (app->game.turn == was) break;
                }
        }

        // Walks up to the first townsperson and talks, so the conversation
        // panel can be photographed without anyone driving the game.
        if (app->screen_name && SDL_strcmp(app->screen_name, "talk") == 0) {
            for (int i = 0; i < app->game.actors; i++) {
                if (i == app->game.player || !app->game.actor[i].active) continue;
                gg_actor *pl = gg_player(&app->game);
                pl->x = app->game.actor[i].x;
                pl->y = (int16_t)(app->game.actor[i].y + 1);
                pl->step = 0;
                act(app, GG_ACT_N);
                break;
            }
        }

        // With --debug, capture the debug window rather than the game: that is
        // the view you wanted a picture of, and it is otherwise unreachable
        // from a headless run. Same convention as gavaga.
        SDL_Renderer *target = app->ren;
        if (app->debug) {
            debug_open(app);
            if (app->dbg_ren) {
                gg_debug_draw(&app->game, app->dbg_ren, app->seed);
                target = app->dbg_ren;
            }
        }
        if (target == app->ren) draw(app);

        const bool ok = save_shot(target, app->shot_path);
        SDL_Log("gigantima: wrote %s (%s) at turn %u", app->shot_path,
                target == app->ren ? "game" : "debug", app->game.turn);
        return ok ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }

    // Headless play: run the turns and leave by the front door, so everything
    // that happens on a normal exit happens here too.
    if (app->turn_limit_set) {
        while (app->game.turn < app->turn_limit) {
            const uint32_t before = app->game.turn;

            // Walking rather than waiting, with a wait as the fallback: this
            // used to stand still for the whole run, which exercised almost
            // nothing and made an audio capture of it silent, because waiting
            // is silent. A blocked move costs no turn, so the fallback is what
            // keeps this from spinning against a wall.
            act(app, GG_ACT_E);
            if (app->game.turn == before) act(app, GG_ACT_WAIT);
            gg_game_animate(&app->game);
            if (app->game.turn == before) break;   // cannot advance; do not spin

            // Whatever that turn had to say. Drained here as well as in the
            // frame loop because this path never reaches one, and a headless
            // run is exactly where an audio capture is taken.
            gg_event heard[GG_EVENTS_MAX];
            const int n = gg_events_drain(&app->game, heard, GG_EVENTS_MAX);
            for (int i = 0; i < n; i++) gg_audio_play(heard[i]);
        }
        gg_audio_music_for(&app->game);
        SDL_Log("gigantima: played to turn %u", app->game.turn);

        // --listen holds the process open afterwards so the audio device is
        // actually asked for something. Without it the turns run in a
        // millisecond and a capture is an empty file - which is how this was
        // found. The audio equivalent of --shot, and it exists for the same
        // reason: a thing you cannot capture is a thing you cannot check.
        if (app->listen_ms > 0) {
            const uint64_t until = SDL_GetTicks() + app->listen_ms;
            while (SDL_GetTicks() < until) SDL_Delay(10);
            SDL_Log("gigantima: listened for %u ms", app->listen_ms);
        }
        return SDL_APP_SUCCESS;
    }

    const uint64_t now = SDL_GetTicksNS();
    uint64_t delta = now - app->last_ns;
    app->last_ns = now;

    // Never make up more than a few ticks at once: after a stall it is better
    // to drop time than to fast-forward the animation.
    const uint64_t max_delta = GG_TICK_NS * GG_MAX_CATCHUP_TICKS;
    if (delta > max_delta) delta = max_delta;
    app->accum_ns += delta;

    while (app->accum_ns >= GG_TICK_NS) {
        app->accum_ns -= GG_TICK_NS;
        // A menu row can ask to quit, and the pad reaches those rows from
        // inside the tick loop rather than from an event.
        if (!step_once(app)) return SDL_APP_SUCCESS;
    }

    draw(app);
    SDL_RenderPresent(app->ren);

    if (app->dbg_win) {
        gg_debug_draw(&app->game, app->dbg_ren, app->seed);
        SDL_RenderPresent(app->dbg_ren);
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)result;
    gg_app *app = appstate;
    if (!app) return;

    // Put the windowed geometry back before exiting, or a borderless
    // fullscreen session leaves the next run opening the size of a monitor.
    if (app->faux_fs && app->win) {
        SDL_SetWindowBordered(app->win, true);
        SDL_SetWindowSize(app->win, app->pre_fs_w, app->pre_fs_h);
        SDL_SetWindowPosition(app->win, app->pre_fs_x, app->pre_fs_y);
    }

    // The recording, closed with the hash of the world it ended on. Before the
    // save, so a session that is being recorded is written down even if saving
    // it fails.
    if (app->rec.open) {
        const uint64_t h = gg_record_end(&app->rec, &app->game);
        SDL_Log("gigantima: recorded %u actions, ending on %016llX",
                app->rec.acts, (unsigned long long)h);
    }

    // Save on the way out, so "pick it up later" needs no thought. Not for a
    // capture run: that world was fast-forwarded by the shot loop and is not
    // one anybody was playing.
    if (app->started && !app->shot_path && app->game.map.cell) {
        if (gg_save_write(&app->game, gg_pref_path(), app->game.profile))
            SDL_Log("gigantima: saved %s on exit", app->game.profile);
    }

    gg_dialogue_clear();
    gg_magic_clear();
    gg_bestiary_clear();
    gg_quests_clear();
    gg_audio_quit();
    gg_input_quit(&app->in);
    gg_game_free(&app->game);
    debug_close(app);
    gg_font_quit();
    gg_render_quit();
    if (app->ren) SDL_DestroyRenderer(app->ren);
    if (app->win) SDL_DestroyWindow(app->win);
    SDL_free(app);
}
