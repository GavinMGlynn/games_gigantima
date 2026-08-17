// gg_bestiary.c - reading what lives in the world.
//
// Same format as the dialogue and the spells, for the same reason: one shape of
// file to learn, and a person who can write one can write all three.
#include "core/gg_bestiary.h"

static gg_beast g_beast[GG_BEASTS_MAX];
static int      g_beasts;

void gg_bestiary_clear(void) {
    SDL_zeroa(g_beast);
    g_beasts = 0;
}

int gg_bestiary_count(void) { return g_beasts; }

const gg_beast *gg_bestiary_at(int i) {
    return (i >= 0 && i < g_beasts) ? &g_beast[i] : nullptr;
}

int gg_bestiary_find(const char *id) {
    if (!id || !*id) return -1;
    for (int i = 0; i < g_beasts; i++)
        if (SDL_strcasecmp(g_beast[i].id, id) == 0) return i;
    return -1;
}

// ---------------------------------------------------------------------------
// Parsing
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
    int v = 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') return false;
        v = v * 10 + (*p - '0');
    }
    *out = v;
    return true;
}

static int art_from(const char *word) {
    for (int i = 0; i < GG_ACTOR_COUNT; i++)
        if (SDL_strcasecmp(GG_ACTOR_ID_NAME[i], word) == 0) return i;
    return -1;
}

static int item_from(const char *word) {
    for (int i = 0; i < GG_ITEM_COUNT; i++)
        if (SDL_strcasecmp(GG_ITEM[i].id, word) == 0) return i;
    return -1;
}

static void complain(const char *path, int line, const char *what) {
    SDL_Log("gigantima: %s:%d: %s", path, line, what);
}

// A plain `keyword N` line, into the field the caller points at.
static bool number_into(const char *path, int lineno, const char *rest,
                        int low, int high, int *out) {
    int v = 0;
    if (!as_int(rest, &v) || v < low || v > high) {
        SDL_Log("gigantima: %s:%d: expected a number from %d to %d, got '%s'",
                path, lineno, low, high, rest);
        return false;
    }
    *out = v;
    return true;
}

