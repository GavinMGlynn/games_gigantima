// gg_magic.c - reading the book of spells.
//
// The format is the dialogue file's, because the two are read by the same kind
// of person for the same kind of reason: lines of `keyword rest-of-line`, hash
// to end of line for comments, indentation ignored.
#include "core/gg_magic.h"

static gg_rune  g_rune[GG_RUNES_MAX];
static int      g_runes;
static gg_spell g_spell[GG_SPELLS_MAX];
static int      g_spells;

void gg_magic_clear(void) {
    SDL_zeroa(g_rune);
    SDL_zeroa(g_spell);
    g_runes = 0;
    g_spells = 0;
}

int gg_magic_spells(void) { return g_spells; }
int gg_magic_runes(void)  { return g_runes; }

const gg_spell *gg_magic_spell(int i) {
    return (i >= 0 && i < g_spells) ? &g_spell[i] : nullptr;
}
const gg_rune *gg_magic_rune(int i) {
    return (i >= 0 && i < g_runes) ? &g_rune[i] : nullptr;
}

const gg_rune *gg_magic_find_rune(const char *word) {
    if (!word || !*word) return nullptr;
    for (int i = 0; i < g_runes; i++)
        if (SDL_strcasecmp(g_rune[i].word, word) == 0) return &g_rune[i];
    return nullptr;
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

// Pulls the next whitespace-separated token out of `*p`, advancing it.
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

static int effect_from(const char *word) {
    if (SDL_strcasecmp(word, "light") == 0) return GG_SPELL_LIGHT;
    if (SDL_strcasecmp(word, "heal")  == 0) return GG_SPELL_HEAL;
    if (SDL_strcasecmp(word, "harm")  == 0) return GG_SPELL_HARM;
    return GG_SPELL_NONE;
}

// An item by the id the item table carries, so a spell file says GINSENG and
// not a number nobody can check. The id rather than the prose name, because
// the prose has spaces in it and this file is split on whitespace.
static int item_from(const char *word) {
    for (int i = 0; i < GG_ITEM_COUNT; i++)
        if (SDL_strcasecmp(GG_ITEM[i].id, word) == 0) return i;
    return -1;
}

static void complain(const char *path, int line, const char *what) {
    SDL_Log("gigantima: %s:%d: %s", path, line, what);
}

bool gg_magic_load(const char *path) {
    gg_magic_clear();

    size_t size = 0;
    char *text = SDL_LoadFile(path, &size);
    if (!text) {
        SDL_Log("gigantima: cannot read %s: %s", path, SDL_GetError());
        return false;
    }

    gg_spell *sp = nullptr;
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

        if (SDL_strcasecmp(key, "rune") == 0) {
            if (g_runes >= GG_RUNES_MAX) {
                complain(path, lineno, "too many runes");
                ok = false;
                break;
            }
            gg_rune *r = &g_rune[g_runes];
            char word[GG_WORD_MAX];
            if (!next_token(&rest, word, sizeof word) || !*rest) {
                complain(path, lineno, "a rune needs a word and a meaning");
                ok = false;
                break;
            }
            SDL_strlcpy(r->word, word, sizeof r->word);
            SDL_strlcpy(r->meaning, rest, sizeof r->meaning);
            g_runes++;
            sp = nullptr;
            continue;
        }

        if (SDL_strcasecmp(key, "spell") == 0) {
            if (g_spells >= GG_SPELLS_MAX) {
                complain(path, lineno, "too many spells");
                ok = false;
                break;
            }
            sp = &g_spell[g_spells++];
            SDL_zerop(sp);

            char word[GG_WORD_MAX];
            while (next_token(&rest, word, sizeof word)) {
                if (sp->runes >= GG_SPELL_RUNES_MAX) {
                    complain(path, lineno, "a phrase longer than a spell may be");
                    ok = false;
                    break;
                }
                if (!gg_magic_find_rune(word)) {
                    SDL_Log("gigantima: %s:%d: '%s' is not a rune. Runes have to "
                            "be declared before the spells that use them.",
                            path, lineno, word);
                    ok = false;
                    break;
                }
                SDL_strlcpy(sp->rune[sp->runes++], word, GG_WORD_MAX);
            }
            if (ok && sp->runes == 0) {
                complain(path, lineno, "a spell with no runes to speak");
                ok = false;
            }
            continue;
        }

        if (!sp) {
            complain(path, lineno, "this line comes before any `spell`");
            ok = false;
            break;
        }

        if (SDL_strcasecmp(key, "name") == 0) {
            SDL_strlcpy(sp->name, rest, sizeof sp->name);
        } else if (SDL_strcasecmp(key, "say") == 0) {
            SDL_strlcpy(sp->say, rest, sizeof sp->say);
        } else if (SDL_strcasecmp(key, "circle") == 0) {
            int v = 0;
            if (!as_int(rest, &v) || v < 1 || v > 8) {
                complain(path, lineno, "a circle is a number from 1 to 8");
                ok = false;
                break;
            }
            sp->circle = (uint8_t)v;
        } else if (SDL_strcasecmp(key, "costs") == 0) {
            char what[GG_WORD_MAX], howmany[GG_WORD_MAX];
            while (next_token(&rest, what, sizeof what)) {
                int n = 1;
                if (next_token(&rest, howmany, sizeof howmany) &&
                    !as_int(howmany, &n)) {
                    complain(path, lineno, "a reagent needs a count after it");
                    ok = false;
                    break;
                }
                const int kind = item_from(what);
                if (kind < 0) {
                    SDL_Log("gigantima: %s:%d: there is no reagent called '%s'",
                            path, lineno, what);
                    ok = false;
                    break;
                }
                if (n < 1 || n > 255) {
                    complain(path, lineno, "a reagent count out of all reason");
                    ok = false;
                    break;
                }
                if (sp->reagents >= GG_SPELL_REAGENTS_MAX) {
                    complain(path, lineno, "more reagents than a spell may want");
                    ok = false;
                    break;
                }
                sp->reagent[sp->reagents] = (uint8_t)kind;
                sp->reagent_count[sp->reagents] = (uint8_t)n;
                sp->reagents++;
            }
        } else if (SDL_strcasecmp(key, "effect") == 0) {
            char what[GG_WORD_MAX], amount[GG_WORD_MAX];
            if (!next_token(&rest, what, sizeof what)) {
                complain(path, lineno, "an effect with no name");
                ok = false;
                break;
            }
            sp->effect = (uint8_t)effect_from(what);
            if (sp->effect == GG_SPELL_NONE) {
                SDL_Log("gigantima: %s:%d: no spell does '%s'", path, lineno, what);
                ok = false;
                break;
            }
            if (next_token(&rest, amount, sizeof amount) &&
                !as_int(amount, &sp->power)) {
                complain(path, lineno, "an effect's strength has to be a number");
                ok = false;
                break;
            }
            // The rest of the line is optional `turns N` and `reach N`.
            char more[GG_WORD_MAX];
            while (ok && next_token(&rest, more, sizeof more)) {
                char value[GG_WORD_MAX];
                int v = 0;
                if (!next_token(&rest, value, sizeof value) || !as_int(value, &v)) {
                    complain(path, lineno, "expected a number");
                    ok = false;
                    break;
                }
                if (SDL_strcasecmp(more, "turns") == 0)      sp->turns = v;
                else if (SDL_strcasecmp(more, "reach") == 0) sp->reach = v;
                else {
                    SDL_Log("gigantima: %s:%d: no idea what '%s' means on an "
                            "effect", path, lineno, more);
                    ok = false;
                }
            }
        } else {
            SDL_Log("gigantima: %s:%d: no idea what `%s` means", path, lineno, key);
            ok = false;
        }
    }

    SDL_free(text);

    // A spell has to have a name, do something, and do a definite amount of it.
    for (int i = 0; ok && i < g_spells; i++) {
        const gg_spell *s = &g_spell[i];
        if (!s->name[0]) {
            SDL_Log("gigantima: %s: a spell with no name", path);
            ok = false;
        } else if (s->effect == GG_SPELL_NONE || s->power <= 0) {
            SDL_Log("gigantima: %s: '%s' does nothing", path, s->name);
            ok = false;
        } else if (s->effect == GG_SPELL_LIGHT && s->turns <= 0) {
            SDL_Log("gigantima: %s: '%s' is a light that lasts no time at all",
                    path, s->name);
            ok = false;
        } else if (s->effect == GG_SPELL_HARM && s->reach <= 0) {
            SDL_Log("gigantima: %s: '%s' strikes at no distance", path, s->name);
            ok = false;
        }
    }

    if (!ok) {
        gg_magic_clear();
        return false;
    }
    SDL_Log("gigantima: %d runes and %d spells", g_runes, g_spells);
    return true;
}
