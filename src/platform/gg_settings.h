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

#define GG_SETTINGS_FILE "settings.txt"
#define GG_LAST_PROFILE_MAX 32

typedef struct {
    int  scale;            // 1..4 window scale
    bool fullscreen;
    bool rumble;
    int  music;            // 0..10, reserved until there is audio to turn down
    int  effects;          // 0..10, likewise
    char last_profile[GG_LAST_PROFILE_MAX];
} gg_settings;

// The defaults a first run gets.
void gg_settings_defaults(gg_settings *s);

// Reads from `path`. Missing or unreadable leaves `s` at its defaults and
// returns false - which is not an error, it is a first run.
bool gg_settings_load(gg_settings *s, const char *path);

bool gg_settings_save(const gg_settings *s, const char *path);

#endif // GG_SETTINGS_H
