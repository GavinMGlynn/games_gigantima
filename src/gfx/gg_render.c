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

static SDL_Texture *g_tiles, *g_props, *g_actors, *g_edges, *g_overlays;
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
    g_edges  = load(ren, "atlas_edges.png");
    g_overlays = load(ren, "atlas_overlays.png");
    g_props  = load(ren, "atlas_props.png");
    g_actors = load(ren, "atlas_actors.png");
    return g_tiles && g_edges && g_overlays && g_props && g_actors;
}

void gg_render_quit(void) {
    SDL_DestroyTexture(g_tiles);
    SDL_DestroyTexture(g_edges);
    SDL_DestroyTexture(g_overlays);
    SDL_DestroyTexture(g_props);
    SDL_DestroyTexture(g_actors);
    g_tiles = g_edges = g_overlays = g_props = g_actors = nullptr;
    g_ren = nullptr;
}

// ---------------------------------------------------------------------------
// Shoreline autotiling
//
// Water is stored as one terrain id; which of the nine pieces gets drawn is
// decided here, from the four orthogonal neighbours. Keeping this in the
// renderer rather than baking edge ids into the map is what lets the editor
// paint water as water and lets a single edited cell re-shape the coast around
// it with no bookkeeping.
// ---------------------------------------------------------------------------
// "Is the cell at (x, y) the same body as the one being drawn?" Off-map counts
// as the same, so a lake running to the map edge does not draw itself a
// shoreline against nothing.
typedef bool (*gg_same_fn)(const gg_cell *c);

static bool same_water(const gg_cell *c) {
    return c->terrain == GG_TILE_WATER || c->terrain == GG_TILE_WATER_DEEP;
}

static bool same_deep(const gg_cell *c) {
    return c->terrain == GG_TILE_WATER_DEEP;
}

static bool matches(const gg_map *m, int x, int y, gg_same_fn same) {
    const gg_cell *c = gg_map_at_const(m, x, y);
    return c ? same(c) : true;
}

// Which of the nine pieces, given where the boundary is. Returns GG_EDGE_C
// when the cell is surrounded, which is also the fallback for the concave case
// the LPC sheets carry no art for - see the note in tools/make_atlas.py.
static int edge_piece(const gg_map *m, int x, int y, gg_same_fn same) {
    const bool n = !matches(m, x, y - 1, same), s = !matches(m, x, y + 1, same);
    const bool w = !matches(m, x - 1, y, same), e = !matches(m, x + 1, y, same);

    if (n && w) return GG_EDGE_NW;
    if (n && e) return GG_EDGE_NE;
    if (s && w) return GG_EDGE_SW;
    if (s && e) return GG_EDGE_SE;
    if (n) return GG_EDGE_N;
    if (s) return GG_EDGE_S;
    if (w) return GG_EDGE_W;
    if (e) return GG_EDGE_E;

    // No orthogonal boundary, so this cell is interior on all four sides - but
    // a diagonal neighbour may still be outside, which is the concave case.
    // Checked only after the orthogonals, because an orthogonal boundary
    // always dominates: a cell with land to the north gets the north edge
    // whatever its corners are doing.
    if (!matches(m, x - 1, y - 1, same)) return GG_EDGE_IN_NW;
    if (!matches(m, x + 1, y - 1, same)) return GG_EDGE_IN_NE;
    if (!matches(m, x - 1, y + 1, same)) return GG_EDGE_IN_SW;
    if (!matches(m, x + 1, y + 1, same)) return GG_EDGE_IN_SE;
    return GG_EDGE_C;
}

int gg_render_water_piece(const gg_map *m, int x, int y) {
    return edge_piece(m, x, y, same_water);
}

