// gg_debug.h - the debug window: a whole-map overview and the actor roster.
//
// A separate window rather than an overlay, because the two things you most
// want while debugging this game are the map at a scale that shows the town
// *and* the world view at its normal scale, at the same time. An overlay can
// only ever give you one of them.
//
// Opened with --debug, toggled with F1.
#ifndef GG_DEBUG_H
#define GG_DEBUG_H

#include "core/gg_common.h"
#include "core/gg_game.h"

// Named GG_DBG_* rather than GG_DEBUG_*: the include guard above is already
// GG_DEBUG_H, and a constant that collides with its own header's guard is a
// redefinition the preprocessor reports from a confusing place.
#define GG_DBG_W 900
#define GG_DBG_H 640

bool gg_debug_init(SDL_Renderer *ren);
void gg_debug_quit(SDL_Renderer *ren);

// Draws the whole debug view. Does not present.
void gg_debug_draw(const gg_game *g, SDL_Renderer *ren, uint32_t seed);

// Draw the overview in colours chosen to stay apart for the commonest kinds of
// colour blindness, and tell the markers apart by shape as well as by shade.
// Off by default: the ordinary palette reads better for everyone it works for.
void gg_debug_plain_colours(bool plain);

#endif // GG_DEBUG_H
