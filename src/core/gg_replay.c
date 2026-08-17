// gg_replay.c - hashing a world, and writing down what made it.
#include "core/gg_replay.h"

// ---------------------------------------------------------------------------
// The hash
// ---------------------------------------------------------------------------
#define FNV_OFFSET 1469598103934665603ULL
#define FNV_PRIME  1099511628211ULL

static void mix(uint64_t *h, const void *data, size_t n) {
    const uint8_t *p = data;
    for (size_t i = 0; i < n; i++) {
        *h ^= p[i];
        *h *= FNV_PRIME;
    }
}

static void mix_u32(uint64_t *h, uint32_t v) { mix(h, &v, sizeof v); }
static void mix_i32(uint64_t *h, int32_t v)  { mix(h, &v, sizeof v); }

static void mix_str(uint64_t *h, const char *s) {
    mix(h, s, SDL_strlen(s));
    mix_u32(h, 0);                     // a terminator, so "ab"+"c" != "a"+"bc"
}

// Everything about one actor that the *world* would notice.
//
// Not `from_x`, `from_y`, `step`, `anim` or `idle`: those are how far through a
// slide the drawing is, and they are advanced by gg_game_animate on a frame
// rather than by gg_game_act on a turn. The simulation is turn-based and the
// presentation is not - so a world played at 60 Hz and the same world replayed
// as fast as it can be read are the same world, and a hash that said otherwise
// would report a divergence on every single replay. It did, the first time
// this was run.
static void mix_actor(uint64_t *h, const gg_actor *a) {
    mix_u32(h, a->active ? 1u : 0u);
    mix_u32(h, a->art);
    mix_u32(h, a->facing);
    mix_str(h, a->name);
    mix_i32(h, a->hp);
    mix_i32(h, a->hp_max);
    mix_u32(h, a->level);
    mix_u32(h, a->party);
    mix_u32(h, a->hostile ? 1u : 0u);
    mix_u32(h, a->speed);
    mix_i32(h, a->energy);
    mix_u32(h, a->damage);
    mix_u32(h, a->guard);
    mix_u32(h, a->reach);
    mix_u32(h, a->notice);
    mix_i32(h, a->flees);
    mix_u32(h, a->beast);
    mix_i32(h, a->x);
    mix_i32(h, a->y);
    mix_u32(h, a->def);
    mix_u32(h, a->schedn);
    for (int i = 0; i < a->schedn && i < GG_SCHEDULE_MAX; i++) {
        mix_u32(h, a->sched[i].hour);
        mix_i32(h, a->sched[i].x);
        mix_i32(h, a->sched[i].y);
    }
}

// The map's own state - what is on its floor and who it says lives in it. The
// terrain is hashed as a block: it is the largest part by far and the only one
// where a memcmp is exactly the right question.
static void mix_map(uint64_t *h, const gg_map *m) {
    mix_i32(h, m->w);
    mix_i32(h, m->h);
    mix_str(h, m->name);
    if (m->cell)
        mix(h, m->cell, (size_t)m->w * (size_t)m->h * sizeof *m->cell);

    mix_i32(h, m->grounds);
    for (int i = 0; i < m->grounds; i++) {
        mix_i32(h, m->ground[i].x);
        mix_i32(h, m->ground[i].y);
        mix_u32(h, m->ground[i].kind);
        mix_u32(h, m->ground[i].count);
    }

    mix_i32(h, m->actors);
    for (int i = 0; i < m->actors; i++) {
        mix_i32(h, m->actor[i].x);
        mix_i32(h, m->actor[i].y);
        mix_u32(h, m->actor[i].art);
        mix_str(h, m->actor[i].name);
    }
}

