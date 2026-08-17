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

    bool     started;            // false while the title screen is up
    bool     quit;

    // Borderless fullscreen, and the windowed geometry to come back to.
    bool faux_fs;
    int  pre_fs_x, pre_fs_y, pre_fs_w, pre_fs_h;

    // Headless capture: --shot <file.bmp> [--shot-at <turn>] runs the world
    // forward, writes one frame and exits. For eyeballing changes without a
    // window, and for the CI smoke test.
    const char *shot_path;
    uint32_t    shot_at;

    uint32_t    seed;
    const char *profile;
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

    if (!app->started) {
        gg_ui_title(app->ren, app->frames, app->profile);
        return;
    }

    gg_render_world(&app->game, app->ren);
    gg_ui_hud(&app->game, app->ren);
    if (app->game.mode == GG_MODE_CONVERSE)
        gg_ui_converse(&app->game, app->ren);
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
static void usage(void) {
    SDL_Log("usage: gigantima [--profile NAME] [--seed N] [--play] [--debug]\n"
            "                 [--scale N] [--fullscreen] [--no-rumble]\n"
            "                 [--shot FILE.bmp] [--shot-at TURN]");
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
        } else if (SDL_strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            app->shot_path = argv[++i];
        } else if (SDL_strcmp(argv[i], "--shot-at") == 0 && i + 1 < argc) {
            app->shot_at = (uint32_t)SDL_atoi(argv[++i]);
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

    const uint32_t seed = seed_set ? app->seed
                                   : (uint32_t)SDL_GetPerformanceCounter();
    app->seed = seed;
    if (!gg_game_new(&app->game, seed, app->profile)) {
        SDL_Log("gigantima: could not build a world");
        return SDL_APP_FAILURE;
    }

    if (want_fs) toggle_fullscreen(app);
    if (app->debug) debug_open(app);

    app->last_ns = SDL_GetTicksNS();

    const char *rname = SDL_GetRendererName(app->ren);
    SDL_Log("gigantima: renderer '%s', %dx%d logical, seed %u",
            rname ? rname : "?", GG_SCREEN_W, GG_SCREEN_H, seed);
    SDL_Log("gigantima: map %dx%d, %d actors, start %d,%d",
            app->game.map.w, app->game.map.h, app->game.actors,
            app->game.map.start_x, app->game.map.start_y);
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

    case SDL_EVENT_KEY_DOWN:
        if (event->key.repeat) break;
        switch (event->key.scancode) {
        case SDL_SCANCODE_ESCAPE:
            return SDL_APP_SUCCESS;
        case SDL_SCANCODE_F11:
            toggle_fullscreen(app);
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
            if (!app->started) {
                app->started = true;
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
static void step_once(gg_app *app) {
    app->frames++;
    gg_input_tick(&app->in);

    if (app->started) {
        const gg_action a = gg_input_take(&app->in);
        if (a != GG_ACT_NONE) {
            gg_game_act(&app->game, a);
            if (app->game.blocked_bump) {
                app->game.blocked_bump = false;
                gg_input_rumble(&app->in, 0x2000, 0x1000, 60);
            }
        }
        gg_game_animate(&app->game);
    }
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    gg_app *app = appstate;

    // Capture mode: run the world forward, draw once, write, exit.
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
            gg_game_act(&app->game, GG_ACT_E);
            if (app->game.turn == before) gg_game_act(&app->game, GG_ACT_WAIT);
            gg_game_animate(&app->game);
        }
        if (app->started && app->game.turn < app->shot_at)
            SDL_Log("gigantima: capture stalled at turn %u of %u",
                    app->game.turn, app->shot_at);
        draw(app);
        const bool ok = save_shot(app->ren, app->shot_path);
        SDL_Log("gigantima: wrote %s at turn %u", app->shot_path, app->game.turn);
        return ok ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
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
        step_once(app);
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

    gg_input_quit(&app->in);
    gg_game_free(&app->game);
    debug_close(app);
    gg_font_quit();
    gg_render_quit();
    if (app->ren) SDL_DestroyRenderer(app->ren);
    if (app->win) SDL_DestroyWindow(app->win);
    SDL_free(app);
}
