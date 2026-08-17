// gg_screens.h - everything that is not the world: title, profiles, options,
// naming a new game, and the pause menu.
//
// The screens own their menus and their state; the frontend owns which one is
// showing and what choosing a row does. That split keeps the transitions - the
// part that gets tangled - in one readable place in main.c, and keeps the
// drawing out of it.
#ifndef GG_SCREENS_H
#define GG_SCREENS_H

#include "core/gg_common.h"
#include "core/gg_game.h"
#include "core/gg_save.h"
#include "platform/gg_settings.h"
#include "ui/gg_menu.h"

typedef enum {
    GG_SCREEN_TITLE,
    GG_SCREEN_PROFILES,
    GG_SCREEN_NAME,       // typing a name for a new game
    GG_SCREEN_OPTIONS,
    GG_SCREEN_KEYS,       // what every key does, and changing one
    GG_SCREEN_PLAY,
    GG_SCREEN_PAUSE,
    GG_SCREEN_COUNT
} gg_screen_id;

// What a screen wants the frontend to do once a row has been chosen. The
// screen never acts on the world itself.
typedef enum {
    GG_ACTION_NONE,
    GG_ACTION_CONTINUE,     // resume the highlighted profile
    GG_ACTION_NEW_GAME,     // start one, with `name` as the profile
    GG_ACTION_DELETE,       // delete the highlighted profile
    GG_ACTION_GO,           // move to `next`
    GG_ACTION_SAVE,
    GG_ACTION_QUIT_TO_TITLE,
    GG_ACTION_QUIT,
} gg_screen_action;

typedef struct {
    gg_screen_id id;
    gg_menu      menu;

    // The profile picker's rows, and which one the cursor is on.
    gg_profile profiles[GG_PROFILES_MAX];
    int        profile_count;

    // The name being typed on GG_SCREEN_NAME.
    char typed[GG_PROFILE_NAME_MAX];

    // Where the cursor sits on that screen's on-screen alphabet, which exists
    // so a journey can be named with a pad and nothing else. -1 means the
    // alphabet has not been touched: until it is, the screen behaves exactly as
    // it did when it was keyboard-only, and Enter begins the journey.
    int key_row, key_col;

    // A line shown under the menu: "that name cannot be used", and so on.
    char notice[96];

    // Set while a delete is waiting to be confirmed, so one keypress cannot
    // destroy somebody's game.
    bool confirming_delete;

    // The keys page. `key_page` is which nine actions it is showing, and
    // `binding` is the action waiting for a key to be pressed for it, or -1.
    // While one is waiting the frontend hands every keypress to
    // gg_screens_bind instead of steering the menu - which is the whole of the
    // "press the key you want" interaction.
    int key_page;
    int binding;

    // Where backing out of the options page goes. Options is the one screen
    // reachable from two places, and dropping to the title from a paused game
    // would abandon it.
    gg_screen_id options_from;
} gg_screens;

typedef struct {
    gg_screen_action action;
    gg_screen_id     next;      // for GG_ACTION_GO
    char             name[GG_PROFILE_NAME_MAX];
} gg_screen_result;

// Builds the menu for `id` from the current settings and profile list, and
// makes it the showing screen. Call whenever the underlying data changes -
// after a save, a delete, or an option being altered.
void gg_screens_enter(gg_screens *s, gg_screen_id id, const char *base,
                      const gg_settings *set, const gg_game *g, bool have_game);

// Moves the cursor. `dx` matters only on the naming screen, whose alphabet is a
// grid; everywhere else a menu is a column and only `dy` is read.
void gg_screens_move(gg_screens *s, int dx, int dy);

// A key was pressed while the keys page was waiting for one. Binds it to
// whichever action was chosen and stops waiting. `SDL_SCANCODE_ESCAPE` cancels
// instead, so a player who changes their mind is not stuck. Returns true if
// the settings changed, which is when they need writing out again.
bool gg_screens_bind(gg_screens *s, gg_settings *set, int scancode);

// Left or right on a row that holds a value: cycles it. Rows that are not
// values ignore this, so nudging a stick sideways can never leave a screen or
// destroy anything - which is why this is not simply gg_screens_choose.
void gg_screens_adjust(gg_screens *s, int dir, gg_settings *set);

// Puts the naming cursor on "Begin", so that Start on a pad confirms a name
// from wherever in the alphabet the cursor happens to be. Does nothing on any
// other screen.
void gg_screens_ready(gg_screens *s);

// Chooses the row under the cursor. `set` may be altered in place: the options
// page cycles a value rather than opening a sub-menu, which for five options
// is fewer keystrokes and one less screen to get lost in.
gg_screen_result gg_screens_choose(gg_screens *s, const char *base,
                                   gg_settings *set, bool have_game);

// Backing out of the screen - Escape, or B on a pad.
gg_screen_result gg_screens_back(gg_screens *s);

// A typed character, for the name screen. `ch` is a printable ASCII byte, or
// '\b' for backspace.
void gg_screens_type(gg_screens *s, char ch);

void gg_screens_draw(const gg_screens *s, SDL_Renderer *ren, uint64_t frame);

#endif // GG_SCREENS_H
