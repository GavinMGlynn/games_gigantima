// main.c - the level editor's window.
//
// Everything this does to a map is a call into src/editor/gg_edit.c. This file
// is a window, a mouse and a palette, and nothing else - which is why the
// item's verification is a test that authors a map through the same calls and
// plays it, rather than a screenshot of a window with a map in it.
//
// The same shape as the game's frontend: SDL callbacks, one window, no
// simulation inside the drawing.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "core/gg_common.h"
#include "core/gg_world.h"
#include "editor/gg_edit.h"
#include "core/gg_maptext.h"
#include "gfx/gg_atlas.h"
#include "gfx/gg_font.h"
#include "gfx/gg_render.h"
#include "platform/gg_paths.h"

#define WIN_W      1280
#define WIN_H       800
#define PALETTE_W   220
#define STATUS_H     72
#define VIEW_X     PALETTE_W
#define VIEW_W     (WIN_W - PALETTE_W)
#define VIEW_H     (WIN_H - STATUS_H)

// A whole tile is 32px; the editor draws at half that so a useful amount of map
// is on screen at once. Not a free parameter in the game - see CLAUDE.md - but
// here it is a zoom, and the map is still stored at one tile per cell.
#define ZOOM_MIN 1
#define ZOOM_MAX 4

// What a prompt is asking for. One modal state rather than one flag per
// question, so "is something being typed" is a single thing to check.
typedef enum {
    GG_ASK_NOTHING,
    GG_ASK_OPEN,        // which map to read
    GG_ASK_SAVE_AS,     // what to call it
    GG_ASK_NAME,        // the map, a region, a person - whichever the tool is about
    GG_ASK_LINK,        // where the next way out leads
} gg_ask;

static const SDL_Color INK   = { 226, 216, 190, 255 };
static const SDL_Color DIM   = { 138, 132, 116, 255 };
static const SDL_Color AMBER = { 217, 145,  63, 255 };
static const SDL_Color WARN  = { 200,  96,  72, 255 };
static const SDL_Color MOSS  = { 124, 184,  84, 255 };

typedef struct {
    SDL_Window   *win;
    SDL_Renderer *ren;
    gg_editor     ed;

    int  zoom;          // 1..4, quarter-tiles to tiles
    int  cam_x, cam_y;  // top-left tile of the view
    int  hover_x, hover_y;
    bool painting, erasing;
    bool grid;
    bool show_problems;

    char file[GG_EDIT_PATH_MAX];
    uint64_t frames;

    // --shot FILE draws one frame with no window shown and exits, the same
    // affordance the game has and for the same reason: a window nobody can
    // photograph is a window nobody can check.
    const char *shot_path;

    // --export writes whatever was opened as text and leaves. Converting a map
    // is then one command, which is what makes a binary map in a repository
    // stop being a blob nobody can review.
    const char *export_path;

    // The one modal thing in the editor: a line of text with a question over
    // it. Everything that needed a name went through the command line before
    // this - which is why the editor could open one map, the one it was
    // started with, and why every region in an authored map was called
    // "town 1".
    gg_ask   ask;
    char     entry[GG_EDIT_PATH_MAX];
    int      entry_n;

    // What the prompt offers, for the ones that are about a file: every map
    // this machine has, found rather than typed. Up and down walk them.
    char (*found)[GG_EDIT_PATH_MAX];
    int  founds, found_at;
} gg_app;

// The pixel size of one tile at the current zoom.
static int tile_px(const gg_app *a) { return 8 * a->zoom; }

// ---------------------------------------------------------------------------
// Asking for a line of text
// ---------------------------------------------------------------------------
// Every map on this machine: the ones that ship, and the ones somebody has
// saved beside their journeys. Found rather than typed, because "open by name"
// is only useful to somebody who already knows the name.
static void forget_found(gg_app *a) {
    SDL_free(a->found);
    a->found = nullptr;
    a->founds = a->found_at = 0;
}

static void find_maps(gg_app *a) {
    forget_found(a);
    a->found = SDL_calloc(64, sizeof *a->found);
    if (!a->found) return;

    // The shipped maps, then whatever is beside the profiles. Both directories
    // are asked for by the same paths the game uses, so a map the editor can
    // see is a map the game can open.
    const char *dirs[2] = { gg_asset_path("maps/"), gg_pref_file("") };
    for (int d = 0; d < 2 && a->founds < 64; d++) {
        int n = 0;
        char **hits = SDL_GlobDirectory(dirs[d], "*.ggmap", 0, &n);
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; hits && i < n && a->founds < 64; i++)
                SDL_snprintf(a->found[a->founds++], GG_EDIT_PATH_MAX, "%s%s",
                             dirs[d], hits[i]);
            SDL_free(hits);
            hits = pass == 0 ? SDL_GlobDirectory(dirs[d], "*.map.txt", 0, &n)
                             : nullptr;
            if (!hits) n = 0;
        }
    }
}

