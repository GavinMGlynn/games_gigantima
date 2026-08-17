// gg_ui.h - the status band and the conversation panel.
//
// Everything here draws in screen space and reads the game without changing
// it, so the frontend decides what to compose and in what order.
#ifndef GG_UI_H
#define GG_UI_H

#include "core/gg_common.h"
#include "core/gg_game.h"

// The band beneath the world view: who you are, what you carry, where and
// when you are, and the message log.
void gg_ui_hud(const gg_game *g, SDL_Renderer *ren);

// The conversation panel, drawn over the world while mode is GG_MODE_CONVERSE.
void gg_ui_converse(const gg_game *g, SDL_Renderer *ren);

#endif // GG_UI_H
