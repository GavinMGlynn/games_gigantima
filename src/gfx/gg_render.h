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

// --- light -----------------------------------------------------------------
#define GG_LIGHT_FULL          255
// A room is lit by whoever lives there, day or night. Short of full daylight,
// so stepping inside at noon still reads as stepping into shade.
#define GG_LIGHT_INDOOR        215
// How far the avatar's own light reaches, in tiles. Small: the point is that
// the player is never in the dark, not that night stops mattering.
#define GG_LIGHT_CARRY_RADIUS  4

// How lit the cell at (x, y) is, 0 to GG_LIGHT_FULL, given the sky's `day`
// level. The brightest of the sky, the room, and the avatar's carried light.
//
// Exposed because it is pure arithmetic over the game state - no texture
// involved - and because a lighting rule is far easier to check as a number
// than by eye in a night-time screenshot.
uint8_t gg_light_at(const gg_game *g, int x, int y, uint8_t day);

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
