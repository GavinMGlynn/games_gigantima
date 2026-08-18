// gg_maptext.c - reading and writing a map as text.
//
// The same line-based shape as every other content file here: `keyword
// rest-of-line`, `#` comments, blank lines ignored, and a complaint that names
// the file and the line. See gg_maptext.h for the format and for why it exists.
#include "core/gg_maptext.h"

// ---------------------------------------------------------------------------
// The legend
//
// A character stands for a (terrain, flags) pair. Which pairs exist is a
// property of the map being written, not of the format - so the legend is
// collected from the map and written into the file, and the reader learns it
// from there. A fixed alphabet would have to guess, and guessing wrong is a
// map that does not survive the trip.
// ---------------------------------------------------------------------------
#define LEGEND_MAX 64

// Characters that read clearly at a glance and cannot be confused with the
// format's own punctuation. Order matters only in that the commonest ground in
// a map gets the quietest character.
static const char LEGEND_CHARS[] =
    ".,:;'`\"~-_=+*^%$&@#/\\|()[]{}<>!?0123456789"
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

typedef struct {
    char    ch;
    uint8_t terrain;
    uint8_t flags;
} gg_legend_entry;

typedef struct {
    gg_legend_entry entry[LEGEND_MAX];
    int             n;
} gg_legend;

static int legend_find(const gg_legend *l, uint8_t terrain, uint8_t flags) {
    for (int i = 0; i < l->n; i++)
        if (l->entry[i].terrain == terrain && l->entry[i].flags == flags)
            return i;
    return -1;
}

static int legend_of_char(const gg_legend *l, char ch) {
    for (int i = 0; i < l->n; i++)
        if (l->entry[i].ch == ch) return i;
    return -1;
}

// Every combination the map actually uses, commonest first - so the character
// that costs the least ink is the ground most of the map is made of.
static bool legend_build(gg_legend *l, const gg_map *m) {
    SDL_zerop(l);

    int count[LEGEND_MAX] = { 0 };
    for (int i = 0; i < m->w * m->h; i++) {
        const gg_cell *c = &m->cell[i];
        int at = legend_find(l, c->terrain, c->flags);
        if (at < 0) {
            if (l->n >= LEGEND_MAX) {
                SDL_Log("gigantima: this map uses more than %d kinds of ground",
                        LEGEND_MAX);
                return false;
            }
            at = l->n++;
            l->entry[at].terrain = c->terrain;
            l->entry[at].flags = c->flags;
        }
        count[at]++;
    }

    // Sorted by how much of the map each covers, then given characters.
    for (int i = 1; i < l->n; i++)
        for (int k = i; k > 0 && count[k] > count[k - 1]; k--) {
            const gg_legend_entry tmp = l->entry[k];
            l->entry[k] = l->entry[k - 1];
            l->entry[k - 1] = tmp;
            const int c = count[k];
            count[k] = count[k - 1];
            count[k - 1] = c;
        }
    for (int i = 0; i < l->n; i++) l->entry[i].ch = LEGEND_CHARS[i];
    return true;
}

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------
static const char *const FLAG_NAME[] = { "BLOCKED", "DOOR", "INDOORS", "WATER" };

static const char *const REGION_KIND[] = {
    "WILD", "TOWN", "DUNGEON", "CASTLE",
};

// One character per region, for the picture of which place each tile is in.
// Thirty-two of them, which is GG_REGION_MAX, and none of them `.` - that is
// what "nowhere" is written as.
static const char REGION_CHAR[] = "0123456789ABCDEFGHIJKLMNOPQRSTUV";

// Does every cell belong to exactly the region whose box covers it, with the
// later box winning? Then the boxes say everything and the picture is not
// written. This is `gg_map_regions_stamp` asked as a question.
static bool regions_are_boxes(const gg_map *m) {
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++) {
            int want = 0;
            for (int i = 0; i < m->regions; i++) {
                const gg_region *r = &m->region[i];
                if (x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h)
                    want = i + 1;
            }
            if (gg_map_at_const(m, x, y)->region != (uint8_t)want) return false;
        }
    return true;
}

