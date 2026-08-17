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

#endif // GG_RENDER_H
