// gg_quest.c - reading the story.
//
// The dialogue file's format again, for the third time and for the same
// reason: one shape of file to learn.
#include "core/gg_quest.h"

static gg_quest g_quest[GG_QUESTS_MAX];
static int      g_quests;

void gg_quests_clear(void) {
    SDL_zeroa(g_quest);
    g_quests = 0;
}

int gg_quests_count(void) { return g_quests; }

const gg_quest *gg_quest_at(int i) {
    return (i >= 0 && i < g_quests) ? &g_quest[i] : nullptr;
}

int gg_quest_find(const char *id) {
    if (!id || !*id) return -1;
    for (int i = 0; i < g_quests; i++)
        if (SDL_strcasecmp(g_quest[i].id, id) == 0) return i;
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

static int item_from(const char *word) {
    for (int i = 0; i < GG_ITEM_COUNT; i++)
        if (SDL_strcasecmp(GG_ITEM[i].id, word) == 0) return i;
    return -1;
}

static void complain(const char *path, int line, const char *what) {
    SDL_Log("gigantima: %s:%d: %s", path, line, what);
}

bool gg_quests_load(const char *path) {
    gg_quests_clear();

    size_t size = 0;
    char *text = SDL_LoadFile(path, &size);
    if (!text) {
        SDL_Log("gigantima: cannot read %s: %s", path, SDL_GetError());
        return false;
    }

    gg_quest *q = nullptr;
    gg_stage *st = nullptr;
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

        if (SDL_strcasecmp(key, "quest") == 0) {
            if (!*rest) { complain(path, lineno, "a quest with no id"); ok = false; break; }
            if (g_quests >= GG_QUESTS_MAX) {
                complain(path, lineno, "too many quests");
                ok = false;
                break;
            }
            q = &g_quest[g_quests++];
            SDL_zerop(q);
            SDL_strlcpy(q->id, rest, sizeof q->id);
            st = nullptr;
            continue;
        }

        if (!q) {
            complain(path, lineno, "this line comes before any `quest`");
            ok = false;
            break;
        }

        if (SDL_strcasecmp(key, "name") == 0) {
            SDL_strlcpy(q->name, rest, sizeof q->name);
            st = nullptr;
        } else if (SDL_strcasecmp(key, "stage") == 0) {
            if (q->stages >= GG_STAGES_MAX) {
                complain(path, lineno, "more stages than a quest may have");
                ok = false;
                break;
            }
            st = &q->stage[q->stages++];
            SDL_zerop(st);
            st->what = GG_WHEN_ALWAYS;
        } else if (!st) {
            complain(path, lineno, "this line comes before any `stage`");
            ok = false;
            break;
        } else if (SDL_strcasecmp(key, "journal") == 0) {
            SDL_strlcpy(st->journal, rest, sizeof st->journal);
        } else if (SDL_strcasecmp(key, "sets") == 0) {
            SDL_strlcpy(st->sets, rest, sizeof st->sets);
        } else if (SDL_strcasecmp(key, "ends") == 0) {
            st->ends = true;
        } else if (SDL_strcasecmp(key, "when") == 0) {
            char what[GG_FLAG_MAX];
            if (!next_token(&rest, what, sizeof what)) {
                // A bare `when` means "as soon as the stage before it" - the
                // way a stage that only writes a line is written. Documented
                // in assets/quests.txt, so it has to be accepted here.
                st->what = GG_WHEN_ALWAYS;
                continue;
            }
            if (SDL_strcasecmp(what, "knows") == 0 ||
                SDL_strcasecmp(what, "flag") == 0) {
                st->what = (uint8_t)(SDL_strcasecmp(what, "knows") == 0
                                     ? GG_WHEN_KNOWS : GG_WHEN_FLAG);
                if (!next_token(&rest, st->word, sizeof st->word)) {
                    complain(path, lineno, "that condition wants a word");
                    ok = false;
                    break;
                }
            } else if (SDL_strcasecmp(what, "has") == 0) {
                char thing[GG_FLAG_MAX], howmany[16];
                if (!next_token(&rest, thing, sizeof thing) ||
                    !next_token(&rest, howmany, sizeof howmany) ||
                    !as_int(howmany, &st->count) || st->count < 1) {
                    complain(path, lineno, "`has` wants a thing and a count");
                    ok = false;
                    break;
                }
                const int kind = item_from(thing);
                if (kind < 0) {
                    SDL_Log("gigantima: %s:%d: there is nothing called '%s'",
                            path, lineno, thing);
                    ok = false;
                    break;
                }
                st->what = GG_WHEN_HAS;
                st->item = (uint8_t)kind;
            } else if (SDL_strcasecmp(what, "at") == 0) {
                if (!next_token(&rest, st->where, sizeof st->where)) {
                    complain(path, lineno, "`at` wants a map");
                    ok = false;
                    break;
                }
                st->what = GG_WHEN_AT;
                st->wx = st->wy = -1;

                // A tile and how near is near enough, or nothing and the whole
                // map counts.
                char tx[16], ty[16], rad[16];
                if (next_token(&rest, tx, sizeof tx)) {
                    int px = 0, py = 0, r = 0;
                    if (!next_token(&rest, ty, sizeof ty) ||
                        !next_token(&rest, rad, sizeof rad) ||
                        !as_int(tx, &px) || !as_int(ty, &py) || !as_int(rad, &r) ||
                        r < 1) {
                        complain(path, lineno,
                                 "`at` with a tile wants an x, a y and how near");
                        ok = false;
                        break;
                    }
                    st->wx = (int16_t)px;
                    st->wy = (int16_t)py;
                    st->radius = (uint8_t)(r > 255 ? 255 : r);
                }
            } else if (SDL_strcasecmp(what, "slain") == 0 ||
                       SDL_strcasecmp(what, "party") == 0) {
                char howmany[16];
                if (!next_token(&rest, howmany, sizeof howmany) ||
                    !as_int(howmany, &st->count) || st->count < 1) {
                    complain(path, lineno, "that condition wants a count");
                    ok = false;
                    break;
                }
                st->what = (uint8_t)(SDL_strcasecmp(what, "slain") == 0
                                     ? GG_WHEN_SLAIN : GG_WHEN_PARTY);
            } else {
                SDL_Log("gigantima: %s:%d: no condition called '%s'",
                        path, lineno, what);
                ok = false;
            }
        } else {
            SDL_Log("gigantima: %s:%d: no idea what `%s` means", path, lineno, key);
            ok = false;
        }
        if (!ok) break;
    }

    SDL_free(text);

    for (int i = 0; ok && i < g_quests; i++) {
        const gg_quest *check = &g_quest[i];
        if (!check->name[0]) {
            SDL_Log("gigantima: %s: the quest %s has no name", path, check->id);
            ok = false;
        } else if (check->stages == 0) {
            SDL_Log("gigantima: %s: '%s' has no stages", path, check->name);
            ok = false;
        }
        for (int k = 0; ok && k < check->stages; k++) {
            if (!check->stage[k].journal[0]) {
                SDL_Log("gigantima: %s: stage %d of '%s' writes nothing in the "
                        "journal", path, k + 1, check->name);
                ok = false;
            }
            // A first stage with no condition begins the moment a game does,
            // which is a quest nobody was given.
            if (k == 0 && check->stage[k].what == GG_WHEN_ALWAYS) {
                SDL_Log("gigantima: %s: '%s' begins with no condition, so it "
                        "would begin at once", path, check->name);
                ok = false;
            }
        }
    }

    if (!ok) {
        gg_quests_clear();
        return false;
    }
    SDL_Log("gigantima: %d quests", g_quests);
    return true;
}
