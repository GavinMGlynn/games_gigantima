// gg_paths.h - where the art lives, and where a player's saves go.
#ifndef GG_PATHS_H
#define GG_PATHS_H

#include "core/gg_common.h"

// Absolute path to an asset, resolved against the first assets directory that
// actually exists. Returns a pointer to a static buffer, valid until the next
// call - callers use it immediately to open a file and never store it.
//
// The search order, and why:
//   1. $GIGANTIMA_ASSETS      an explicit override always wins
//   2. <exe>/assets           an installed or packaged build
//   3. <exe>/../assets        a CMake build tree, where the exe sits in build/
//   4. GG_SOURCE_ASSETS       the source tree, compiled in by CMake
// Without 3 and 4 the game would only run after `cmake --install`, which makes
// the ordinary edit-build-run loop needlessly slow.
const char *gg_asset_path(const char *name);

// True if the assets directory was found at all. Worth asking once at startup
// so the failure is one clear message rather than four missing textures.
bool gg_assets_found(void);

// The directory this player's profiles and saves live in, created if needed.
// Static buffer, same rules as above.
const char *gg_pref_path(void);

// Absolute path to a file inside the pref directory.
const char *gg_pref_file(const char *name);

#endif // GG_PATHS_H
