// gg_save.c - writing and reading a saved game, and the profiles around them.
#include "core/gg_save.h"

#define SAVE_FILE    "world.ggsave"
#define PROFILE_FILE "profile.ggprof"

// ---------------------------------------------------------------------------
// Names and paths
// ---------------------------------------------------------------------------
bool gg_profile_name_ok(const char *name) {
    if (!name || !*name) return false;

    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++, n++) {
        if (n >= GG_PROFILE_NAME_MAX - 1) return false;
        // An allow-list, not a deny-list. A profile name becomes a directory
        // name, and the ways a string can escape a directory are far too
        // numerous and platform-specific to enumerate - "..", separators of
        // either slash, drive letters, NUL, trailing dots and spaces on
        // Windows, reserved device names. Permitting a small set closes all of
        // them at once.
        const bool allowed = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                             (*p >= '0' && *p <= '9') || *p == ' ' || *p == '_' ||
                             *p == '-' || *p == '\'';
        if (!allowed) return false;
    }
    if (n == 0) return false;

    // Leading or trailing spaces survive a round trip badly and read as a
    // different name to a human, which is exactly the confusion to avoid when
    // somebody is choosing which of their games to continue.
    if (name[0] == ' ' || name[n - 1] == ' ') return false;
    return true;
}

bool gg_profile_dir(const char *base, const char *name, char *out, size_t n) {
    if (out && n) out[0] = '\0';
    if (!base || !out || !gg_profile_name_ok(name)) return false;

    const int wrote = SDL_snprintf(out, n, "%sprofiles/%s/", base, name);
    if (wrote < 0 || (size_t)wrote >= n) {
        out[0] = '\0';
        return false;
    }
    return true;
}

static bool profile_file(const char *base, const char *name, const char *leaf,
                         char *out, size_t n) {
    char dir[1024];
    if (!gg_profile_dir(base, name, dir, sizeof dir)) return false;
    const int wrote = SDL_snprintf(out, n, "%s%s", dir, leaf);
    return wrote >= 0 && (size_t)wrote < n;
}

// ---------------------------------------------------------------------------
// The header file
//
// Small and separate, so listing a dozen profiles costs a dozen tiny reads
// rather than a dozen whole worlds.
// ---------------------------------------------------------------------------
static bool header_write(const gg_game *g, const char *base, const char *name) {
    char path[1024];
    if (!profile_file(base, name, PROFILE_FILE, path, sizeof path)) return false;

    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) return false;

    // Both strings are copied into a zeroed field before being written, never
    // written from the caller's pointer. A fixed-width field is written at its
    // full width, and `name` is whatever the caller passed - a string literal
    // in the tests - so writing 32 bytes from it reads off the end of it.
    // AddressSanitizer caught exactly that; an ordinary build had not.
    char who[GG_PROFILE_NAME_MAX] = { 0 };
    SDL_strlcpy(who, name, sizeof who);

    char place[GG_MAP_NAME_MAX] = { 0 };
    SDL_strlcpy(place, gg_game_place(g), sizeof place);

    bool ok = SDL_WriteIO(io, GG_SAVE_MAGIC, 8) == 8;
    ok = ok && gg_io_w32(io, GG_SAVE_VERSION);
    ok = ok && SDL_WriteIO(io, who, sizeof who) == sizeof who;
    ok = ok && gg_io_w32(io, g->day) && gg_io_w32(io, g->minutes);
    ok = ok && gg_io_w32(io, g->turn) && gg_io_w32(io, (uint32_t)g->level);
    ok = ok && SDL_WriteIO(io, place, sizeof place) == sizeof place;

    SDL_CloseIO(io);
    return ok;
}