static void ask_for(gg_app *a, gg_ask what, const char *prefill) {
    a->ask = what;
    SDL_strlcpy(a->entry, prefill ? prefill : "", sizeof a->entry);
    a->entry_n = (int)SDL_strlen(a->entry);
    if (what == GG_ASK_OPEN || what == GG_ASK_SAVE_AS) find_maps(a);
    else forget_found(a);
    if (a->win) SDL_StartTextInput(a->win);
}

static void ask_done(gg_app *a) {
    a->ask = GG_ASK_NOTHING;
    a->entry[0] = '\0';
    a->entry_n = 0;
    forget_found(a);
    if (a->win) SDL_StopTextInput(a->win);
}

// What the question reads as, and what the answer is called - both in one place
// so a new prompt is one case rather than three.
static const char *ask_says(gg_ask what) {
    switch (what) {
    case GG_ASK_OPEN:    return "Open which map?";
    case GG_ASK_SAVE_AS: return "Save it as what?";
    case GG_ASK_NAME:    return "Call it what?";
    case GG_ASK_LINK:    return "Ways out lead to which map?";
    default:             return "";
    }
}

// ---------------------------------------------------------------------------
// Where the mouse is
// ---------------------------------------------------------------------------
static bool mouse_to_tile(const gg_app *a, float mx, float my, int *tx, int *ty) {
    if (mx < VIEW_X || my >= VIEW_H) return false;
    const int t = tile_px(a);
    *tx = a->cam_x + (int)((mx - VIEW_X) / (float)t);
    *ty = a->cam_y + (int)(my / (float)t);
    return gg_map_in_bounds(&a->ed.map, *tx, *ty);
}

static void clamp_camera(gg_app *a) {
    const int t = tile_px(a);
    const int across = VIEW_W / t, down = VIEW_H / t;
    const int max_x = a->ed.map.w - across, max_y = a->ed.map.h - down;
    a->cam_x = gg_clampi(a->cam_x, 0, max_x > 0 ? max_x : 0);
    a->cam_y = gg_clampi(a->cam_y, 0, max_y > 0 ? max_y : 0);
}