static int name_index(const char *const *table, int n, const char *word) {
    for (int i = 0; i < n; i++)
        if (table[i] && SDL_strcasecmp(table[i], word) == 0) return i;
    return -1;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------
typedef struct {
    SDL_IOStream *io;
    bool ok;
} gg_writer;

static void put(gg_writer *w, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
    SDL_PRINTF_VARARG_FUNC(2);

static void put(gg_writer *w, const char *fmt, ...) {
    if (!w->ok) return;
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    const size_t n = SDL_strlen(line);
    if (SDL_WriteIO(w->io, line, n) != n) w->ok = false;
}

bool gg_map_write_text(const gg_map *m, const char *path) {
    if (!m || !m->cell) return false;

    gg_legend legend;
    if (!legend_build(&legend, m)) return false;

    gg_writer w = { .io = SDL_IOFromFile(path, "wb"), .ok = true };
    if (!w.io) {
        SDL_Log("gigantima: cannot write %s: %s", path, SDL_GetError());
        return false;
    }

    put(&w, "# %s, as text.\n", m->name[0] ? m->name : "a map");
    put(&w, "#\n"
           "# One character to a tile; the legend below says what each one is.\n"
           "# Everything else stands on the ground and is listed after it.\n"
           "# Written by the editor, and readable by anything - see\n"
           "# src/core/gg_maptext.h.\n\n");

    put(&w, "map %d %d\n", m->w, m->h);
    if (m->name[0]) put(&w, "name %s\n", m->name);
    put(&w, "seed %u\n", m->seed);
    put(&w, "start %d %d\n\n", m->start_x, m->start_y);

    for (int i = 0; i < legend.n; i++) {
        const gg_legend_entry *e = &legend.entry[i];
        put(&w, "legend %c %s", e->ch,
            e->terrain < GG_TILE_COUNT ? GG_TILE_ID_NAME[e->terrain] : "GRASS");
        for (int b = 0; b < 4; b++)
            if (e->flags & (1u << b)) put(&w, " %s", FLAG_NAME[b]);
        put(&w, "\n");
    }
    put(&w, "\n");

    // The ground itself.
    for (int y = 0; y < m->h; y++) {
        char row[1024];
        int n = 0;
        for (int x = 0; x < m->w && n < (int)sizeof row - 1; x++) {
            const gg_cell *c = gg_map_at_const(m, x, y);
            const int at = legend_find(&legend, c->terrain, c->flags);
            row[n++] = at >= 0 ? legend.entry[at].ch : '?';
        }
        row[n] = '\0';
        put(&w, "row %s\n", row);
    }
    put(&w, "\n");

    // And everything standing on it. Props are written as the cell that holds
    // them rather than as a thing to place: placing recomputes a footprint, and
    // a map that came back with different walls would not be the same map.
    for (int i = 0; i < m->regions; i++) {
        const gg_region *r = &m->region[i];
        // The name last, because a name has spaces in it - "The Standing
        // Stones" - and anything after it on the line would be eaten by them.
        // Every line in this format that carries free text carries it last.
        put(&w, "region %s %d %d %d %d %s\n",
            REGION_KIND[r->kind < 4 ? r->kind : 0], r->x, r->y, r->w, r->h,
            r->name[0] ? r->name : "-");
    }
    if (m->regions) put(&w, "\n");

    // And, only when it says something the boxes do not, a picture of which
    // place each cell belongs to.
    //
    // A place is a *shape*: the box is where it roughly is, the cells are what
    // it actually covers, and a town carved around a lake is not a rectangle.
    // Written only when the two disagree, so a map whose places are plain boxes
    // stays as short as it was - a second full-size picture in every file, to
    // say what the line above it already said, is noise.
    if (m->regions > 0 && !regions_are_boxes(m)) {
        put(&w, "# which place each tile belongs to: . is nowhere, and the\n"
                "# rest index the regions above in the order they are listed.\n");
        for (int y = 0; y < m->h; y++) {
            char row[1024];
            int n = 0;
            for (int x = 0; x < m->w && n < (int)sizeof row - 1; x++) {
                const int r = gg_map_region_at(m, x, y);
                row[n++] = r < 0 ? '.' : REGION_CHAR[r];
            }
            row[n] = '\0';
            put(&w, "rrow %s\n", row);
        }
        put(&w, "\n");
    }

    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++) {
            const gg_cell *c = gg_map_at_const(m, x, y);
            if (!GG_HAS_PROP(c)) continue;
            const int p = (int)GG_PROP_OF(c);
            put(&w, "prop %s %d %d\n",
                p < GG_PROP_COUNT ? GG_PROP_ID_NAME[p] : "?", x, y);
        }

    for (int i = 0; i < m->grounds; i++) {
        const gg_ground_item *it = &m->ground[i];
        put(&w, "item %s %u %d %d\n",
            it->kind < GG_ITEM_COUNT ? GG_ITEM[it->kind].id : "?",
            it->count, it->x, it->y);
    }

    for (int i = 0; i < m->actors; i++) {
        const gg_map_actor *a = &m->actor[i];
        put(&w, "person %s %d %d %s\n",
            a->art < GG_ACTOR_COUNT ? GG_ACTOR_ID_NAME[a->art] : "AVATAR",
            a->x, a->y, a->name[0] ? a->name : "-");
        for (int k = 0; k < a->schedn && k < GG_SCHEDULE_MAX; k++)
            put(&w, "  at %02u %d %d\n", a->sched[k].hour, a->sched[k].x,
                a->sched[k].y);
    }

    for (int i = 0; i < m->portals; i++) {
        const gg_portal *p = &m->portal[i];
        put(&w, "portal %d %d %d %d %s\n", p->x, p->y, p->to_x, p->to_y, p->to);
    }

    SDL_CloseIO(w.io);
    if (!w.ok) SDL_Log("gigantima: short write on %s", path);
    return w.ok;
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
    // A comment is a line that *starts* with `#`, and nothing else is. The
    // other content files here strip a `#` anywhere, but `#` is the natural
    // character for a wall and this format is mostly a picture made of them -
    // `row ####...` would be read as a blank line, and `legend # MOUNTAIN` as
    // a legend with nothing in it. Both of which it was.
    if (*p == '#') return nullptr;
    if (!*p) return nullptr;

    char *q = p;
    while (*q && *q != ' ' && *q != '\t') q++;
    if (*q) { *q = '\0'; *rest = skip_spaces(q + 1); }
    else    { *rest = q; }
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

static bool ints(char **rest, int *out, int n) {
    char tok[32];
    for (int i = 0; i < n; i++)
        if (!next_token(rest, tok, sizeof tok) || !as_int(tok, &out[i]))
            return false;
    return true;
}

static void complain(const char *path, int line, const char *what) {
    SDL_Log("gigantima: %s:%d: %s", path, line, what);
}

bool gg_map_is_text(const char *path) {
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io) return false;
    char magic[8] = { 0 };
    const bool got = SDL_ReadIO(io, magic, sizeof magic) == sizeof magic;
    SDL_CloseIO(io);
    // Anything that is not the binary magic is treated as text, and the text
    // reader will say what is wrong with it if it is neither.
    return !got || SDL_memcmp(magic, GG_MAP_MAGIC, 8) != 0;
}

bool gg_map_read_text(gg_map *m, const char *path) {
    size_t size = 0;
    char *text = SDL_LoadFile(path, &size);
    if (!text) {
        SDL_Log("gigantima: cannot read %s: %s", path, SDL_GetError());
        return false;
    }

    gg_map out;
    SDL_zero(out);
    gg_legend legend;
    SDL_zero(legend);

    bool ok = true;
    int lineno = 0, row = 0, rrow = 0;
    bool placed = false;                // the map carried a picture of its places
    char *cursor = text;

    while (ok && cursor && *cursor) {
        char *nl = SDL_strchr(cursor, '\n');
        if (nl) *nl = '\0';
        char *line = cursor;
        cursor = nl ? nl + 1 : nullptr;
        lineno++;

        // A line ending is "\n" or "\r\n" - see the note in the other parsers.
        for (size_t n = SDL_strlen(line);
             n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n'); n--)
            line[n - 1] = '\0';

        char *rest = nullptr;
        char *key = split_word(line, &rest);
        if (!key) continue;

        if (SDL_strcasecmp(key, "map") == 0) {
            int wh[2];
            if (!ints(&rest, wh, 2) || wh[0] < 1 || wh[1] < 1 ||
                wh[0] > 4096 || wh[1] > 4096) {
                complain(path, lineno, "`map` wants a width and a height");
                ok = false;
                break;
            }
            if (out.cell) {
                complain(path, lineno, "a second `map` line");
                ok = false;
                break;
            }
            ok = gg_map_alloc(&out, wh[0], wh[1]);
            if (!ok) break;
        } else if (SDL_strcasecmp(key, "name") == 0) {
            SDL_strlcpy(out.name, rest, sizeof out.name);
        } else if (SDL_strcasecmp(key, "seed") == 0) {
            int v = 0;
            ok = as_int(rest, &v);
            if (!ok) { complain(path, lineno, "a seed that is not a number"); break; }
            out.seed = (uint32_t)v;
        } else if (SDL_strcasecmp(key, "start") == 0) {
            int xy[2];
            if (!ints(&rest, xy, 2)) {
                complain(path, lineno, "`start` wants a tile");
                ok = false;
                break;
            }
            out.start_x = xy[0];
            out.start_y = xy[1];
        } else if (SDL_strcasecmp(key, "legend") == 0) {
            char ch[8], what[32];
            if (!next_token(&rest, ch, sizeof ch) || SDL_strlen(ch) != 1 ||
                !next_token(&rest, what, sizeof what)) {
                complain(path, lineno, "`legend` wants a character and a ground");
                ok = false;
                break;
            }
            if (legend.n >= LEGEND_MAX) {
                complain(path, lineno, "more kinds of ground than a map may have");
                ok = false;
                break;
            }
            const int t = name_index(GG_TILE_ID_NAME, GG_TILE_COUNT, what);
            if (t < 0) {
                SDL_Log("gigantima: %s:%d: there is no ground called '%s'",
                        path, lineno, what);
                ok = false;
                break;
            }
            gg_legend_entry *e = &legend.entry[legend.n++];
            e->ch = ch[0];
            e->terrain = (uint8_t)t;
            e->flags = 0;

            char flag[32];
            while (ok && next_token(&rest, flag, sizeof flag)) {
                const int b = name_index(FLAG_NAME, 4, flag);
                if (b < 0) {
                    SDL_Log("gigantima: %s:%d: there is no flag called '%s'",
                            path, lineno, flag);
                    ok = false;
                    break;
                }
                e->flags |= (uint8_t)(1u << b);
            }
        } else if (SDL_strcasecmp(key, "row") == 0) {
            if (!out.cell) {
                complain(path, lineno, "a row before the `map` line");
                ok = false;
                break;
            }
            if (row >= out.h) {
                complain(path, lineno, "more rows than the map is tall");
                ok = false;
                break;
            }
            const int len = (int)SDL_strlen(rest);
            if (len != out.w) {
                SDL_Log("gigantima: %s:%d: this row is %d wide and the map is %d",
                        path, lineno, len, out.w);
                ok = false;
                break;
            }
            for (int x = 0; x < out.w; x++) {
                const int at = legend_of_char(&legend, rest[x]);
                if (at < 0) {
                    SDL_Log("gigantima: %s:%d: nothing in the legend is '%c'",
                            path, lineno, rest[x]);
                    ok = false;
                    break;
                }
                gg_cell *c = gg_map_at(&out, x, row);
                c->terrain = legend.entry[at].terrain;
                c->flags = legend.entry[at].flags;
            }
            row++;
        } else if (SDL_strcasecmp(key, "rrow") == 0) {
            // The picture of which place each tile is in, when a map has one.
            // It comes after the regions it indexes, so they are all known by
            // the time a character has to be turned into one.
            if (!out.cell) {
                complain(path, lineno, "a place row before the `map` line");
                ok = false;
                break;
            }
            if (rrow >= out.h) {
                complain(path, lineno, "more place rows than the map is tall");
                ok = false;
                break;
            }
            if ((int)SDL_strlen(rest) != out.w) {
                SDL_Log("gigantima: %s:%d: this place row is %d wide and the "
                        "map is %d", path, lineno, (int)SDL_strlen(rest), out.w);
                ok = false;
                break;
            }
            for (int x = 0; x < out.w; x++) {
                if (rest[x] == '.') {
                    gg_map_at(&out, x, rrow)->region = 0;
                    continue;
                }
                const char *at = SDL_strchr(REGION_CHAR, rest[x]);
                const int which = at ? (int)(at - REGION_CHAR) : -1;
                if (which < 0 || which >= out.regions) {
                    SDL_Log("gigantima: %s:%d: '%c' is not one of this map's "
                            "%d places", path, lineno, rest[x], out.regions);
                    ok = false;
                    break;
                }
                gg_map_at(&out, x, rrow)->region = (uint8_t)(which + 1);
            }
            rrow++;
            placed = true;
        } else if (SDL_strcasecmp(key, "region") == 0) {
            char kind[32];
            int box[4];
            if (!next_token(&rest, kind, sizeof kind) || !ints(&rest, box, 4)) {
                complain(path, lineno,
                         "`region` wants a kind, a box and a name");
                ok = false;
                break;
            }
            const char *name = *rest ? rest : "-";
            const int k = name_index(REGION_KIND, 4, kind);
            if (k < 0 || out.regions >= GG_REGION_MAX) {
                complain(path, lineno, "no such kind of region, or too many");
                ok = false;
                break;
            }
            gg_region *r = &out.region[out.regions++];
            SDL_strlcpy(r->name, SDL_strcmp(name, "-") == 0 ? "" : name,
                        sizeof r->name);
            r->kind = (uint8_t)k;
            r->x = box[0]; r->y = box[1]; r->w = box[2]; r->h = box[3];
        } else if (SDL_strcasecmp(key, "prop") == 0) {
            char what[64];
            int xy[2];
            if (!next_token(&rest, what, sizeof what) || !ints(&rest, xy, 2)) {
                complain(path, lineno, "`prop` wants a thing and a tile");
                ok = false;
                break;
            }
            const int p = name_index(GG_PROP_ID_NAME, GG_PROP_COUNT, what);
            gg_cell *c = gg_map_at(&out, xy[0], xy[1]);
            if (p < 0 || !c) {
                SDL_Log("gigantima: %s:%d: no prop called '%s', or it is off "
                        "the map", path, lineno, what);
                ok = false;
                break;
            }
            c->prop = (uint8_t)(p + 1);
        } else if (SDL_strcasecmp(key, "item") == 0) {
            char what[32];
            int rest3[3];
            if (!next_token(&rest, what, sizeof what) || !ints(&rest, rest3, 3)) {
                complain(path, lineno, "`item` wants a thing, a count and a tile");
                ok = false;
                break;
            }
            int kind = -1;
            for (int i = 0; i < GG_ITEM_COUNT; i++)
                if (SDL_strcasecmp(GG_ITEM[i].id, what) == 0) kind = i;
            if (kind < 0 || rest3[0] < 1) {
                SDL_Log("gigantima: %s:%d: there is nothing called '%s'",
                        path, lineno, what);
                ok = false;
                break;
            }
            ok = gg_ground_drop(&out, rest3[1], rest3[2], (gg_item_id)kind,
                                rest3[0]);
            if (!ok) { complain(path, lineno, "no room for another pile"); break; }
        } else if (SDL_strcasecmp(key, "person") == 0) {
            char art[32];
            int xy[2];
            if (!next_token(&rest, art, sizeof art) || !ints(&rest, xy, 2)) {
                complain(path, lineno,
                         "`person` wants a sprite, a tile and a name");
                ok = false;
                break;
            }
            const char *name = *rest ? rest : "-";
            const int a = name_index(GG_ACTOR_ID_NAME, GG_ACTOR_COUNT, art);
            if (a < 0 || out.actors >= GG_MAP_ACTORS_MAX) {
                SDL_Log("gigantima: %s:%d: no sprite called '%s', or too many "
                        "people", path, lineno, art);
                ok = false;
                break;
            }
            gg_map_actor *who = &out.actor[out.actors++];
            SDL_zerop(who);
            SDL_strlcpy(who->name, SDL_strcmp(name, "-") == 0 ? "" : name,
                        sizeof who->name);
            who->art = (uint8_t)a;
            who->x = (int16_t)xy[0];
            who->y = (int16_t)xy[1];
        } else if (SDL_strcasecmp(key, "at") == 0) {
            if (out.actors == 0) {
                complain(path, lineno, "an hour before any `person`");
                ok = false;
                break;
            }
            gg_map_actor *who = &out.actor[out.actors - 1];
            int hxy[3];
            if (!ints(&rest, hxy, 3) || hxy[0] < 0 || hxy[0] > 23 ||
                who->schedn >= GG_SCHEDULE_MAX) {
                complain(path, lineno, "`at` wants an hour and a tile");
                ok = false;
                break;
            }
            who->sched[who->schedn].hour = (uint8_t)hxy[0];
            who->sched[who->schedn].x = (int16_t)hxy[1];
            who->sched[who->schedn].y = (int16_t)hxy[2];
            who->schedn++;
        } else if (SDL_strcasecmp(key, "portal") == 0) {
            int xy[2], txy[2];
            if (!ints(&rest, xy, 2) || !ints(&rest, txy, 2) || !*rest ||
                out.portals >= GG_PORTALS_MAX) {
                complain(path, lineno,
                         "`portal` wants a tile, a tile in the other map, and "
                         "which map");
                ok = false;
                break;
            }
            const char *to = rest;
            gg_portal *p = &out.portal[out.portals++];
            p->x = (int16_t)xy[0];
            p->y = (int16_t)xy[1];
            SDL_strlcpy(p->to, to, sizeof p->to);
            p->to_x = (int16_t)txy[0];
            p->to_y = (int16_t)txy[1];
        } else {
            SDL_Log("gigantima: %s:%d: no idea what `%s` means", path, lineno, key);
            ok = false;
        }
    }

    SDL_free(text);

    if (ok && !out.cell) {
        SDL_Log("gigantima: %s says how big nothing is - there is no `map` line",
                path);
        ok = false;
    }
    if (ok && row != out.h) {
        SDL_Log("gigantima: %s has %d rows and says it is %d tall", path, row,
                out.h);
        ok = false;
    }
    if (ok && placed && rrow != out.h) {
        SDL_Log("gigantima: %s has %d place rows and says it is %d tall", path,
                rrow, out.h);
        ok = false;
    }

    if (!ok) {
        gg_map_free(&out);
        return false;
    }

    // A map with no picture of its places has plain boxes, so they are stamped
    // from them. One with a picture said what it meant and is left alone.
    if (!placed) gg_map_regions_stamp(&out);

    gg_map_free(m);
    *m = out;
    return true;
}