bool gg_profile_read(const char *base, const char *name, gg_profile *out) {
    char path[1024];
    if (!out || !profile_file(base, name, PROFILE_FILE, path, sizeof path))
        return false;

    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io) return false;

    SDL_zerop(out);
    char magic[8];
    uint32_t version = 0, lvl = 0;
    bool ok = SDL_ReadIO(io, magic, 8) == 8 &&
              SDL_memcmp(magic, GG_SAVE_MAGIC, 8) == 0 &&
              gg_io_r32(io, &version) && version == GG_SAVE_VERSION;
    ok = ok && SDL_ReadIO(io, out->name, GG_PROFILE_NAME_MAX) == GG_PROFILE_NAME_MAX;
    ok = ok && gg_io_r32(io, &out->day) && gg_io_r32(io, &out->minutes);
    ok = ok && gg_io_r32(io, &out->turns) && gg_io_r32(io, &lvl);
    ok = ok && SDL_ReadIO(io, out->place, GG_MAP_NAME_MAX) == GG_MAP_NAME_MAX;
    SDL_CloseIO(io);

    if (!ok) return false;
    out->name[GG_PROFILE_NAME_MAX - 1] = '\0';
    out->place[GG_MAP_NAME_MAX - 1] = '\0';
    out->level = lvl;
    out->has_save = gg_save_exists(base, name);
    return true;
}

// ---------------------------------------------------------------------------
// The world
// ---------------------------------------------------------------------------
static bool actor_write(SDL_IOStream *io, const gg_actor *a) {
    bool ok = gg_io_w32(io, a->active ? 1u : 0u);
    ok = ok && gg_io_w32(io, a->art) && gg_io_w32(io, a->facing);
    ok = ok && SDL_WriteIO(io, a->name, GG_ACTOR_NAME_MAX) == GG_ACTOR_NAME_MAX;
    ok = ok && gg_io_w32(io, (uint32_t)(uint16_t)a->x);
    ok = ok && gg_io_w32(io, (uint32_t)(uint16_t)a->y);
    ok = ok && gg_io_w32(io, a->def);
    ok = ok && gg_io_w32(io, a->schedn);
    for (int i = 0; ok && i < a->schedn; i++) {
        ok = gg_io_w32(io, a->sched[i].hour) &&
             gg_io_w32(io, (uint32_t)(uint16_t)a->sched[i].x) &&
             gg_io_w32(io, (uint32_t)(uint16_t)a->sched[i].y);
    }
    return ok;
}

static bool actor_read(SDL_IOStream *io, gg_actor *a) {
    SDL_zerop(a);
    uint32_t active = 0, art = 0, facing = 0, x = 0, y = 0, def = 0, n = 0;
    bool ok = gg_io_r32(io, &active) && gg_io_r32(io, &art) &&
              gg_io_r32(io, &facing);
    ok = ok && SDL_ReadIO(io, a->name, GG_ACTOR_NAME_MAX) == GG_ACTOR_NAME_MAX;
    a->name[GG_ACTOR_NAME_MAX - 1] = '\0';
    ok = ok && gg_io_r32(io, &x) && gg_io_r32(io, &y) && gg_io_r32(io, &def);
    ok = ok && gg_io_r32(io, &n) && n <= GG_SCHEDULE_MAX;
    if (!ok) return false;

    a->active = active != 0;
    // Clamped rather than trusted: an art id past the end of the table would
    // index off it in the renderer, and this file may not be ours.
    a->art = (uint8_t)(art < GG_ACTOR_COUNT ? art : 0);
    a->facing = (uint8_t)(facing < GG_ACTOR_DIRS ? facing : GG_FACE_DOWN);
    a->x = (int16_t)(uint16_t)x;
    a->y = (int16_t)(uint16_t)y;
    a->from_x = a->x;
    a->from_y = a->y;
    a->def = (uint8_t)def;
    a->schedn = (uint8_t)n;

    for (uint32_t i = 0; i < n; i++) {
        uint32_t h = 0, sx = 0, sy = 0;
        if (!gg_io_r32(io, &h) || !gg_io_r32(io, &sx) || !gg_io_r32(io, &sy))
            return false;
        a->sched[i].hour = (uint8_t)(h % 24);
        a->sched[i].x = (int16_t)(uint16_t)sx;
        a->sched[i].y = (int16_t)(uint16_t)sy;
    }
    return true;
}

