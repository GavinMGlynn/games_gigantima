// gg_save.h - saved games, and the profiles they belong to.
//
// A profile is a player: a name, and a directory of their own. Several people
// share a machine, and each should find their own world where they left it.
//
// Paths arrive from the caller rather than being resolved here. `src/core/`
// must not know where a preferences directory lives - that is the platform's
// business - and passing the base directory in also means the tests can write
// somewhere disposable.
#ifndef GG_SAVE_H
#define GG_SAVE_H

#include "core/gg_common.h"
#include "core/gg_game.h"

#define GG_SAVE_MAGIC   "GGSAVE\0\0"
// Version 4. Each version so far has moved something that was nowhere into the
// file: the pack, then the words learned, then each actor's stats and who walks
// with you, then what each of them fights with and what they leave behind, and
// now any light the Avatar has conjured and how long it has left.
// There is no migration between any of them - guessing at a section that was
// never written would resume somebody into a world that is not the one they
// left, and every version so far has existed for days rather than years.
#define GG_SAVE_VERSION 5

#define GG_PROFILE_NAME_MAX 32
#define GG_PROFILES_MAX     64

// What the profile picker needs to draw a row without loading the whole world.
// Kept in a small file beside the save so listing profiles stays cheap.
typedef struct {
    char     name[GG_PROFILE_NAME_MAX];
    uint32_t day;          // in-world day the save sits on
    uint32_t minutes;      // and the time of that day
    uint32_t turns;
    uint32_t level;
    char     place[GG_MAP_NAME_MAX];   // where they were, in words
    bool     has_save;
} gg_profile;

// --- names -----------------------------------------------------------------
// Is this a name a profile may have? Rejects anything that is empty, too long,
// or that could steer a path somewhere it should not go.
bool gg_profile_name_ok(const char *name);

// Writes the directory a profile's files live in, with a trailing separator.
// False if the name is not allowed or the buffer is too small; the buffer is
// left empty rather than half-written.
bool gg_profile_dir(const char *base, const char *name, char *out, size_t n);

// --- saving ----------------------------------------------------------------
// Writes the whole world: the map, every actor, the clock, and what the avatar
// is carrying. Creates the profile's directory if it does not exist.
bool gg_save_write(const gg_game *g, const char *base, const char *name);

// Reads one back. On success the game is exactly what was saved, including the
// RNG, so the world carries on making the same decisions it would have.
// On failure `g` is left untouched and nothing is allocated.
bool gg_save_read(gg_game *g, const char *base, const char *name);

// True if this profile has a save to continue from.
bool gg_save_exists(const char *base, const char *name);

// Deletes a profile's save and its directory. False if the name is not
// allowed; true if there was nothing there to delete.
bool gg_profile_delete(const char *base, const char *name);

// --- listing ---------------------------------------------------------------
// Fills `out` with every profile found under `base`, newest first by day then
// turns, and returns how many. Reads only the small header file per profile.
int gg_profile_list(const char *base, gg_profile *out, int max);

// Reads one profile's header. False if it has none.
bool gg_profile_read(const char *base, const char *name, gg_profile *out);

#endif // GG_SAVE_H
