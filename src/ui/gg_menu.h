// gg_menu.h - a vertical list you move a cursor down and choose from.
//
// One widget, used by every screen that is a list of choices - the title, the
// profile picker, the options page, the pause menu. Written once because four
// hand-rolled menus would drift apart in exactly the ways a player notices:
// which key confirms, whether the cursor wraps, whether a disabled row can be
// landed on.
//
// Knows nothing about what its rows mean. The screen that owns it decides what
// choosing row three does.
#ifndef GG_MENU_H
#define GG_MENU_H

#include "core/gg_common.h"

#define GG_MENU_MAX      12
#define GG_MENU_TEXT_MAX 64

typedef struct {
    char label[GG_MENU_TEXT_MAX];
    // A second line under the label, greyed - for a profile's "day 3, 14:20,
    // Britain", or an option's current value.
    char detail[GG_MENU_TEXT_MAX];
    bool enabled;
} gg_menu_item;

typedef struct {
    char         title[GG_MENU_TEXT_MAX];
    gg_menu_item item[GG_MENU_MAX];
    int          n;
    int          cursor;
} gg_menu;

// Empties the menu and sets its heading.
void gg_menu_reset(gg_menu *m, const char *title);

// Appends a row. `detail` may be nullptr. Returns the row's index, or -1 if
// the menu is full.
int gg_menu_add(gg_menu *m, bool enabled, const char *label,
                SDL_PRINTF_FORMAT_STRING const char *detail, ...)
    SDL_PRINTF_VARARG_FUNC(4);

// Moves the cursor by `dy`, skipping disabled rows and wrapping at both ends.
// Wrapping matters more than it sounds: on a five-row menu, pressing up once
// to reach "Quit" is the difference between a menu that feels considered and
// one that feels like a list.
void gg_menu_move(gg_menu *m, int dy);

// Puts the cursor on the first enabled row at or after `index`.
void gg_menu_select(gg_menu *m, int index);

// The chosen row, or -1 if nothing on the menu can be chosen.
int gg_menu_chosen(const gg_menu *m);

// Draws the menu centred on `cx`, starting at `top`. Returns the y below it,
// so a screen can put something after it without counting rows.
int gg_menu_draw(const gg_menu *m, SDL_Renderer *ren, int cx, int top);

#endif // GG_MENU_H
