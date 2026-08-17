// gg_debug.c - whole-map overview, actor roster and clock, in their own window.
#include "debug/gg_debug.h"
#include "gfx/gg_font.h"

static const SDL_Color INK   = { 220, 214, 198, 255 };
static const SDL_Color DIM   = { 138, 132, 116, 255 };
static const SDL_Color AMBER = { 217, 145,  63, 255 };
static const SDL_Color CYAN  = {  70, 179, 201, 255 };

// The overview is a pixel per tile, drawn into a streaming texture and blitted
// once. One SDL_RenderPoint per tile would be ~30,000 draw calls a frame for a
// 192x160 map, which is enough to halve the frame rate of the window it is
// meant to be diagnosing.
#define MAP_PANE_W 420
#define MAP_PANE_H 400

static SDL_Texture *g_map;
static SDL_Renderer *g_ren;
static int g_map_w, g_map_h;

bool gg_debug_init(SDL_Renderer *ren) {
    g_ren = ren;
    return true;
}

void gg_debug_quit(SDL_Renderer *ren) {
    if (ren != g_ren) return;
    SDL_DestroyTexture(g_map);
    g_map = nullptr;
    g_ren = nullptr;
    g_map_w = g_map_h = 0;
}

// Flat colours per terrain: this view is for reading shape, so it wants
// contrast between classes, not fidelity to the art.
static uint32_t terrain_colour(const gg_cell *c) {
    if (c->flags & GG_CELL_BLOCKED) return 0xFF503C28u;   // masonry
    if (c->flags & GG_CELL_DOOR)    return 0xFF00D0FFu;
    switch (c->terrain) {
    case GG_TILE_WATER:      return 0xFF298497u;
    case GG_TILE_WATER_DEEP: return 0xFF17384Fu;
    case GG_TILE_SAND:       return 0xFFF3D69Eu;
    case GG_TILE_DESERT:     return 0xFFF7BC76u;
    case GG_TILE_MOUNTAIN:   return 0xFFA47B4Bu;
    case GG_TILE_CLIFF:      return 0xFF5F342Au;
    case GG_TILE_ROAD:       return 0xFFA8825Au;
    case GG_TILE_DIRT:
    case GG_TILE_EARTH_DARK: return 0xFF6E5130u;
    case GG_TILE_FARMLAND:   return 0xFF8A633Du;
    default:                 return 0xFF47832Fu;          // grass
    }
}

static void rebuild_map_texture(const gg_game *g, SDL_Renderer *ren) {
    if (g_map && g_map_w == g->map.w && g_map_h == g->map.h) return;

    SDL_DestroyTexture(g_map);
    g_map = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, g->map.w, g->map.h);
    if (!g_map) {
        SDL_Log("gigantima: debug map texture failed: %s", SDL_GetError());
        return;
    }
    SDL_SetTextureScaleMode(g_map, SDL_SCALEMODE_NEAREST);
    g_map_w = g->map.w;
    g_map_h = g->map.h;
}

static void draw_map(const gg_game *g, SDL_Renderer *ren, int ox, int oy) {
    rebuild_map_texture(g, ren);
    if (!g_map) return;

    void *pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(g_map, nullptr, &pixels, &pitch)) {
        for (int y = 0; y < g->map.h; y++) {
            uint32_t *row = (uint32_t *)((uint8_t *)pixels + (size_t)y * (size_t)pitch);
            for (int x = 0; x < g->map.w; x++)
                row[x] = terrain_colour(&g->map.cell[(size_t)y * (size_t)g->map.w + (size_t)x]);
        }
        SDL_UnlockTexture(g_map);
    }

    // Fit the map into the pane, preserving aspect - a squashed overview is
    // actively misleading about distance.
    const float sx = (float)MAP_PANE_W / (float)g->map.w;
    const float sy = (float)MAP_PANE_H / (float)g->map.h;
    const float s = sx < sy ? sx : sy;
    const SDL_FRect dst = { (float)ox, (float)oy,
                            (float)g->map.w * s, (float)g->map.h * s };
    SDL_RenderTexture(ren, g_map, nullptr, &dst);

    SDL_SetRenderDrawColor(ren, 95, 72, 46, 255);
    SDL_RenderRect(ren, &dst);

    // Actors over the top, the player last so it is never hidden.
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < g->actors; i++) {
        const gg_actor *a = &g->actor[i];
        if (!a->active || i == g->player) continue;
        SDL_SetRenderDrawColor(ren, 255, 220, 90, 255);
        const SDL_FRect r = { dst.x + a->x * s - 1, dst.y + a->y * s - 1, 3, 3 };
        SDL_RenderFillRect(ren, &r);
    }
    const gg_actor *p = gg_player_const(g);
    SDL_SetRenderDrawColor(ren, 255, 60, 60, 255);
    const SDL_FRect pr = { dst.x + p->x * s - 2, dst.y + p->y * s - 2, 5, 5 };
    SDL_RenderFillRect(ren, &pr);

    // The camera's footprint, so it is obvious how much of the world the
    // player can actually see at once.
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 140);
    const SDL_FRect view = {
        dst.x + (p->x - GG_VIEW_TILES_X / 2) * s,
        dst.y + (p->y - GG_VIEW_TILES_Y / 2) * s,
        GG_VIEW_TILES_X * s, GG_VIEW_TILES_Y * s,
    };
    SDL_RenderRect(ren, &view);
}