bool gg_bestiary_load(const char *path) {
    gg_bestiary_clear();

    size_t size = 0;
    char *text = SDL_LoadFile(path, &size);
    if (!text) {
        SDL_Log("gigantima: cannot read %s: %s", path, SDL_GetError());
        return false;
    }

    gg_beast *b = nullptr;
    bool ok = true;
    int lineno = 0;
    char *cursor = text;

    while (ok && cursor && *cursor) {
        char *nl = SDL_strchr(cursor, '\n');
        if (nl) *nl = '\0';
        char *line = cursor;
        cursor = nl ? nl + 1 : nullptr;
        lineno++;

        char *rest = nullptr;
        char *key = split_word(line, &rest);
        if (!key) continue;

        if (SDL_strcasecmp(key, "creature") == 0) {
            if (!*rest) { complain(path, lineno, "a creature with no id"); ok = false; break; }
            if (g_beasts >= GG_BEASTS_MAX) {
                complain(path, lineno, "too many creatures in one bestiary");
                ok = false;
                break;
            }
            b = &g_beast[g_beasts++];
            SDL_zerop(b);
            SDL_strlcpy(b->id, rest, sizeof b->id);
            // Defaults, so a short entry is a workable creature rather than a
            // motionless one with no health.
            b->health = 10;
            b->level = 1;
            b->speed = 100;
            // Damage is deliberately NOT defaulted. A creature that can hurt
            // nobody is scenery, and the check at the bottom refuses one - but
            // a default of 1 made that check unreachable, so the file has to
            // say. Caught by the malformed-bestiary test, which is what it is
            // there for.
            b->reach = 1;
            b->notice = 8;
            b->haunts = 0;
            continue;
        }

        if (!b) {
            complain(path, lineno, "this line comes before any `creature`");
            ok = false;
            break;
        }

        int v = 0;
        if (SDL_strcasecmp(key, "name") == 0) {
            SDL_strlcpy(b->name, rest, sizeof b->name);
        } else if (SDL_strcasecmp(key, "art") == 0) {
            const int art = art_from(rest);
            if (art < 0) {
                SDL_Log("gigantima: %s:%d: there is no art called '%s'",
                        path, lineno, rest);
                ok = false;
                break;
            }
            b->art = (uint8_t)art;
        } else if (SDL_strcasecmp(key, "health") == 0) {
            ok = number_into(path, lineno, rest, 1, 30000, &v);
            b->health = (int16_t)v;
        } else if (SDL_strcasecmp(key, "level") == 0) {
            ok = number_into(path, lineno, rest, 1, 255, &v);
            b->level = (uint8_t)v;
        } else if (SDL_strcasecmp(key, "speed") == 0) {
            ok = number_into(path, lineno, rest, 1, 255, &v);
            b->speed = (uint8_t)v;
        } else if (SDL_strcasecmp(key, "damage") == 0) {
            ok = number_into(path, lineno, rest, 0, 255, &v);
            b->damage = (uint8_t)v;
        } else if (SDL_strcasecmp(key, "guard") == 0) {
            ok = number_into(path, lineno, rest, 0, 255, &v);
            b->guard = (uint8_t)v;
        } else if (SDL_strcasecmp(key, "reach") == 0) {
            ok = number_into(path, lineno, rest, 1, 12, &v);
            b->reach = (uint8_t)v;
        } else if (SDL_strcasecmp(key, "notice") == 0) {
            ok = number_into(path, lineno, rest, 1, 64, &v);
            b->notice = (uint8_t)v;
        } else if (SDL_strcasecmp(key, "flees") == 0) {
            ok = number_into(path, lineno, rest, 0, 30000, &v);
            b->flees = (int16_t)v;
        } else if (SDL_strcasecmp(key, "haunts") == 0) {
            ok = number_into(path, lineno, rest, 0, 64, &v);
            b->haunts = (uint8_t)v;
        } else if (SDL_strcasecmp(key, "loot") == 0) {
            if (b->loots >= GG_BEAST_LOOT_MAX) {
                complain(path, lineno, "more loot than one creature may carry");
                ok = false;
                break;
            }
            char what[GG_BEAST_NAME_MAX], a[16], z[16], pc[16];
            if (!next_token(&rest, what, sizeof what) ||
                !next_token(&rest, a, sizeof a) ||
                !next_token(&rest, z, sizeof z) ||
                !next_token(&rest, pc, sizeof pc)) {
                complain(path, lineno,
                         "loot wants a thing, a least, a most and a chance");
                ok = false;
                break;
            }
            const int kind = item_from(what);
            int least = 0, most = 0, chance = 0;
            if (kind < 0) {
                SDL_Log("gigantima: %s:%d: there is nothing called '%s' to drop",
                        path, lineno, what);
                ok = false;
                break;
            }
            if (!as_int(a, &least) || !as_int(z, &most) || !as_int(pc, &chance) ||
                least < 1 || most < least || most > 255 || chance < 1 || chance > 100) {
                complain(path, lineno, "loot numbers out of all reason");
                ok = false;
                break;
            }
            b->loot[b->loots] = (gg_loot){
                .kind = (uint8_t)kind, .least = (uint8_t)least,
                .most = (uint8_t)most, .chance = (uint8_t)chance,
            };
            b->loots++;
        } else {
            SDL_Log("gigantima: %s:%d: no idea what `%s` means", path, lineno, key);
            ok = false;
        }
        if (!ok) break;
    }

    SDL_free(text);

    for (int i = 0; ok && i < g_beasts; i++) {
        if (!g_beast[i].name[0]) {
            SDL_Log("gigantima: %s: the creature %s has no name to be called by",
                    path, g_beast[i].id);
            ok = false;
        }
        // Something that cannot hurt you and cannot be reached is not a
        // creature, it is scenery - and this file is not where scenery goes.
        if (ok && g_beast[i].damage == 0) {
            SDL_Log("gigantima: %s: %s does no damage at all", path, g_beast[i].id);
            ok = false;
        }
    }

    if (!ok) {
        gg_bestiary_clear();
        return false;
    }
    SDL_Log("gigantima: %d kinds of creature", g_beasts);
    return true;
}
