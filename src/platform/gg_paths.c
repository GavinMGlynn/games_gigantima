// gg_paths.c - asset and preference directory resolution.
#include "platform/gg_paths.h"

#ifndef GG_SOURCE_ASSETS
#  define GG_SOURCE_ASSETS ""      // CMake normally defines this
#endif

static char g_assets[1024];
static bool g_probed;

static bool dir_has_atlas(const char *dir) {
    if (!dir || !*dir) return false;
    char probe[1024];
    // atlas_tiles.png is the one file the game cannot start without, so its
    // presence is the test for "this is the assets directory" rather than the
    // directory merely existing.
    SDL_snprintf(probe, sizeof probe, "%satlas_tiles.png", dir);
    SDL_IOStream *io = SDL_IOFromFile(probe, "rb");
    if (!io) return false;
    SDL_CloseIO(io);
    return true;
}

static void probe_assets(void) {
    if (g_probed) return;
    g_probed = true;

    char cand[1024];
    const char *env = SDL_getenv("GIGANTIMA_ASSETS");
    if (env && *env) {
        SDL_snprintf(cand, sizeof cand, "%s%s", env,
                     env[SDL_strlen(env) - 1] == '/' ? "" : "/");
        if (dir_has_atlas(cand)) {
            SDL_strlcpy(g_assets, cand, sizeof g_assets);
            return;
        }
        SDL_Log("gigantima: GIGANTIMA_ASSETS=%s has no atlas_tiles.png", env);
    }

    const char *base = SDL_GetBasePath();
    if (base) {
        SDL_snprintf(cand, sizeof cand, "%sassets/", base);
        if (dir_has_atlas(cand)) {
            SDL_strlcpy(g_assets, cand, sizeof g_assets);
            return;
        }
        SDL_snprintf(cand, sizeof cand, "%s../assets/", base);
        if (dir_has_atlas(cand)) {
            SDL_strlcpy(g_assets, cand, sizeof g_assets);
            return;
        }
    }

    if (GG_SOURCE_ASSETS[0]) {
        SDL_snprintf(cand, sizeof cand, "%s/", GG_SOURCE_ASSETS);
        if (dir_has_atlas(cand)) {
            SDL_strlcpy(g_assets, cand, sizeof g_assets);
            return;
        }
    }

    SDL_Log("gigantima: no assets directory found. Looked in "
            "$GIGANTIMA_ASSETS, <exe>/assets, <exe>/../assets%s%s",
            GG_SOURCE_ASSETS[0] ? " and " : "", GG_SOURCE_ASSETS);
}

bool gg_assets_found(void) {
    probe_assets();
    return g_assets[0] != '\0';
}

const char *gg_asset_path(const char *name) {
    static char buf[1024];
    probe_assets();
    SDL_snprintf(buf, sizeof buf, "%s%s", g_assets, name);
    return buf;
}

const char *gg_pref_path(void) {
    static char buf[1024];
    if (!buf[0]) {
        // SDL creates the directory and returns a trailing separator. A null
        // return means no writable home, in which case saving is simply
        // unavailable - the game still runs.
        char *p = SDL_GetPrefPath("gigantima", "gigantima");
        if (p) {
            SDL_strlcpy(buf, p, sizeof buf);
            SDL_free(p);
        } else {
            SDL_Log("gigantima: no writable preferences directory: %s",
                    SDL_GetError());
        }
    }
    return buf;
}

const char *gg_pref_file(const char *name) {
    static char buf[1024];
    SDL_snprintf(buf, sizeof buf, "%s%s", gg_pref_path(), name);
    return buf;
}