uint64_t gg_state_hash(const gg_game *g) {
    uint64_t h = FNV_OFFSET;

    mix_u32(&h, g->turn);
    mix_u32(&h, g->minutes);
    mix_u32(&h, g->day);
    mix_u32(&h, g->rng.s);
    mix_i32(&h, g->exp);
    mix_u32(&h, g->slain);
    mix_u32(&h, (uint32_t)g->mode);
    mix_u32(&h, g->story_over ? 1u : 0u);
    mix_str(&h, g->profile);
    mix_str(&h, g->here);

    mix_i32(&h, g->player);
    mix_i32(&h, g->actors);
    for (int i = 0; i < g->actors; i++) mix_actor(&h, &g->actor[i]);

    mix_i32(&h, g->trailn);
    for (int i = 0; i < g->trailn; i++) {
        mix_i32(&h, g->trail_x[i]);
        mix_i32(&h, g->trail_y[i]);
    }

    mix_i32(&h, g->packn);
    for (int i = 0; i < g->packn; i++) {
        mix_u32(&h, g->pack[i].kind);
        mix_u32(&h, g->pack[i].count);
    }
    for (int i = 0; i < GG_SLOT_COUNT; i++) mix_i32(&h, g->equipped[i]);
    mix_i32(&h, g->light_turns);
    mix_i32(&h, g->light_power);

    mix_i32(&h, g->knownn);
    for (int i = 0; i < g->knownn; i++) mix_str(&h, g->known[i]);
    mix_i32(&h, g->flags);
    for (int i = 0; i < g->flags; i++) mix_str(&h, g->flag[i]);
    for (int i = 0; i < GG_QUESTS_MAX; i++) mix_u32(&h, g->quest[i]);

    mix_map(&h, &g->map);

    // And everywhere else that is being held in mind, in the order it is held:
    // a world that has walked out of a map and left a body on it is not the
    // same world as one that has not.
    mix_i32(&h, g->visiteds);
    for (int i = 0; i < g->visiteds; i++) {
        mix_str(&h, g->visited[i].leaf);
        mix_map(&h, &g->visited[i].map);
        mix_i32(&h, g->visited[i].whos);
        for (int k = 0; k < g->visited[i].whos; k++)
            mix_actor(&h, &g->visited[i].who[k]);
    }

    return h;
}

// ---------------------------------------------------------------------------
// Action names
//
// One table, used in both directions, so a file cannot be written with one
// spelling and read with another.
// ---------------------------------------------------------------------------
static const char *const ACT_NAME[GG_ACT_COUNT] = {
    [GG_ACT_NONE]    = "NONE",
    [GG_ACT_N]       = "N",
    [GG_ACT_S]       = "S",
    [GG_ACT_E]       = "E",
    [GG_ACT_W]       = "W",
    [GG_ACT_NE]      = "NE",
    [GG_ACT_NW]      = "NW",
    [GG_ACT_SE]      = "SE",
    [GG_ACT_SW]      = "SW",
    [GG_ACT_WAIT]    = "WAIT",
    [GG_ACT_TALK]    = "TALK",
    [GG_ACT_LOOK]    = "LOOK",
    [GG_ACT_OPEN]    = "OPEN",
    [GG_ACT_GET]     = "GET",
    [GG_ACT_FIGHT]   = "FIGHT",
    [GG_ACT_CAST]    = "CAST",
    [GG_ACT_JOURNAL] = "JOURNAL",
    [GG_ACT_PACK]    = "PACK",
    [GG_ACT_USE]     = "USE",
    [GG_ACT_EQUIP]   = "EQUIP",
    [GG_ACT_DROP]    = "DROP",
};

const char *gg_action_name(gg_action a) {
    return (a >= 0 && a < GG_ACT_COUNT && ACT_NAME[a]) ? ACT_NAME[a] : "NONE";
}

