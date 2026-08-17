// gg_settings.h - the options a player sets, and where they are kept.
//
// A plain `key = value` text file in the preferences directory, not a binary
// blob. Settings are the one thing a player might reasonably want to look at
// or fix by hand when something has gone wrong, and a text file costs nothing
// to write.
//
// An unknown key is kept and written back rather than dropped, so a file
// written by a newer build survives being loaded by an older one.
#ifndef GG_SETTINGS_H
#define GG_SETTINGS_H

#include "core/gg_common.h"
#include "core/gg_game.h"

#define GG_SETTINGS_FILE "settings.txt"
#define GG_LAST_PROFILE_MAX 32

typedef struct {
    int  scale;            // 1..4 window scale
    bool fullscreen;
    bool rumble;
    int  music;            // 0..10
    int  effects;          // 0..10

    // --- what a player may need in order to play at all -------------------
    // How large the text is drawn, as a whole-number multiple. Whole numbers
    // only: the font is a baked bitmap and half a texel is a blurred one.
    int  text_scale;       // 1 or 2

    // Colours for the debug overview chosen to stay apart for the commonest
    // kinds of colour blindness. Off by default because the ordinary palette
    // reads better for everyone else.
    bool plain_colours;

    // Every action's key, and a second key for it. Two because the game has
    // always answered to both the arrows and WASD, and a single binding per
    // action would have had to throw one of them away. Zero means unbound.
    //
    // Stored as SDL scancodes - *positions* on the keyboard rather than the
    // letters printed on them - so a binding made on one layout is the same
    // physical key on another.
    uint16_t key[GG_ACT_COUNT];
    uint16_t alt[GG_ACT_COUNT];

    char last_profile[GG_LAST_PROFILE_MAX];
} gg_settings;

// The keys a fresh install answers to, without touching anything else in `s`.
// Public because the options page offers "put the keys back".
void gg_settings_default_keys(gg_settings *s);

// The defaults a first run gets.
void gg_settings_defaults(gg_settings *s);

// Reads from `path`. Missing or unreadable leaves `s` at its defaults and
// returns false - which is not an error, it is a first run.
bool gg_settings_load(gg_settings *s, const char *path);

bool gg_settings_save(const gg_settings *s, const char *path);

#endif // GG_SETTINGS_H