// Acts on whatever was typed. Every question ends here, so there is one place
// that knows what an answer means.
static void ask_answer(gg_app *a) {
    switch (a->ask) {
    case GG_ASK_OPEN:
        if (gg_edit_load(&a->ed, a->entry))
            SDL_strlcpy(a->file, a->ed.path, sizeof a->file);
        break;

    case GG_ASK_SAVE_AS:
        if (gg_edit_save(&a->ed, a->entry))
            SDL_strlcpy(a->file, a->ed.path, sizeof a->file);
        break;

    // One key names whatever the current tool is about, rather than one key
    // per kind of name. What it means is the tool, which the status line is
    // already showing.
    case GG_ASK_NAME:
        switch (a->ed.tool) {
        case GG_TOOL_REGION:
            gg_edit_name_region(&a->ed, a->hover_x, a->hover_y, a->entry);
            break;
        case GG_TOOL_ACTOR:
        case GG_TOOL_SCHEDULE: {
            const int who = gg_edit_actor_at(&a->ed, a->hover_x, a->hover_y);
            if (who >= 0) a->ed.actor = who;
            gg_edit_name_actor(&a->ed, a->entry);
            break;
        }
        default:
            gg_edit_name_map(&a->ed, a->entry);
            break;
        }
        break;

    // "vale.ggmap 40 8" - the map and where in it you arrive, because a way out
    // that does not say where it lands is half a way out.
    case GG_ASK_LINK: {
        char to[GG_MAP_NAME_MAX] = { 0 };
        int x = a->ed.portal_x, y = a->ed.portal_y;
        if (SDL_sscanf(a->entry, "%47s %d %d", to, &x, &y) >= 1)
            gg_edit_link_to(&a->ed, to, x, y);
        break;
    }

    default:
        break;
    }
    ask_done(a);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
static void draw_map(gg_app *a) {
    const gg_map *m = &a->ed.map;
    const int t = tile_px(a);
    const int across = VIEW_W / t + 1, down = VIEW_H / t + 1;

    SDL_Texture *tiles = gg_render_tiles();
    SDL_Texture *props = gg_render_props();
    SDL_Texture *items = gg_render_items();
    SDL_Texture *actors = gg_render_actors();

    // Ground first, then everything standing on it, in the map's own order -
    // the editor is a plan view, so there is no depth sorting to do.
    for (int dy = 0; dy < down; dy++) {
        for (int dx = 0; dx < across; dx++) {
            const int wx = a->cam_x + dx, wy = a->cam_y + dy;
            const gg_cell *c = gg_map_at_const(m, wx, wy);
            if (!c) continue;
            const gg_rect *r = &GG_TILE_RECT[c->terrain];
            const SDL_FRect src = { (float)r->x, (float)r->y, (float)r->w, (float)r->h };
            const SDL_FRect dst = { (float)(VIEW_X + dx * t), (float)(dy * t),
                                    (float)t, (float)t };
            SDL_RenderTexture(a->ren, tiles, &src, &dst);
        }
    }

    for (int dy = 0; dy < down; dy++) {
        for (int dx = 0; dx < across; dx++) {
            const int wx = a->cam_x + dx, wy = a->cam_y + dy;
            const gg_cell *c = gg_map_at_const(m, wx, wy);
            if (!c || !GG_HAS_PROP(c)) continue;

            const gg_prop_id p = GG_PROP_OF(c);
            const gg_rect *r = &GG_PROP_RECT[p];
            const gg_prop_size *s = &GG_PROP_SIZE[p];
            const SDL_FRect src = { (float)r->x, (float)r->y, (float)r->w, (float)r->h };
            const SDL_FRect dst = {
                (float)(VIEW_X + (dx - s->anchor_x) * t),
                (float)((dy - s->anchor_y) * t),
                (float)(s->tiles_w * t), (float)(s->tiles_h * t),
            };
            SDL_RenderTexture(a->ren, props, &src, &dst);
        }
    }

    for (int i = 0; i < m->grounds; i++) {
        const gg_ground_item *g = &m->ground[i];
        const int dx = g->x - a->cam_x, dy = g->y - a->cam_y;
        if (dx < 0 || dy < 0 || dx >= across || dy >= down) continue;
        const gg_rect *r = &GG_ITEM_RECT[g->kind];
        const SDL_FRect src = { (float)r->x, (float)r->y, (float)r->w, (float)r->h };
        const SDL_FRect dst = { (float)(VIEW_X + dx * t),
                                (float)(dy * t - (r->h / GG_TILE - 1) * t),
                                (float)t, (float)(r->h * t / GG_TILE) };
        SDL_RenderTexture(a->ren, items, &src, &dst);
    }

    for (int i = 0; i < m->actors; i++) {
        const gg_map_actor *p = &m->actor[i];
        const int dx = p->x - a->cam_x, dy = p->y - a->cam_y;
        if (dx < 0 || dy < 0 || dx >= across || dy >= down) continue;

        const gg_rect *r = &GG_ACTOR_RECT[p->art < GG_ACTOR_COUNT ? p->art : 0];
        const SDL_FRect src = { (float)r->x, (float)(r->y + GG_FACE_DOWN * GG_ACTOR_FRAME),
                                (float)GG_ACTOR_FRAME, (float)GG_ACTOR_FRAME };
        const SDL_FRect dst = { (float)(VIEW_X + dx * t - t / 2),
                                (float)(dy * t - t), (float)(t * 2), (float)(t * 2) };
        SDL_RenderTexture(a->ren, actors, &src, &dst);

        // The chosen person, and the day they keep, drawn over everything -
        // a schedule is invisible otherwise, and invisible is unauthorable.
        if (i == a->ed.actor) {
            SDL_SetRenderDrawBlendMode(a->ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(a->ren, 217, 145, 63, 220);
            const SDL_FRect box = { (float)(VIEW_X + dx * t), (float)(dy * t),
                                    (float)t, (float)t };
            SDL_RenderRect(a->ren, &box);

            SDL_SetRenderDrawColor(a->ren, 124, 184, 84, 200);
            for (int k = 0; k < p->schedn; k++) {
                const int sx = p->sched[k].x - a->cam_x;
                const int sy = p->sched[k].y - a->cam_y;
                const SDL_FRect at = { (float)(VIEW_X + sx * t + t / 4),
                                       (float)(sy * t + t / 4),
                                       (float)(t / 2), (float)(t / 2) };
                SDL_RenderFillRect(a->ren, &at);
                // A line from where they are to where they go, so the shape of
                // a day reads at a glance.
                SDL_RenderLine(a->ren, (float)(VIEW_X + dx * t + t / 2),
                               (float)(dy * t + t / 2),
                               (float)(VIEW_X + sx * t + t / 2),
                               (float)(sy * t + t / 2));
            }
        }
    }

    // Regions, as outlines with their names in the corner.
    SDL_SetRenderDrawBlendMode(a->ren, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < m->regions; i++) {
        const gg_region *r = &m->region[i];
        const SDL_FRect box = {
            (float)(VIEW_X + (r->x - a->cam_x) * t), (float)((r->y - a->cam_y) * t),
            (float)(r->w * t), (float)(r->h * t),
        };
        SDL_SetRenderDrawColor(a->ren, 120, 170, 230, 60);
        SDL_RenderFillRect(a->ren, &box);
        SDL_SetRenderDrawColor(a->ren, 150, 200, 255, 200);
        SDL_RenderRect(a->ren, &box);
        gg_font_draw(a->ren, (int)box.x + 4, (int)box.y + 2, INK, r->name);
    }

    if (a->grid && t >= 12) {
        SDL_SetRenderDrawColor(a->ren, 255, 255, 255, 28);
        for (int dx = 0; dx <= across; dx++)
            SDL_RenderLine(a->ren, (float)(VIEW_X + dx * t), 0,
                           (float)(VIEW_X + dx * t), (float)VIEW_H);
        for (int dy = 0; dy <= down; dy++)
            SDL_RenderLine(a->ren, (float)VIEW_X, (float)(dy * t),
                           (float)WIN_W, (float)(dy * t));
    }

    // Ways out, so a link is something you can see rather than something you
    // have to remember placing.
    for (int i = 0; i < m->portals; i++) {
        const gg_portal *w = &m->portal[i];
        const int dx = w->x - a->cam_x, dy = w->y - a->cam_y;
        if (dx < 0 || dy < 0 || dx >= across || dy >= down) continue;
        SDL_SetRenderDrawColor(a->ren, 220, 200, 120, 210);
        const SDL_FRect box = { (float)(VIEW_X + dx * t), (float)(dy * t),
                                (float)t, (float)t };
        SDL_RenderRect(a->ren, &box);
        gg_font_draw(a->ren, (int)box.x + 2, (int)box.y - 12, AMBER, w->to);
    }

    // Where a new game begins.
    {
        const int dx = m->start_x - a->cam_x, dy = m->start_y - a->cam_y;
        SDL_SetRenderDrawColor(a->ren, 124, 184, 84, 230);
        const SDL_FRect box = { (float)(VIEW_X + dx * t), (float)(dy * t),
                                (float)t, (float)t };
        SDL_RenderRect(a->ren, &box);
        gg_font_draw(a->ren, (int)box.x + 2, (int)box.y - 12, MOSS, "start");
    }

    // The tile under the mouse.
    if (gg_map_in_bounds(m, a->hover_x, a->hover_y)) {
        const int dx = a->hover_x - a->cam_x, dy = a->hover_y - a->cam_y;
        SDL_SetRenderDrawColor(a->ren, 255, 255, 255, 150);
        const SDL_FRect box = { (float)(VIEW_X + dx * t), (float)(dy * t),
                                (float)t, (float)t };
        SDL_RenderRect(a->ren, &box);
    }

    // A region being dragged out, while it is being dragged.
    if (a->ed.drag) {
        const int x0 = a->ed.drag_x < a->hover_x ? a->ed.drag_x : a->hover_x;
        const int y0 = a->ed.drag_y < a->hover_y ? a->ed.drag_y : a->hover_y;
        const int x1 = a->ed.drag_x > a->hover_x ? a->ed.drag_x : a->hover_x;
        const int y1 = a->ed.drag_y > a->hover_y ? a->ed.drag_y : a->hover_y;
        const SDL_FRect box = {
            (float)(VIEW_X + (x0 - a->cam_x) * t), (float)((y0 - a->cam_y) * t),
            (float)((x1 - x0 + 1) * t), (float)((y1 - y0 + 1) * t),
        };
        SDL_SetRenderDrawColor(a->ren, 150, 200, 255, 90);
        SDL_RenderFillRect(a->ren, &box);
    }
}

static void draw_palette(gg_app *a) {
    const int line = gg_font_height();
    SDL_SetRenderDrawBlendMode(a->ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(a->ren, 22, 18, 14, 255);
    const SDL_FRect panel = { 0, 0, (float)PALETTE_W, (float)WIN_H };
    SDL_RenderFillRect(a->ren, &panel);
    SDL_SetRenderDrawColor(a->ren, 95, 72, 46, 255);
    SDL_RenderLine(a->ren, (float)PALETTE_W, 0, (float)PALETTE_W, (float)WIN_H);

    int y = 10;
    gg_font_draw(a->ren, 12, y, AMBER, "GIGANTIMA EDITOR");
    y += line + 8;

    for (int i = 0; i < GG_TOOL_COUNT; i++) {
        const bool here = (i == (int)a->ed.tool);
        if (here) {
            SDL_SetRenderDrawColor(a->ren, 217, 145, 63, 45);
            const SDL_FRect bar = { 4, (float)(y - 3), (float)(PALETTE_W - 8),
                                    (float)(line + 6) };
            SDL_RenderFillRect(a->ren, &bar);
        }
        gg_font_printf(a->ren, 12, y, here ? AMBER : INK, "%d  %s", i + 1,
                       gg_tool_name((gg_tool)i));
        y += line + 6;
    }

    y += 8;
    gg_font_draw(a->ren, 12, y, DIM, "brush");
    y += line;
    gg_font_printf(a->ren, 12, y, INK, "%s", gg_edit_brush_name(&a->ed));
    y += line + 4;
    gg_font_draw(a->ren, 12, y, DIM, "[ ] to change");
    y += line + 12;

    if (a->ed.actor >= 0 && a->ed.actor < a->ed.map.actors) {
        const gg_map_actor *p = &a->ed.map.actor[a->ed.actor];
        gg_font_draw(a->ren, 12, y, DIM, "chosen");
        y += line;
        gg_font_printf(a->ren, 12, y, MOSS, "%s", p->name);
        y += line;
        gg_font_printf(a->ren, 12, y, DIM, "%u hours set", p->schedn);
        y += line + 12;
    }

    static const char *const KEYS[] = {
        "S save    shift-S as",
        "O open    N new",
        "Z undo    Y redo",
        "F fill    E name it",
        "L where a way out goes",
        "G grid    C check",
        "+ - zoom  arrows scroll",
    };
    for (size_t k = 0; k < GG_COUNTOF(KEYS); k++) {
        gg_font_draw(a->ren, 12, y, DIM, KEYS[k]);
        y += line;
    }
}

static void draw_status(gg_app *a) {
    const int line = gg_font_height();
    const int top = VIEW_H;
    SDL_SetRenderDrawColor(a->ren, 22, 18, 14, 255);
    const SDL_FRect band = { (float)VIEW_X, (float)top,
                             (float)VIEW_W, (float)STATUS_H };
    SDL_RenderFillRect(a->ren, &band);
    SDL_SetRenderDrawColor(a->ren, 95, 72, 46, 255);
    SDL_RenderLine(a->ren, (float)VIEW_X, (float)top, (float)WIN_W, (float)top);

    int y = top + 8;
    gg_font_printf(a->ren, VIEW_X + 12, y, a->ed.dirty ? WARN : INK, "%s%s",
                   a->file[0] ? a->file : "(unsaved)", a->ed.dirty ? " *" : "");
    gg_font_printf(a->ren, VIEW_X + 420, y, DIM, "%dx%d   %d people   %d regions",
                   a->ed.map.w, a->ed.map.h, a->ed.map.actors, a->ed.map.regions);
    y += line + 2;

    gg_font_printf(a->ren, VIEW_X + 12, y, DIM,
                   "at %d,%d   zoom %dx   %d to take back, %d to put back",
                   a->hover_x, a->hover_y, a->zoom, gg_edit_undos(&a->ed),
                   gg_edit_redos(&a->ed));
    y += line + 2;
    gg_font_printf(a->ren, VIEW_X + 12, y, MOSS, "%s", a->ed.say);

    if (a->ask != GG_ASK_NOTHING) return;   // the question is drawn over the lot

    if (a->show_problems) {
        char problems[GG_EDIT_PROBLEMS_MAX][GG_EDIT_SAY_MAX];
        const int n = gg_edit_check(&a->ed, problems);
        int py = 12;
        if (n == 0) {
            gg_font_draw(a->ren, VIEW_X + 12, py, MOSS,
                         "nothing wrong with it - press C to put this away");
        } else {
            gg_font_printf(a->ren, VIEW_X + 12, py, WARN, "%d problems:", n);
            py += line;
            for (int i = 0; i < n && i < GG_EDIT_PROBLEMS_MAX; i++) {
                gg_font_printf(a->ren, VIEW_X + 24, py, WARN, "%s", problems[i]);
                py += line;
            }
        }
    }
}

// The one modal thing in the editor: a question, a line being typed, and - when
// the question is about a file - every map this machine has, to walk with the
// arrows rather than type out.
static void draw_ask(gg_app *a) {
    if (a->ask == GG_ASK_NOTHING) return;
    const int line = gg_font_height();
    const int rows = a->founds > 8 ? 8 : a->founds;
    const int h = (line + 10) * 3 + rows * line + 20;
    const SDL_FRect box = { (float)(VIEW_X + 40), (float)((VIEW_H - h) / 2),
                            (float)(VIEW_W - 80), (float)h };

    SDL_SetRenderDrawBlendMode(a->ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(a->ren, 18, 14, 10, 244);
    SDL_RenderFillRect(a->ren, &box);
    SDL_SetRenderDrawColor(a->ren, 217, 145, 63, 200);
    SDL_RenderRect(a->ren, &box);

    const int x = (int)box.x + 14;
    int y = (int)box.y + 12;
    gg_font_draw(a->ren, x, y, AMBER, ask_says(a->ask));
    y += line + 8;

    // The line, with a block for a cursor so an empty answer still shows where
    // the typing goes.
    gg_font_printf(a->ren, x, y, INK, "%s", a->entry);
    const SDL_FRect caret = { (float)(x + gg_font_width(a->entry)), (float)y,
                              (float)(line / 2), (float)line };
    SDL_SetRenderDrawColor(a->ren, 226, 216, 190,
                           (a->frames / 20) % 2 ? 40 : 200);
    SDL_RenderFillRect(a->ren, &caret);
    y += line + 10;

    for (int i = 0; i < rows; i++) {
        const int at = (a->found_at + i) % (a->founds > 0 ? a->founds : 1);
        gg_font_printf(a->ren, x + 12, y, i == 0 ? INK : DIM, "%s",
                       a->found[at]);
        y += line;
    }
    if (a->founds > 0) y += 6;

    gg_font_draw(a->ren, x, y, DIM,
                 a->founds > 0 ? "type it, or walk them with up and down   "
                                 "enter to do it   escape to leave it"
                               : "enter to do it   escape to leave it");
}

// ---------------------------------------------------------------------------
// SDL
// ---------------------------------------------------------------------------
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    gg_app *app = SDL_calloc(1, sizeof *app);
    if (!app) return SDL_APP_FAILURE;
    *appstate = app;

    app->zoom = 2;
    app->grid = true;

    const char *open_path = nullptr;
    int w = 128, h = 112, tool = -1, link_x = 0, link_y = 0;
    const char *link_to = nullptr;
    gg_ask ask_at_start = GG_ASK_NOTHING;
    for (int i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "--open") == 0 && i + 1 < argc)
            open_path = argv[++i];
        else if (SDL_strcmp(argv[i], "--size") == 0 && i + 2 < argc) {
            w = SDL_atoi(argv[++i]);
            h = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--link") == 0 && i + 3 < argc) {
            // Where the portal tool's ways out lead. On the command line
            // because the editor has no text entry - a named gap.
            link_to = argv[++i];
            link_x = SDL_atoi(argv[++i]);
            link_y = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--ask") == 0 && i + 1 < argc) {
            // Opens a prompt on the way in, so the one modal thing in the
            // editor can be photographed like every other page in the game.
            const char *what = argv[++i];
            ask_at_start = SDL_strcmp(what, "open") == 0    ? GG_ASK_OPEN
                         : SDL_strcmp(what, "save") == 0    ? GG_ASK_SAVE_AS
                         : SDL_strcmp(what, "name") == 0    ? GG_ASK_NAME
                         : SDL_strcmp(what, "link") == 0    ? GG_ASK_LINK
                         : GG_ASK_NOTHING;
            if (ask_at_start == GG_ASK_NOTHING) {
                SDL_Log("gigantima: no prompt called '%s'", what);
                return SDL_APP_FAILURE;
            }
        } else if (SDL_strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            app->shot_path = argv[++i];
        } else if (SDL_strcmp(argv[i], "--tool") == 0 && i + 1 < argc) {
            tool = gg_clampi(SDL_atoi(argv[++i]), 0, GG_TOOL_COUNT - 1);
        } else if (SDL_strcmp(argv[i], "--export") == 0 && i + 1 < argc) {
            app->export_path = argv[++i];
        } else if (SDL_strcmp(argv[i], "--help") == 0) {
            SDL_Log("gigantima_editor [--open FILE] [--size W H] [--tool N]\n"
                    "                 [--link MAP X Y] [--shot FILE.bmp]\n"
                    "                 [--export FILE.map.txt]\n"
                    "\n"
                    "--open takes either form of map; a name ending in "
                    ".map.txt is\nread and written as text.");
            return SDL_APP_SUCCESS;
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("gigantima: SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (!SDL_CreateWindowAndRenderer("Gigantima - editor", WIN_W, WIN_H,
                                     SDL_WINDOW_RESIZABLE,
                                     &app->win, &app->ren)) {
        SDL_Log("gigantima: could not open a window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderVSync(app->ren, 1);
    SDL_SetRenderLogicalPresentation(app->ren, WIN_W, WIN_H,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);

    if (!gg_assets_found()) {
        SDL_Log("gigantima: cannot find the assets");
        return SDL_APP_FAILURE;
    }
    if (!gg_font_init(app->ren) || !gg_render_init(app->ren)) {
        SDL_Log("gigantima: could not load the art");
        return SDL_APP_FAILURE;
    }

    if (open_path) {
        if (!gg_edit_load(&app->ed, open_path)) return SDL_APP_FAILURE;
        SDL_strlcpy(app->file, open_path, sizeof app->file);
    } else if (!gg_edit_new(&app->ed, w, h)) {
        return SDL_APP_FAILURE;
    }
    if (link_to) gg_edit_link_to(&app->ed, link_to, link_x, link_y);
    if (ask_at_start != GG_ASK_NOTHING)
        ask_for(app, ask_at_start,
                ask_at_start == GG_ASK_OPEN ? gg_asset_path("maps/") : "");
    if (tool >= 0) gg_edit_tool(&app->ed, (gg_tool)tool);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    gg_app *app = appstate;

    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;

    case SDL_EVENT_MOUSE_MOTION: {
        int tx, ty;
        if (mouse_to_tile(app, event->motion.x, event->motion.y, &tx, &ty)) {
            app->hover_x = tx;
            app->hover_y = ty;
            // Painting is a drag, so a held button keeps applying as the mouse
            // moves - a tile-at-a-time editor is unusable for a field of grass.
            if (app->painting) gg_edit_apply(&app->ed, tx, ty);
            if (app->erasing) gg_edit_erase(&app->ed, tx, ty);
        }
        return SDL_APP_CONTINUE;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        int tx, ty;
        if (!mouse_to_tile(app, event->button.x, event->button.y, &tx, &ty))
            return SDL_APP_CONTINUE;
        if (event->button.button == SDL_BUTTON_LEFT) {
            if (app->ed.tool == GG_TOOL_REGION) {
                gg_edit_drag_start(&app->ed, tx, ty);
            } else {
                app->painting = true;
                gg_edit_stroke(&app->ed, true);
                gg_edit_apply(&app->ed, tx, ty);
            }
        } else if (event->button.button == SDL_BUTTON_RIGHT) {
            app->erasing = true;
            gg_edit_stroke(&app->ed, true);
            gg_edit_erase(&app->ed, tx, ty);
        }
        return SDL_APP_CONTINUE;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (event->button.button == SDL_BUTTON_LEFT) {
            if (app->ed.drag) gg_edit_drag_end(&app->ed, app->hover_x, app->hover_y);
            app->painting = false;
        }
        if (event->button.button == SDL_BUTTON_RIGHT) app->erasing = false;
        // The drag is over, so the next one is a new thing to undo.
        if (!app->painting && !app->erasing) gg_edit_stroke(&app->ed, false);
        return SDL_APP_CONTINUE;
    }

    case SDL_EVENT_MOUSE_WHEEL:
        gg_edit_brush(&app->ed, event->wheel.y > 0 ? 1 : -1);
        return SDL_APP_CONTINUE;

    // Everything a person types while a question is open goes into the answer,
    // and nothing else happens until they say so or give up.
    case SDL_EVENT_TEXT_INPUT:
        if (app->ask != GG_ASK_NOTHING) {
            const int room = (int)sizeof app->entry - 1 - app->entry_n;
            const int want = (int)SDL_strlen(event->text.text);
            if (want > 0 && want <= room) {
                SDL_memcpy(app->entry + app->entry_n, event->text.text,
                           (size_t)want);
                app->entry_n += want;
                app->entry[app->entry_n] = '\0';
            }
        }
        return SDL_APP_CONTINUE;

    case SDL_EVENT_KEY_DOWN:
        if (app->ask != GG_ASK_NOTHING) {
            switch (event->key.scancode) {
            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_KP_ENTER:
                ask_answer(app);
                break;
            case SDL_SCANCODE_ESCAPE:
                ask_done(app);
                break;
            case SDL_SCANCODE_BACKSPACE:
                // Back over one *character*, not one byte: the entry is UTF-8
                // and a name may not be in this alphabet.
                while (app->entry_n > 0) {
                    app->entry_n--;
                    if ((app->entry[app->entry_n] & 0xC0) != 0x80) break;
                }
                app->entry[app->entry_n] = '\0';
                break;
            case SDL_SCANCODE_UP:
            case SDL_SCANCODE_DOWN:
                // Walk what was found rather than typing a path out. This is
                // the half of "open by name" that makes it usable.
                if (app->founds > 0) {
                    const int by = event->key.scancode == SDL_SCANCODE_DOWN ? 1 : -1;
                    app->found_at = ((app->found_at + by) % app->founds +
                                     app->founds) % app->founds;
                    SDL_strlcpy(app->entry, app->found[app->found_at],
                                sizeof app->entry);
                    app->entry_n = (int)SDL_strlen(app->entry);
                }
                break;
            default:
                break;
            }
            return SDL_APP_CONTINUE;
        }

        if (event->key.repeat && event->key.scancode != SDL_SCANCODE_LEFT &&
            event->key.scancode != SDL_SCANCODE_RIGHT &&
            event->key.scancode != SDL_SCANCODE_UP &&
            event->key.scancode != SDL_SCANCODE_DOWN)
            return SDL_APP_CONTINUE;

        switch (event->key.scancode) {
        case SDL_SCANCODE_1: case SDL_SCANCODE_2: case SDL_SCANCODE_3:
        case SDL_SCANCODE_4: case SDL_SCANCODE_5: case SDL_SCANCODE_6:
        case SDL_SCANCODE_7: case SDL_SCANCODE_8: {
            // Scancodes are unsigned, so the subtraction is done in int
            // rather than left to widen on its own.
            const int which = (int)event->key.scancode - (int)SDL_SCANCODE_1;
            if (which >= 0 && which < GG_TOOL_COUNT)
                gg_edit_tool(&app->ed, (gg_tool)which);
            break;
        }
        case SDL_SCANCODE_LEFTBRACKET:  gg_edit_brush(&app->ed, -1); break;
        case SDL_SCANCODE_RIGHTBRACKET: gg_edit_brush(&app->ed, 1); break;

        case SDL_SCANCODE_LEFT:  app->cam_x -= 2; clamp_camera(app); break;
        case SDL_SCANCODE_RIGHT: app->cam_x += 2; clamp_camera(app); break;
        case SDL_SCANCODE_UP:    app->cam_y -= 2; clamp_camera(app); break;
        case SDL_SCANCODE_DOWN:  app->cam_y += 2; clamp_camera(app); break;

        case SDL_SCANCODE_EQUALS:
            app->zoom = gg_clampi(app->zoom + 1, ZOOM_MIN, ZOOM_MAX);
            clamp_camera(app);
            break;
        case SDL_SCANCODE_MINUS:
            app->zoom = gg_clampi(app->zoom - 1, ZOOM_MIN, ZOOM_MAX);
            clamp_camera(app);
            break;

        case SDL_SCANCODE_G: app->grid = !app->grid; break;
        case SDL_SCANCODE_C: app->show_problems = !app->show_problems; break;

        case SDL_SCANCODE_Z: gg_edit_undo(&app->ed); break;
        case SDL_SCANCODE_Y: gg_edit_redo(&app->ed); break;
        case SDL_SCANCODE_F: gg_edit_fill(&app->ed, app->hover_x, app->hover_y); break;

        // One key for "name the thing this tool is about", and one for where a
        // way out leads. Both were command-line arguments until there was
        // anywhere to type.
        case SDL_SCANCODE_E: ask_for(app, GG_ASK_NAME, ""); break;
        case SDL_SCANCODE_L: ask_for(app, GG_ASK_LINK, app->ed.portal_to); break;

        case SDL_SCANCODE_S: {
            // Shift asks what to call it; plain S writes it back where it came
            // from, or beside the profiles the first time - a directory that
            // exists and is writable on every platform this runs on.
            if (event->key.mod & SDL_KMOD_SHIFT) {
                ask_for(app, GG_ASK_SAVE_AS,
                        app->file[0] ? app->file : gg_pref_file("authored.ggmap"));
                break;
            }
            const char *where = app->file[0] ? app->file
                                             : gg_pref_file("authored.ggmap");
            if (gg_edit_save(&app->ed, where))
                SDL_strlcpy(app->file, app->ed.path, sizeof app->file);
            break;
        }
        case SDL_SCANCODE_O:
            ask_for(app, GG_ASK_OPEN,
                    app->file[0] ? app->file : gg_asset_path("maps/"));
            break;
        case SDL_SCANCODE_N:
            gg_edit_new(&app->ed, app->ed.map.w, app->ed.map.h);
            app->file[0] = '\0';
            break;

        case SDL_SCANCODE_ESCAPE:
            return SDL_APP_SUCCESS;
        default:
            break;
        }
        return SDL_APP_CONTINUE;

    default:
        return SDL_APP_CONTINUE;
    }
}

// Reads back the current target. Must run before SDL_RenderPresent.
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

SDL_AppResult SDL_AppIterate(void *appstate) {
    gg_app *app = appstate;
    app->frames++;

    SDL_SetRenderDrawColor(app->ren, 12, 14, 11, 255);
    SDL_RenderClear(app->ren);

    if (app->ed.open) {
        // Centred on the start, so a capture shows the part of the map
        // somebody actually authored rather than its top-left corner.
        if (app->shot_path && app->frames == 1) {
            const int t = tile_px(app);
            app->cam_x = app->ed.map.start_x - (VIEW_W / t) / 2;
            app->cam_y = app->ed.map.start_y - (VIEW_H / t) / 2;
            clamp_camera(app);
            app->hover_x = app->ed.map.start_x;
            app->hover_y = app->ed.map.start_y;
            if (app->ed.map.actors > 0) app->ed.actor = 0;
        }
        draw_map(app);
        draw_status(app);
        draw_ask(app);
    }
    draw_palette(app);

    if (app->export_path) {
        const bool ok = gg_map_write_text(&app->ed.map, app->export_path);
        SDL_Log("gigantima: %s %s", ok ? "wrote" : "could not write",
                app->export_path);
        return ok ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }

    if (app->shot_path) {
        const bool ok = save_shot(app->ren, app->shot_path);
        SDL_Log("gigantima: wrote %s", app->shot_path);
        return ok ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }

    SDL_RenderPresent(app->ren);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)result;
    gg_app *app = appstate;
    if (!app) return;

    gg_edit_close(&app->ed);
    gg_render_quit();
    if (app->ren) gg_font_quit_renderer(app->ren);
    gg_font_quit();
    if (app->ren) SDL_DestroyRenderer(app->ren);
    if (app->win) SDL_DestroyWindow(app->win);
    SDL_free(app);
}