gg_action gg_action_from_name(const char *name) {
    if (!name || !*name) return GG_ACT_NONE;
    for (int i = 0; i < GG_ACT_COUNT; i++)
        if (ACT_NAME[i] && SDL_strcasecmp(ACT_NAME[i], name) == 0)
            return (gg_action)i;
    return GG_ACT_COUNT;               // no such action - distinct from NONE
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------
static void say(gg_recorder *r, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
    SDL_PRINTF_VARARG_FUNC(2);

static void say(gg_recorder *r, const char *fmt, ...) {
    if (!r->open) return;
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    const size_t n = SDL_strlen(line);
    if (SDL_WriteIO(r->io, line, n) != n) {
        SDL_Log("gigantima: short write on the replay; recording stopped");
        SDL_CloseIO(r->io);
        r->open = false;
    }
}

bool gg_record_begin(gg_recorder *r, const char *path, const gg_game *g,
                     const char *map_leaf) {
    SDL_zerop(r);
    r->io = SDL_IOFromFile(path, "wb");
    if (!r->io) {
        SDL_Log("gigantima: cannot write %s: %s", path, SDL_GetError());
        return false;
    }
    r->open = true;

    say(r, "# gigantima replay\n");
    say(r, "# Play it back with --replay. The hash at the bottom is what this\n"
           "# session ended on; a replay that ends on another number has found\n"
           "# a divergence, and the turn it happened on is the bug.\n");
    // A world made by the generator is named by its seed; one read from a file
    // is named by the file, and seeds its RNG from the seed the map carries. So
    // only one of these two lines is ever written, and a `seed` line beside a
    // `map` line would be a number that decides nothing.
    //
    // The seed the *world* was built from, not the RNG state now: by the time a
    // recording starts the generator has already spent some of the stream, and
    // replaying from where it got to would build a different world.
    if (map_leaf && *map_leaf) {
        say(r, "map %s\n", map_leaf);
    } else {
        say(r, "seed %u\n", g->map.seed);
        say(r, "generated %d %d\n", g->map.w, g->map.h);
    }
    say(r, "profile %s\n", g->profile);
    say(r, "start %016llX\n", (unsigned long long)gg_state_hash(g));
    return r->open;
}

void gg_record_act(gg_recorder *r, gg_action a) {
    if (!r->open) return;
    say(r, "act %s\n", gg_action_name(a));
    r->acts++;
}

void gg_record_travel(gg_recorder *r, const char *leaf, int x, int y) {
    if (!r->open) return;
    say(r, "travel %s %d %d\n", leaf, x, y);
    r->acts++;
}

uint64_t gg_record_end(gg_recorder *r, const gg_game *g) {
    const uint64_t h = gg_state_hash(g);
    if (r->open) {
        say(r, "hash %016llX\n", (unsigned long long)h);
        SDL_CloseIO(r->io);
        r->open = false;
    }
    return h;
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------
static char *skip_spaces(char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static char *split_word(char *line, char **rest) {
    char *p = skip_spaces(line);
    char *hash = SDL_strchr(p, '#');
    if (hash) *hash = '\0';
    if (!*p) return nullptr;

    char *q = p;
    while (*q && *q != ' ' && *q != '\t') q++;
    if (*q) { *q = '\0'; *rest = skip_spaces(q + 1); }
    else    { *rest = q; }

    size_t n = SDL_strlen(*rest);
    while (n > 0 && ((*rest)[n - 1] == ' ' || (*rest)[n - 1] == '\t' ||
                     (*rest)[n - 1] == '\r' || (*rest)[n - 1] == '\n'))
        (*rest)[--n] = '\0';
    return p;
}

static bool next_token(char **p, char *out, size_t n) {
    char *w = skip_spaces(*p);
    if (!*w) return false;
    char *end = w;
    while (*end && *end != ' ' && *end != '\t') end++;
    const char save = *end;
    *end = '\0';
    SDL_strlcpy(out, w, n);
    *end = save;
    *p = skip_spaces(end);
    return true;
}

static bool as_int(const char *s, int *out) {
    if (!s || !*s) return false;
    const bool neg = *s == '-';
    if (neg) s++;
    if (!*s) return false;
    int v = 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') return false;
        v = v * 10 + (*p - '0');
    }
    *out = neg ? -v : v;
    return true;
}

static bool as_hex64(const char *s, uint64_t *out) {
    if (!s || !*s) return false;
    uint64_t v = 0;
    for (const char *p = s; *p; p++) {
        v <<= 4;
        if      (*p >= '0' && *p <= '9') v |= (uint64_t)(*p - '0');
        else if (*p >= 'a' && *p <= 'f') v |= (uint64_t)(*p - 'a' + 10);
        else if (*p >= 'A' && *p <= 'F') v |= (uint64_t)(*p - 'A' + 10);
        else return false;
    }
    *out = v;
    return true;
}

static void complain(const char *path, int line, const char *what) {
    SDL_Log("gigantima: %s:%d: %s", path, line, what);
}

void gg_replay_free(gg_replay *r) {
    if (!r) return;
    SDL_free(r->step);
    SDL_zerop(r);
}

bool gg_replay_load(gg_replay *r, const char *path) {
    SDL_zerop(r);

    size_t size = 0;
    char *text = SDL_LoadFile(path, &size);
    if (!text) {
        SDL_Log("gigantima: cannot read %s: %s", path, SDL_GetError());
        return false;
    }

    // One step per line at the very most, so one allocation is enough and the
    // reader never has to grow anything mid-file.
    int lines = 1;
    for (size_t i = 0; i < size; i++) if (text[i] == '\n') lines++;
    if (lines > GG_REPLAY_MAX) {
        SDL_Log("gigantima: %s has %d lines, more than a replay may hold",
                path, lines);
        SDL_free(text);
        return false;
    }
    r->step = SDL_calloc((size_t)lines, sizeof *r->step);
    if (!r->step) {
        SDL_free(text);
        return false;
    }

    bool ok = true;
    int lineno = 0;
    char *cursor = text;

    while (ok && cursor && *cursor) {
        char *nl = SDL_strchr(cursor, '\n');
        if (nl) *nl = '\0';
        char *line = cursor;
        cursor = nl ? nl + 1 : nullptr;
        lineno++;

        // A line ending is "\n" or "\r\n". Left on, the carriage return makes
        // a *blank* line a line holding "\r" - which is not blank, and is read
        // as a keyword nobody has ever heard of. Every one of these files then
        // refuses to load in its entirety, on Windows only, which is where a
        // checkout puts CRLF and where somebody editing it in Notepad will put
        // it too. Found by the Windows job and nowhere else.
        for (size_t n = SDL_strlen(line);
             n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n'); n--)
            line[n - 1] = '\0';

        char *rest = nullptr;
        char *key = split_word(line, &rest);
        if (!key) continue;

        if (SDL_strcasecmp(key, "seed") == 0) {
            int v = 0;
            ok = as_int(rest, &v);
            if (!ok) { complain(path, lineno, "a seed that is not a number"); break; }
            r->seed = (uint32_t)v;
        } else if (SDL_strcasecmp(key, "map") == 0) {
            SDL_strlcpy(r->map, rest, sizeof r->map);
        } else if (SDL_strcasecmp(key, "generated") == 0) {
            char w[16], h[16];
            if (!next_token(&rest, w, sizeof w) ||
                !next_token(&rest, h, sizeof h) ||
                !as_int(w, &r->w) || !as_int(h, &r->h)) {
                complain(path, lineno, "`generated` wants a width and a height");
                ok = false;
                break;
            }
        } else if (SDL_strcasecmp(key, "profile") == 0) {
            SDL_strlcpy(r->profile, rest, sizeof r->profile);
        } else if (SDL_strcasecmp(key, "start") == 0) {
            // Read and ignored: it is there for a human comparing two files.
        } else if (SDL_strcasecmp(key, "hash") == 0) {
            ok = as_hex64(rest, &r->hash);
            if (!ok) { complain(path, lineno, "a hash that is not hex"); break; }
            r->has_hash = true;
        } else if (SDL_strcasecmp(key, "act") == 0) {
            const gg_action a = gg_action_from_name(rest);
            if (a == GG_ACT_COUNT) {
                SDL_Log("gigantima: %s:%d: there is no action called '%s'",
                        path, lineno, rest);
                ok = false;
                break;
            }
            r->step[r->steps].kind = GG_STEP_ACT;
            r->step[r->steps].act = (uint8_t)a;
            r->steps++;
        } else if (SDL_strcasecmp(key, "travel") == 0) {
            char leaf[GG_MAP_NAME_MAX], x[16], y[16];
            int px = 0, py = 0;
            if (!next_token(&rest, leaf, sizeof leaf) ||
                !next_token(&rest, x, sizeof x) ||
                !next_token(&rest, y, sizeof y) ||
                !as_int(x, &px) || !as_int(y, &py)) {
                complain(path, lineno, "`travel` wants a map and a tile in it");
                ok = false;
                break;
            }
            r->step[r->steps].kind = GG_STEP_TRAVEL;
            SDL_strlcpy(r->step[r->steps].leaf, leaf, GG_MAP_NAME_MAX);
            r->step[r->steps].x = (int16_t)px;
            r->step[r->steps].y = (int16_t)py;
            r->steps++;
        } else {
            SDL_Log("gigantima: %s:%d: no idea what `%s` means", path, lineno, key);
            ok = false;
        }
    }

    SDL_free(text);

    if (ok && !r->map[0] && (r->w <= 0 || r->h <= 0)) {
        SDL_Log("gigantima: %s says neither which map nor what size of world",
                path);
        ok = false;
    }

    if (!ok) {
        gg_replay_free(r);
        return false;
    }
    return true;
}