void gg_debug_draw(const gg_game *g, SDL_Renderer *ren, uint32_t seed) {
    SDL_SetRenderDrawColor(ren, 18, 20, 16, 255);
    SDL_RenderClear(ren);

    const int line = gg_font_height();
    int y = 10;

    gg_font_draw(ren, 12, y, AMBER, "GIGANTIMA - debug");
    y += line + 6;

    draw_map(g, ren, 12, y);

    // --- right column ------------------------------------------------------
    const int rx = MAP_PANE_W + 40;
    int ry = y;

    gg_font_printf(ren, rx, ry, CYAN, "world"); ry += line;
    gg_font_printf(ren, rx, ry, INK, "%s", g->map.name); ry += line;
    gg_font_printf(ren, rx, ry, DIM, "seed %u   %dx%d", seed, g->map.w, g->map.h);
    ry += line;
    gg_font_printf(ren, rx, ry, DIM, "day %u  %02d:%02d  light %u",
                   g->day, gg_game_hour(g), gg_game_minute(g),
                   gg_game_daylight(g));
    ry += line;
    gg_font_printf(ren, rx, ry, DIM, "turn %u   minute %u", g->turn, g->minutes);
    ry += line + 8;

    const gg_actor *p = gg_player_const(g);
    const gg_cell *under = gg_map_at_const(&g->map, p->x, p->y);
    gg_font_printf(ren, rx, ry, CYAN, "avatar"); ry += line;
    gg_font_printf(ren, rx, ry, INK, "at %d,%d facing %d", p->x, p->y, p->facing);
    ry += line;
    gg_font_printf(ren, rx, ry, DIM, "on %s%s",
                   under ? GG_TERRAIN[under->terrain].name : "?",
                   under && (under->flags & GG_CELL_INDOORS) ? " (indoors)" : "");
    ry += line;
    gg_font_printf(ren, rx, ry, DIM, "region: %s", gg_game_place(g));
    ry += line + 8;

    // --- actor roster ------------------------------------------------------
    gg_font_printf(ren, rx, ry, CYAN, "townsfolk (%d)", g->actors - 1);
    ry += line;
    const int hour = gg_game_hour(g);
    for (int i = 0; i < g->actors && ry < GG_DBG_H - line; i++) {
        if (i == g->player || !g->actor[i].active) continue;
        const gg_actor *a = &g->actor[i];

        int tx = a->x, ty = a->y;
        const bool has = gg_actor_target_at(a, hour, &tx, &ty);
        const int d = gg_dist_cheb(a->x, a->y, tx, ty);

        // Colour by whether they have reached where their day says they should
        // be. A roster that is permanently amber is the tell that a schedule
        // point landed somewhere unreachable.
        gg_font_printf(ren, rx, ry, d == 0 ? DIM : AMBER,
                       "%-9s %3d,%-3d %s%d", a->name, a->x, a->y,
                       has ? "->" : "  ", has ? d : 0);
        ry += line;
    }

    // --- log ---------------------------------------------------------------
    int ly = GG_DBG_H - line * (GG_LOG_LINES + 1) - 8;
    gg_font_draw(ren, 12, ly, CYAN, "log");
    ly += line;
    for (int i = 0; i < g->logn; i++) {
        gg_font_draw(ren, 12, ly, DIM, g->log[i]);
        ly += line;
    }
}