bool gg_save_write(const gg_game *g, const char *base, const char *name) {
    char dir[1024], path[1024];
    if (!gg_profile_dir(base, name, dir, sizeof dir)) {
        SDL_Log("gigantima: '%s' is not a name a profile may have", name ? name : "");
        return false;
    }

    // SDL_CreateDirectory makes intermediate directories, so `profiles/` comes
    // into being with the first profile rather than needing its own step.
    if (!SDL_CreateDirectory(dir)) {
        SDL_Log("gigantima: cannot make %s: %s", dir, SDL_GetError());
        return false;
    }
    if (!profile_file(base, name, SAVE_FILE, path, sizeof path)) return false;

    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) {
        SDL_Log("gigantima: cannot write %s: %s", path, SDL_GetError());
        return false;
    }

    bool ok = SDL_WriteIO(io, GG_SAVE_MAGIC, 8) == 8;
    ok = ok && gg_io_w32(io, GG_SAVE_VERSION);

    ok = ok && gg_io_w32(io, g->rng.s);
    ok = ok && gg_io_w32(io, g->turn) && gg_io_w32(io, g->minutes) &&
               gg_io_w32(io, g->day);
    ok = ok && gg_io_w32(io, (uint32_t)g->hp) && gg_io_w32(io, (uint32_t)g->hp_max);
    ok = ok && gg_io_w32(io, (uint32_t)g->level) && gg_io_w32(io, (uint32_t)g->exp);
    for (int i = 0; ok && i < GG_ITEM_COUNT; i++)
        ok = gg_io_w32(io, (uint32_t)g->item[i]);
    ok = ok && gg_io_w32(io, (uint32_t)g->player);
    ok = ok && SDL_WriteIO(io, g->profile, sizeof g->profile) == sizeof g->profile;

    ok = ok && gg_io_w32(io, (uint32_t)g->actors);
    for (int i = 0; ok && i < g->actors; i++) ok = actor_write(io, &g->actor[i]);

    // The map last, because it is by far the largest part and a reader that
    // fails earlier need not have touched it.
    ok = ok && gg_map_write(&g->map, io);

    SDL_CloseIO(io);
    if (!ok) {
        SDL_Log("gigantima: short write saving %s", path);
        return false;
    }
    return header_write(g, base, name);
}