// ---------------------------------------------------------------------------
// Land meeting land
//
// Water *replaces* a cell's tile; land is drawn *over* one. That difference is
// what lets several boundaries meet on the same cell without fighting: a patch
// of dirt with grass to the north and sand to the east gets both, as two
// transparent passes.
//
// Which way round a boundary is drawn comes from a rank. The source art has an
// overlay ring for grass and for nothing else; the rest are synthesised at
// bake time by filling the grass ring's shape with each terrain's own fill, so
// every pair has one. See tools/make_atlas.py.
// ---------------------------------------------------------------------------
// How strongly a terrain encroaches on its neighbours. Softer, more vegetated
// ground bleeds onto harder, barer ground and never the other way, so a
// boundary is drawn once and from one side only. Equal ranks do not transition
// at all, which is what keeps grass and worn grass, or dirt and a building's
// earth floor, from fringing each other pointlessly.
//
// Rank -1 means "no overlay at all": water carries its bank in its own edge
// sets, and GG_TILE_CLIFF stands in for masonry as well as rock, where a grass
// fringe up the side of every building would be worse than a hard edge.
static int terrain_rank(uint8_t t) {
    switch (t) {
    case GG_TILE_GRASS:  case GG_TILE_GRASS_WORN:               return 4;
    case GG_TILE_FARMLAND: case GG_TILE_DIRT: case GG_TILE_EARTH_DARK: return 3;
    case GG_TILE_ROAD:   case GG_TILE_SAND:                     return 2;
    case GG_TILE_DESERT:                                        return 1;
    // Rock takes a verge but never gives one. Not obvious - grass does not
    // grow on a cliff face - but the alternative is a hard staircase where the
    // mountains meet the grass, and that reads as a tiling artifact rather
    // than as a cliff.
    case GG_TILE_MOUNTAIN:                                      return 0;
    default:                                                    return -1;
    }
}

// Which ring a terrain bleeds *with*. Several terrains share one: a building's
// earth floor uses dirt's, because they are the same material.
static int overlay_of(uint8_t t) {
    switch (t) {
    case GG_TILE_GRASS: case GG_TILE_GRASS_WORN: return GG_OVERLAY_GRASS;
    case GG_TILE_FARMLAND:                       return GG_OVERLAY_FARMLAND;
    case GG_TILE_DIRT:  case GG_TILE_EARTH_DARK: return GG_OVERLAY_DIRT;
    case GG_TILE_ROAD:                           return GG_OVERLAY_ROAD;
    case GG_TILE_SAND:                           return GG_OVERLAY_SAND;
    case GG_TILE_DESERT:                         return GG_OVERLAY_DESERT;
    default:                                     return -1;
    }
}

// Does the cell at (x, y) bleed onto a cell of rank `rank` using ring `set`?
// Off-map never does: the map edge should not sprout a verge.
static bool encroaches(const gg_map *m, int x, int y, int set, int rank) {
    const gg_cell *c = gg_map_at_const(m, x, y);
    if (!c) return false;
    return terrain_rank(c->terrain) > rank && overlay_of(c->terrain) == set;
}

// Which overlay pieces to draw, as a bitmask over the 13 piece indices.
//
// A mask rather than a single piece, because an overlay is transparent and so
// can be drawn more than once. Picking one piece the way the edge sets do
// cannot express "grass on both sides", which is exactly what a one-tile-wide
// road and a one-tile island both are - and both are common. Choosing a single
// piece left the road grassy down its west side and hard-edged down its east.
//
// So: one straight piece per orthogonal side that has grass, which composites
// into the right shape for corners without needing the convex pieces at all -
// north and west drawn together *is* the north-west corner. The concave pieces
// then cover only the case a straight piece cannot reach, a grass neighbour on
// a diagonal with neither of its own sides grassy.
uint16_t gg_render_overlay_mask(const gg_map *m, int x, int y, int set) {
    const gg_cell *c = gg_map_at_const(m, x, y);
    if (!c) return 0;
    const int rank = terrain_rank(c->terrain);
    if (rank < 0) return 0;              // water and masonry take no overlay

    const bool n = encroaches(m, x, y - 1, set, rank);
    const bool s = encroaches(m, x, y + 1, set, rank);
    const bool w = encroaches(m, x - 1, y, set, rank);
    const bool e = encroaches(m, x + 1, y, set, rank);

    uint16_t mask = 0;
    if (n) mask |= 1u << GG_EDGE_N;
    if (s) mask |= 1u << GG_EDGE_S;
    if (w) mask |= 1u << GG_EDGE_W;
    if (e) mask |= 1u << GG_EDGE_E;

    if (encroaches(m, x - 1, y - 1, set, rank) && !n && !w) mask |= 1u << GG_EDGE_IN_NW;
    if (encroaches(m, x + 1, y - 1, set, rank) && !n && !e) mask |= 1u << GG_EDGE_IN_NE;
    if (encroaches(m, x - 1, y + 1, set, rank) && !s && !w) mask |= 1u << GG_EDGE_IN_SW;
    if (encroaches(m, x + 1, y + 1, set, rank) && !s && !e) mask |= 1u << GG_EDGE_IN_SE;
    return mask;
}

