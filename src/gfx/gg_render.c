// gg_render.c - the world view.
//
// Three passes, in this order, and the order is the whole design:
//
//   1. terrain   one textured quad per visible tile, no sorting
//   2. sprites   props and actors together, sorted by the row they stand on
//   3. light     a single translucent quad tinted by the world clock
//
// Pass 2 is what makes the world look three-quarter rather than flat. A tree
// is three tiles wide and four tall but stands on one tile; sorting everything
// that stands on the ground by its base row - trees and people in the same
// list - is what lets the player walk behind a canopy and in front of a trunk
// without any per-object special cases.
#include "gfx/gg_render.h"
#include "gfx/gg_atlas.h"
#include "platform/gg_paths.h"

#include <SDL3_image/SDL_image.h>

static SDL_Texture *g_tiles, *g_props, *g_actors;
static SDL_Renderer *g_ren;

static SDL_Texture *load(SDL_Renderer *ren, const char *name) {
    SDL_Texture *t = IMG_LoadTexture(ren, gg_asset_path(name));
    if (!t) {
        SDL_Log("gigantima: cannot load %s: %s", name, SDL_GetError());
        return nullptr;
    }
    // The art is pixel art at a fixed scale, so nearest is not a preference,
    // it is the only correct filter.
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    return t;
}

bool gg_render_init(SDL_Renderer *ren) {
    if (!gg_assets_found()) return false;

    g_ren = ren;
    g_tiles  = load(ren, "atlas_tiles.png");
    g_props  = load(ren, "atlas_props.png");
    g_actors = load(ren, "atlas_actors.png");
    return g_tiles && g_props && g_actors;
}

