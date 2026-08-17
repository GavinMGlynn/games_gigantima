// gg_render.h - drawing the world: camera, terrain, depth-sorted sprites, light.
#ifndef GG_RENDER_H
#define GG_RENDER_H

#include "core/gg_common.h"
#include "core/gg_game.h"

// Loads the atlas textures. Call once per renderer, after the renderer exists.
bool gg_render_init(SDL_Renderer *ren);
void gg_render_quit(void);

// Draws the world view. Does not present, and does not draw the HUD - the
// frontend composes those, so a screenshot path and the debug window can each
// choose what they want.
void gg_render_world(const gg_game *g, SDL_Renderer *ren);

// Top-left of the view **in world pixels**, not tiles.
//
// Pixels matter: a tile-quantised camera makes the whole world jump 32 px
// whenever the player crosses a tile boundary, which reads as the game
// stuttering even though the avatar itself is interpolating perfectly. The
// camera follows the avatar's interpolated position and the terrain is drawn
// at a sub-tile offset, so the world slides.
void gg_render_camera(const gg_game *g, int *cam_px, int *cam_py);

// Screen pixel -> world tile, using the same camera the last draw used.
void gg_render_screen_to_tile(const gg_game *g, int sx, int sy, int *tx, int *ty);

// Which of an edge set's nine pieces a water cell resolves to, as
// GG_EDGE_NW .. GG_EDGE_SE. Exposed because it is pure logic over the map with
// no texture involved, and it is where a shoreline bug would actually live -
// a wrong index there is a one-tile artifact that is easy to miss by eye and
// trivial to catch in a test.
int gg_render_water_piece(const gg_map *m, int x, int y);

// Which pieces of overlay ring `set` to draw over the cell at (x, y), as a
// bitmask over the 13 piece indices. Zero when no neighbour of that set
// encroaches here.
//
// A mask, not a single piece: overlays are transparent, so more than one can
// be drawn, and only that can express a neighbour on two opposite sides -
// which is what a one-tile-wide road has.
//
// `set` is a gg_overlay_id. The renderer asks once per set, because a cell can
// have different terrains encroaching from different sides.
uint16_t gg_render_overlay_mask(const gg_map *m, int x, int y, int set);

#endif // GG_RENDER_H
