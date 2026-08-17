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

// A weight in hundredths of a stone, written as stone-and-a-tenth: "0.3 st".
// Integer arithmetic throughout - the simulation has no floating point in it
// and the display has no business introducing any.
void gg_ui_weight(char *out, size_t n, int hundredths);

// The spells whose runes are known, drawn over the world while mode is
// GG_MODE_SPELL. Shows the phrase, the price and whether it can be paid, so a
// player can see why a spell is out of reach without leaving the book.
void gg_ui_spells(const gg_game *g, SDL_Renderer *ren);

// What is carried, drawn over the world while mode is GG_MODE_PACK. `items` is
// the item atlas: the pack shows each thing's own picture, because a list of
// words is not what the player has been looking at while playing.
void gg_ui_pack(const gg_game *g, SDL_Renderer *ren, SDL_Texture *items);

#endif // GG_UI_H