bool gg_save_read(gg_game *g, const char *base, const char *name) {
    char path[1024];
    if (!profile_file(base, name, SAVE_FILE, path, sizeof path)) return false;

    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io) return false;

    // Built into a local and only moved into `g` once every part has read
    // cleanly, so a truncated file cannot leave a half-loaded world behind.
    gg_game tmp;
    SDL_zerop(&tmp);
    tmp.talking_to = -1;
    tmp.mode = GG_MODE_PLAY;

    char magic[8];
    uint32_t version = 0, rng = 0, hp = 0, hpmax = 0, lvl = 0, exp = 0;
    uint32_t player = 0, actors = 0;

    bool ok = SDL_ReadIO(io, magic, 8) == 8 &&
              SDL_memcmp(magic, GG_SAVE_MAGIC, 8) == 0;
    if (!ok) {
        SDL_Log("gigantima: %s is not a saved game", path);
        SDL_CloseIO(io);
        return false;
    }
    ok = gg_io_r32(io, &version) && version == GG_SAVE_VERSION;
    if (!ok) {
        SDL_Log("gigantima: %s is save version %u, this build reads %d",
                path, version, GG_SAVE_VERSION);
        SDL_CloseIO(io);
        return false;
    }

    ok = gg_io_r32(io, &rng);
    ok = ok && gg_io_r32(io, &tmp.turn) && gg_io_r32(io, &tmp.minutes) &&
               gg_io_r32(io, &tmp.day);
    ok = ok && gg_io_r32(io, &hp) && gg_io_r32(io, &hpmax) &&
               gg_io_r32(io, &lvl) && gg_io_r32(io, &exp);
    for (int i = 0; ok && i < GG_ITEM_COUNT; i++) {
        uint32_t v = 0;
        ok = gg_io_r32(io, &v);
        tmp.item[i] = (int)v;
    }
    ok = ok && gg_io_r32(io, &player);
    ok = ok && SDL_ReadIO(io, tmp.profile, sizeof tmp.profile) == sizeof tmp.profile;
    tmp.profile[sizeof tmp.profile - 1] = '\0';

    ok = ok && gg_io_r32(io, &actors) && actors <= GG_ACTORS_MAX;
    for (uint32_t i = 0; ok && i < actors; i++) ok = actor_read(io, &tmp.actor[i]);
    tmp.actors = ok ? (int)actors : 0;

    ok = ok && gg_map_read(&tmp.map, io);
    SDL_CloseIO(io);

    if (!ok) {
        SDL_Log("gigantima: %s is truncated or corrupt", path);
        gg_map_free(&tmp.map);
        return false;
    }

    tmp.rng.s = rng ? rng : 1;           // a zero xorshift state never advances
    tmp.hp = (int)hp;
    tmp.hp_max = (int)hpmax;
    tmp.level = (int)lvl;
    tmp.exp = (int)exp;
    tmp.minutes %= GG_MINUTES_PER_DAY;
    // The player index must land on a real actor, or every query through
    // gg_player would read off the end of the array.
    tmp.player = (player < (uint32_t)tmp.actors) ? (int)player : 0;

    if (!gg_path_init(&tmp.path, tmp.map.w, tmp.map.h)) {
        gg_map_free(&tmp.map);
        return false;
    }

    gg_game_free(g);
    *g = tmp;
    gg_game_rebind_actors(g);
    gg_log(g, "%s. Day %u, %s.", g->map.name, g->day, gg_game_place(g));
    return true;
}

bool gg_save_exists(const char *base, const char *name) {
    char path[1024];
    if (!profile_file(base, name, SAVE_FILE, path, sizeof path)) return false;

    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io) return false;
    SDL_CloseIO(io);
    return true;
}

bool gg_profile_delete(const char *base, const char *name) {
    char dir[1024], path[1024];
    if (!gg_profile_dir(base, name, dir, sizeof dir)) return false;

    if (profile_file(base, name, SAVE_FILE, path, sizeof path))
        SDL_RemovePath(path);
    if (profile_file(base, name, PROFILE_FILE, path, sizeof path))
        SDL_RemovePath(path);

    // The directory goes only if it is now empty; SDL_RemovePath refuses
    // otherwise, which is the behaviour wanted - anything else in there is
    // not ours to throw away.
    SDL_RemovePath(dir);
    return true;
}

// ---------------------------------------------------------------------------
// Listing
// ---------------------------------------------------------------------------
static int profile_cmp(const void *va, const void *vb) {
    const gg_profile *a = va, *b = vb;
    // Newest first, by in-world time. Ties on the name, so the order is total
    // and the list does not shuffle between runs.
    if (a->day != b->day) return a->day > b->day ? -1 : 1;
    if (a->turns != b->turns) return a->turns > b->turns ? -1 : 1;
    return SDL_strcmp(a->name, b->name);
}

int gg_profile_list(const char *base, gg_profile *out, int max) {
    if (!base || !out || max <= 0) return 0;

    char root[1024];
    if (SDL_snprintf(root, sizeof root, "%sprofiles", base) < 0) return 0;

    int count = 0;
    char **names = SDL_GlobDirectory(root, nullptr, 0, &count);
    if (!names) return 0;                 // no profiles yet is not an error

    int found = 0;
    for (int i = 0; i < count && found < max; i++) {
        // Anything in there that is not a profile we wrote is skipped rather
        // than reported: the directory belongs to the player, not to us.
        if (gg_profile_read(base, names[i], &out[found])) found++;
    }
    SDL_free(names);

    SDL_qsort(out, (size_t)found, sizeof *out, profile_cmp);
    return found;
}