// Sand and desert get the beach set, everything else the grass set. Decided
// per cell rather than per lake, so one shore can be beach and the far side
// grass - both sets share an identical water centre, so they meet invisibly.
static gg_edge_id edge_set(const gg_map *m, int x, int y) {
    static const int DX[4] = { 0, 0, -1, 1 };
    static const int DY[4] = { -1, 1, 0, 0 };
    for (int i = 0; i < 4; i++) {
        const gg_cell *c = gg_map_at_const(m, x + DX[i], y + DY[i]);
        if (!c) continue;
        if (c->terrain == GG_TILE_SAND || c->terrain == GG_TILE_DESERT)
            return GG_EDGE_WATER_SAND;
    }
    return GG_EDGE_WATER_GRASS;
}

// ---------------------------------------------------------------------------
// Light
//
// Three sources, and a cell takes the brightest of them:
//
//   the sky      outdoors only, from the world clock
//   a room       an interior is lit by whoever lives there, day or night
//   the avatar   a small carried light, so the player is never in the dark
//
// The interior light is what `GG_CELL_INDOORS` was set for. Without it a room
// was exactly as dark as the street at night, which made going inside after
// dusk pointless.
// ---------------------------------------------------------------------------
uint8_t gg_light_at(const gg_game *g, int x, int y, uint8_t day) {
    const gg_cell *c = gg_map_at_const(&g->map, x, y);

    // A room is lit by its own hearth, and a doorway takes some of it, so an
    // open door reads as light spilling into the street rather than as a hole.
    uint8_t lit = 0;
    if (c && (c->flags & GG_CELL_INDOORS)) lit = GG_LIGHT_INDOOR;
    else if (c && (c->flags & GG_CELL_DOOR)) lit = GG_LIGHT_INDOOR * 3 / 4;
    else lit = day;

    // The avatar's own light. Radius rather than a flat disc, so it falls off
    // and the edge of what you can see moves with you.
    const gg_actor *p = gg_player_const(g);
    const int d = gg_dist_cheb(p->x, p->y, x, y);
    if (d <= GG_LIGHT_CARRY_RADIUS) {
        const int fall = GG_LIGHT_FULL -
                         d * GG_LIGHT_FULL / (GG_LIGHT_CARRY_RADIUS + 1);
        const uint8_t carried = (uint8_t)gg_clampi(fall, 0, GG_LIGHT_FULL);
        if (carried > lit) lit = carried;
    }
    return lit;
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
// plus every actor. Static rather than allocated: it is rebuilt every frame
// and the bound is small and known.
//
// The band runs well outside the view, because a prop is anchored at its
// footprint and drawn from there in every direction. The largest is an 8x7
// house anchored 3 tiles from its left edge and 5 rows up from its bottom, so
// its roof reaches five rows above the anchor and its walls four columns to
// the right. Under-sizing this does not crash - it makes buildings pop in at
// the edge of the screen, which is exactly the artifact a sub-tile camera was
// added to remove.
#define GATHER_PAD_L 5
#define GATHER_PAD_R 6
#define GATHER_PAD_T 2
#define GATHER_PAD_B 8
#define GATHER_X (GG_VIEW_TILES_X + GATHER_PAD_L + GATHER_PAD_R)
#define GATHER_Y (GG_VIEW_TILES_Y + GATHER_PAD_T + GATHER_PAD_B)
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

            // Shallow water picks a shoreline piece; everything else, deep
            // water included, is a flat fill. Deep water is excluded because
            // it only ever borders shallow water, never land.
            SDL_Texture *tex = g_tiles;
            const gg_rect *r;
            if (c && c->terrain == GG_TILE_WATER) {
                tex = g_edges;
                r = &GG_EDGE_RECT[edge_set(&g->map, wx, wy)]
                                 [edge_piece(&g->map, wx, wy, same_water)];
            } else if (c && c->terrain == GG_TILE_WATER_DEEP) {
                // Deep water only ever borders shallow, never land, so it gets
                // its own set and its own predicate.
                tex = g_edges;
                r = &GG_EDGE_RECT[GG_EDGE_WATER_DEEP]
                                 [edge_piece(&g->map, wx, wy, same_deep)];
            } else {
                r = &GG_TILE_RECT[c ? c->terrain : GG_TILE_WATER_DEEP];
            }

            const SDL_FRect src = { (float)r->x, (float)r->y,
                                    (float)r->w, (float)r->h };
            const SDL_FRect dst = { (float)(vx * GG_TILE - off_x),
                                    (float)(vy * GG_TILE - off_y),
                                    (float)GG_TILE, (float)GG_TILE };
            SDL_RenderTexture(ren, tex, &src, &dst);

            // Land meeting land: a second, transparent pass over the base
            // tile. Nothing to draw when the cell touches no grass, which is
            // the common case, so the lookup exits early.
            // Land meeting land: transparent passes over the base tile, one
            // per ring that encroaches here. Asked per set because a cell can
            // have grass on one side and sand on another, and each brings its
            // own boundary. Ordered low rank to high so the softer ground ends
            // up on top where two meet at a corner.
            for (int set = GG_OVERLAY_COUNT - 1; set >= 0; set--) {
                const uint16_t mask = gg_render_overlay_mask(&g->map, wx, wy, set);
                for (int p = 0; mask >> p; p++) {
                    if (!(mask & (1u << p))) continue;
                    const gg_rect *o = &GG_OVERLAY_RECT[set][p];
                    const SDL_FRect osrc = { (float)o->x, (float)o->y,
                                             (float)o->w, (float)o->h };
                    SDL_RenderTexture(ren, g_overlays, &osrc, &dst);
                }
            }
        }
    }

    // --- 2. props and actors, one sorted list ------------------------------
    static gg_sprite list[MAX_SPRITES];
    int n = 0;

    for (int wy = cam_ty - GATHER_PAD_T;
         wy < cam_ty + GG_VIEW_TILES_Y + GATHER_PAD_B && n < MAX_SPRITES; wy++) {
        for (int wx = cam_tx - GATHER_PAD_L;
             wx < cam_tx + GG_VIEW_TILES_X + GATHER_PAD_R && n < MAX_SPRITES; wx++) {
            const gg_cell *c = gg_map_at_const(&g->map, wx, wy);
            if (!c || !GG_HAS_PROP(c)) continue;

            const gg_prop_id id = GG_PROP_OF(c);
            const gg_rect *r = &GG_PROP_RECT[id];
            const gg_prop_size *s = &GG_PROP_SIZE[id];

            // The cutaway. A building is drawn as a whole house seen from
            // outside; step through its door and the roof comes off, showing
            // the room that was walkable all along. Skipping the sprite is the
            // entire mechanism - the floor and walls beneath it are ordinary
            // terrain, already drawn by the pass above.
            if (s->hollow &&
                gg_prop_interior_contains(id, wx, wy,
                                          gg_player_const(g)->x,
                                          gg_player_const(g)->y))
                continue;

            // The sprite is placed so its anchor cell - the footprint's bottom
            // centre - lands on the map cell the prop occupies. For a tree
            // that is the trunk; for a house it is the middle of the front
            // wall, several rows up from the bottom of the sprite, which is
            // what lets the roof overhang tiles the player can walk behind.
            //
            // All in world pixels, shifted by the camera, so props slide with
            // the terrain rather than snapping between tiles.
            const int base_y = (wy + 1) * GG_TILE;
            const int px = (wx - s->anchor_x) * GG_TILE - cam_px;
            const int py = (wy - s->anchor_y) * GG_TILE - cam_py;

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
    // Per tile, not one quad over everything. Tile granularity is not a
    // compromise here - Ultima VI lit by the tile too, and a circle of
    // torchlight stepping outward a tile at a time is the look.
    //
    // Night is blue rather than black: a pure black wash reads as the renderer
    // having broken, and moonlight is blue anyway.
    const uint8_t day = gg_game_daylight(g);
    if (day < GG_LIGHT_FULL) {
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        for (int vy = 0; vy <= GG_VIEW_TILES_Y; vy++) {
            for (int vx = 0; vx <= GG_VIEW_TILES_X; vx++) {
                const int wx = cam_tx + vx, wy = cam_ty + vy;
                const uint8_t lit = gg_light_at(g, wx, wy, day);
                if (lit >= GG_LIGHT_FULL) continue;

                // Never fully black: at three fifths, a moonlit field is still
                // readable, which matters when the player has to walk home.
                const uint8_t dark = (uint8_t)((GG_LIGHT_FULL - lit) * 3 / 5);
                SDL_SetRenderDrawColor(ren, 12, 18, 56, dark);
                const SDL_FRect dst = { (float)(vx * GG_TILE - off_x),
                                        (float)(vy * GG_TILE - off_y),
                                        (float)GG_TILE, (float)GG_TILE };
                SDL_RenderFillRect(ren, &dst);
            }
        }
    }
}