void gg_render_quit(void) {
    SDL_DestroyTexture(g_tiles);
    SDL_DestroyTexture(g_props);
    SDL_DestroyTexture(g_actors);
    g_tiles = g_props = g_actors = nullptr;
    g_ren = nullptr;
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------
void gg_render_camera(const gg_game *g, int *cam_px, int *cam_py) {
    const gg_actor *p = gg_player_const(g);

    // Follow the avatar's *interpolated* position, in pixels. Following its
    // tile instead is what made the world lurch: the avatar slid smoothly
    // while everything behind it teleported a tile at a time, and the eye
    // reads that as the avatar stuttering.
    int ax, ay;
    gg_actor_draw_pos(p, &ax, &ay);

    const int px = ax + GG_TILE / 2 - GG_VIEW_W / 2;
    const int py = ay + GG_TILE / 2 - GG_VIEW_H / 2;

    // Clamp so the view never runs off the map. Ultima VI kept the avatar dead
    // centre and showed void past the edge; clamping trades that for never
    // drawing a black band, which matters more now the map is a finite
    // rectangle rather than a wrapping world.
    const int max_x = g->map.w * GG_TILE - GG_VIEW_W;
    const int max_y = g->map.h * GG_TILE - GG_VIEW_H;
    *cam_px = gg_clampi(px, 0, max_x > 0 ? max_x : 0);
    *cam_py = gg_clampi(py, 0, max_y > 0 ? max_y : 0);
}

void gg_render_screen_to_tile(const gg_game *g, int sx, int sy, int *tx, int *ty) {
    int cam_px, cam_py;
    gg_render_camera(g, &cam_px, &cam_py);
    *tx = (cam_px + sx) / GG_TILE;
    *ty = (cam_py + sy) / GG_TILE;
}

// ---------------------------------------------------------------------------
// Depth-sorted sprite list
// ---------------------------------------------------------------------------
// Sized for the worst case: every cell of the gathered band holding a prop,
// plus every actor. The band is wider and taller than the view because a
// sub-tile camera shows part of a row past each edge, and a tall prop whose
// base is below the bottom edge still has a canopy inside it. Static rather
// than allocated: it is rebuilt every frame and the bound is small and known.
#define GATHER_X (GG_VIEW_TILES_X + 5)
#define GATHER_Y (GG_VIEW_TILES_Y + 8)
#define MAX_SPRITES (GATHER_X * GATHER_Y + GG_ACTORS_MAX)

typedef struct {
    int32_t  key;        // sort key: the pixel row the sprite stands on
    SDL_Texture *tex;
    SDL_FRect src, dst;
} gg_sprite;

static int sprite_cmp(const void *a, const void *b) {
    const gg_sprite *p = a, *q = b;
    // Ties broken by x so the order is total and therefore stable across
    // frames - two things on the same row must not swap places as the camera
    // moves, which is what an unstable comparison would let them do.
    if (p->key != q->key) return p->key < q->key ? -1 : 1;
    if (p->dst.x != q->dst.x) return p->dst.x < q->dst.x ? -1 : 1;
    return 0;
}

// ---------------------------------------------------------------------------
// The frame
// ---------------------------------------------------------------------------
void gg_render_world(const gg_game *g, SDL_Renderer *ren) {
    if (!g_tiles) return;

    int cam_px, cam_py;
    gg_render_camera(g, &cam_px, &cam_py);

    // Split the pixel camera into the tile it starts on and how far into that
    // tile it sits. The remainder is what makes the world slide rather than
    // jump; the extra row and column cover the sliver it exposes at each edge.
    const int cam_tx = cam_px / GG_TILE, off_x = cam_px % GG_TILE;
    const int cam_ty = cam_py / GG_TILE, off_y = cam_py % GG_TILE;

    // --- 1. terrain --------------------------------------------------------
    for (int vy = 0; vy <= GG_VIEW_TILES_Y; vy++) {
        for (int vx = 0; vx <= GG_VIEW_TILES_X; vx++) {
            const int wx = cam_tx + vx, wy = cam_ty + vy;
            const gg_cell *c = gg_map_at_const(&g->map, wx, wy);
            const gg_rect *r = &GG_TILE_RECT[c ? c->terrain : GG_TILE_WATER_DEEP];

            const SDL_FRect src = { (float)r->x, (float)r->y,
                                    (float)r->w, (float)r->h };
            const SDL_FRect dst = { (float)(vx * GG_TILE - off_x),
                                    (float)(vy * GG_TILE - off_y),
                                    (float)GG_TILE, (float)GG_TILE };
            SDL_RenderTexture(ren, g_tiles, &src, &dst);
        }
    }

    // --- 2. props and actors, one sorted list ------------------------------
    static gg_sprite list[MAX_SPRITES];
    int n = 0;

    for (int wy = cam_ty - 1; wy < cam_ty + GATHER_Y - 1 && n < MAX_SPRITES; wy++) {
        for (int wx = cam_tx - 2; wx < cam_tx + GATHER_X - 2 && n < MAX_SPRITES; wx++) {
            const gg_cell *c = gg_map_at_const(&g->map, wx, wy);
            if (!c || !GG_HAS_PROP(c)) continue;

            const gg_prop_id id = GG_PROP_OF(c);
            const gg_rect *r = &GG_PROP_RECT[id];
            const gg_prop_size *s = &GG_PROP_SIZE[id];

            // Anchor: horizontally centred on the base tile, bottom edge flush
            // with the bottom of the base tile row. Everything is computed in
            // world pixels and shifted by the camera, so props slide with the
            // terrain rather than snapping between tiles.
            const int base_y = (wy + 1) * GG_TILE;
            const int px = wx * GG_TILE - ((s->tiles_w - 1) / 2) * GG_TILE - cam_px;
            const int py = base_y - r->h - cam_py;

            list[n++] = (gg_sprite){
                .key = base_y,
                .tex = g_props,
                .src = { (float)r->x, (float)r->y, (float)r->w, (float)r->h },
                .dst = { (float)px, (float)py, (float)r->w, (float)r->h },
            };
        }
    }

    for (int i = 0; i < g->actors && n < MAX_SPRITES; i++) {
        const gg_actor *a = &g->actor[i];
        if (!a->active) continue;

        int ax, ay;
        gg_actor_draw_pos(a, &ax, &ay);
        const int px = ax - cam_px, py = ay - cam_py;
        // Cull generously - a 64px sprite standing just off-view still shows.
        if (px < -GG_ACTOR_FRAME || py < -GG_ACTOR_FRAME ||
            px > GG_VIEW_W + GG_ACTOR_FRAME || py > GG_VIEW_H + GG_ACTOR_FRAME)
            continue;

        const gg_rect *r = &GG_ACTOR_RECT[a->art < GG_ACTOR_COUNT ? a->art : 0];
        const int frame = a->anim % GG_ACTOR_FRAMES;

        list[n++] = (gg_sprite){
            // Feet, not head: the sort key must be where the actor touches the
            // ground, or a tall sprite sorts as if it stood a tile further
            // back. In world pixels, to match the props' key.
            .key = ay + GG_TILE,
            .tex = g_actors,
            .src = { (float)(r->x + frame * GG_ACTOR_FRAME),
                     (float)(r->y + a->facing * GG_ACTOR_FRAME),
                     (float)GG_ACTOR_FRAME, (float)GG_ACTOR_FRAME },
            // The 64px frame is centred on a 32px tile and stands on its
            // bottom edge, so it overhangs 16px each side and 32px upward.
            .dst = { (float)(px - GG_TILE / 2),
                     (float)(py + GG_TILE - GG_ACTOR_FRAME),
                     (float)GG_ACTOR_FRAME, (float)GG_ACTOR_FRAME },
        };
    }

    SDL_qsort(list, (size_t)n, sizeof *list, sprite_cmp);
    for (int i = 0; i < n; i++)
        SDL_RenderTexture(ren, list[i].tex, &list[i].src, &list[i].dst);

    // --- 3. light ----------------------------------------------------------
    // One translucent quad over the world. Night is blue rather than black
    // because a pure black wash reads as "the renderer broke", and moonlight
    // is blue anyway. Indoor cells are not yet excluded - that needs a light
    // source model, and it is a named item in docs/COMPLETION_PLAN.md.
    const uint8_t day = gg_game_daylight(g);
    if (day < 255) {
        const uint8_t dark = (uint8_t)((255 - day) * 3 / 5);   // never fully black
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 12, 18, 56, dark);
        const SDL_FRect all = { 0, 0, (float)GG_VIEW_W, (float)GG_VIEW_H };
        SDL_RenderFillRect(ren, &all);
    }
}
