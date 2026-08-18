// gg_edit.c - the map being edited.
#include "editor/gg_edit.h"
#include "core/gg_maptext.h"

static void say(gg_editor *e, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
    SDL_PRINTF_VARARG_FUNC(2);

static void say(gg_editor *e, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(e->say, sizeof e->say, fmt, ap);
    va_end(ap);
}

// ---------------------------------------------------------------------------
// The document
// ---------------------------------------------------------------------------
static void set_defaults(gg_editor *e) {
    e->tool = GG_TOOL_TERRAIN;
    e->terrain = GG_TILE_GRASS;
    e->prop = GG_PROP_TREE_OAK;
    e->item = GG_ITEM_BREAD;
    e->item_count = 1;
    e->art = GG_ACTOR_MERCHANT;
    e->region_kind = GG_REGION_TOWN;
    e->actor = -1;
    e->sched_slot = 0;
    e->drag = false;
}

bool gg_edit_new(gg_editor *e, int w, int h) {
    gg_edit_close(e);
    SDL_zerop(e);
    set_defaults(e);

    if (!gg_map_alloc(&e->map, w, h)) {
        say(e, "could not make a %dx%d map", w, h);
        return false;
    }
    for (int i = 0; i < w * h; i++) e->map.cell[i].terrain = GG_TILE_GRASS;

    SDL_strlcpy(e->map.name, "Untitled", sizeof e->map.name);
    e->map.start_x = w / 2;
    e->map.start_y = h / 2;
    e->open = true;
    e->dirty = true;
    say(e, "new map, %dx%d", w, h);
    return true;
}

bool gg_edit_load(gg_editor *e, const char *path) {
    gg_map fresh;
    SDL_zero(fresh);
    if (!gg_map_load(&fresh, path)) {
        say(e, "could not read %s", path);
        return false;
    }
    // Only once it has read cleanly: a failed load must leave whatever was
    // being edited exactly as it was.
    gg_edit_close(e);
    SDL_zerop(e);
    set_defaults(e);
    e->map = fresh;
    e->open = true;
    e->dirty = false;
    SDL_strlcpy(e->path, path, sizeof e->path);
    say(e, "read %s, %dx%d", path, e->map.w, e->map.h);
    return true;
}

bool gg_edit_save(gg_editor *e, const char *path) {
    if (!e->open) return false;
    const char *where = (path && *path) ? path : e->path;
    if (!where || !*where) {
        say(e, "no name to save it under");
        return false;
    }
    // Which form is decided by the name: `.map.txt` writes the text a person
    // can read and anything can generate, and everything else writes the
    // binary. One rule, no flag to remember, and "save as text" is "save it
    // under a name ending in .map.txt".
    const size_t n = SDL_strlen(where);
    const bool as_text = n > 8 && SDL_strcasecmp(where + n - 8, ".map.txt") == 0;
    if (!(as_text ? gg_map_write_text(&e->map, where)
                  : gg_map_save(&e->map, where))) {
        say(e, "could not write %s", where);
        return false;
    }
    if (where != e->path) SDL_strlcpy(e->path, where, sizeof e->path);
    e->dirty = false;
    say(e, "wrote %s", e->path);
    return true;
}

void gg_edit_close(gg_editor *e) {
    if (e->open) gg_map_free(&e->map);
    e->open = false;
}

// ---------------------------------------------------------------------------
// Tools and brushes
// ---------------------------------------------------------------------------
static const char *const TOOL_NAME[GG_TOOL_COUNT] = {
    [GG_TOOL_TERRAIN]  = "ground",
    [GG_TOOL_PROP]     = "things",
    [GG_TOOL_ITEM]     = "litter",
    [GG_TOOL_ACTOR]    = "people",
    [GG_TOOL_SCHEDULE] = "their day",
    [GG_TOOL_REGION]   = "regions",
    [GG_TOOL_START]    = "start",
    [GG_TOOL_PORTAL]   = "ways out",
};

const char *gg_tool_name(gg_tool t) {
    return (t < GG_TOOL_COUNT && TOOL_NAME[t]) ? TOOL_NAME[t] : "?";
}

void gg_edit_tool(gg_editor *e, gg_tool t) {
    if (t >= GG_TOOL_COUNT) return;
    e->tool = t;
    e->drag = false;
    say(e, "%s", gg_tool_name(t));
}

static int wrap(int v, int n, int by) {
    if (n <= 0) return 0;
    return ((v + by) % n + n) % n;
}

void gg_edit_brush(gg_editor *e, int by) {
    switch (e->tool) {
    case GG_TOOL_TERRAIN: e->terrain = wrap(e->terrain, GG_TILE_COUNT, by); break;
    case GG_TOOL_PROP:    e->prop = wrap(e->prop, GG_PROP_COUNT, by); break;
    case GG_TOOL_ITEM:    e->item = wrap(e->item, GG_ITEM_COUNT, by); break;
    case GG_TOOL_ACTOR:   e->art = wrap(e->art, GG_ACTOR_COUNT, by); break;
    case GG_TOOL_REGION:
        e->region_kind = wrap(e->region_kind, GG_REGION_CASTLE + 1, by);
        break;
    case GG_TOOL_SCHEDULE:
        // The hour being placed, not a brush: a day has four of them here.
        e->sched_slot = wrap(e->sched_slot, GG_SCHEDULE_MAX, by);
        break;
    default: return;
    }
    say(e, "%s", gg_edit_brush_name(e));
}

static const char *const REGION_KIND_NAME[] = {
    "wilderness", "town", "dungeon", "castle",
};

const char *gg_edit_brush_name(const gg_editor *e) {
    static char buf[GG_EDIT_SAY_MAX];
    switch (e->tool) {
    case GG_TOOL_TERRAIN:
        SDL_snprintf(buf, sizeof buf, "%s", GG_TERRAIN[e->terrain].name);
        break;
    case GG_TOOL_PROP:
        SDL_snprintf(buf, sizeof buf, "prop %d", e->prop);
        break;
    case GG_TOOL_ITEM:
        SDL_snprintf(buf, sizeof buf, "%d %s", e->item_count,
                     GG_ITEM[e->item].short_name);
        break;
    case GG_TOOL_ACTOR:
        SDL_snprintf(buf, sizeof buf, "%s", GG_ACTOR_ID_NAME[e->art]);
        break;
    case GG_TOOL_SCHEDULE:
        SDL_snprintf(buf, sizeof buf, "hour slot %d", e->sched_slot);
        break;
    case GG_TOOL_REGION:
        SDL_snprintf(buf, sizeof buf, "%s", REGION_KIND_NAME[e->region_kind]);
        break;
    case GG_TOOL_START:
        SDL_snprintf(buf, sizeof buf, "%d,%d", e->map.start_x, e->map.start_y);
        break;
    case GG_TOOL_PORTAL:
        SDL_snprintf(buf, sizeof buf, "%s %d,%d",
                     e->portal_to[0] ? e->portal_to : "(nowhere)",
                     e->portal_x, e->portal_y);
        break;
    default:
        buf[0] = '\0';
        break;
    }
    return buf;
}

// ---------------------------------------------------------------------------
// People
// ---------------------------------------------------------------------------
int gg_edit_actor_at(const gg_editor *e, int x, int y) {
    for (int i = 0; i < e->map.actors; i++)
        if (e->map.actor[i].x == x && e->map.actor[i].y == y) return i;
    return -1;
}

void gg_edit_link_to(gg_editor *e, const char *map, int x, int y) {
    SDL_strlcpy(e->portal_to, map ? map : "", sizeof e->portal_to);
    e->portal_x = x;
    e->portal_y = y;
    say(e, "ways out lead to %s at %d,%d",
        e->portal_to[0] ? e->portal_to : "nowhere", x, y);
}

void gg_edit_name_actor(gg_editor *e, const char *name) {
    if (e->actor < 0 || e->actor >= e->map.actors || !name) return;
    SDL_strlcpy(e->map.actor[e->actor].name, name, GG_ACTOR_NAME_MAX);
    e->dirty = true;
}

// ---------------------------------------------------------------------------
// Acting on a tile
// ---------------------------------------------------------------------------
void gg_edit_apply(gg_editor *e, int x, int y) {
    if (!e->open || !gg_map_in_bounds(&e->map, x, y)) return;
    gg_cell *c = gg_map_at(&e->map, x, y);
    if (!c) return;

    switch (e->tool) {
    case GG_TOOL_TERRAIN:
        c->terrain = (uint8_t)e->terrain;
        // Water is a flag as well as a tile, and a cell that says one and not
        // the other walks wrong - so it is set from the table, never by hand.
        if (GG_TERRAIN[e->terrain].water) c->flags |= GG_CELL_WATER;
        else                              c->flags &= (uint8_t)~GG_CELL_WATER;
        break;

    case GG_TOOL_PROP:
        // Through the same placement the generator uses, so a building's walls
        // and its doorway are laid out identically however it got there.
        if (!gg_map_place_prop(&e->map, x, y, (gg_prop_id)e->prop)) {
            say(e, "it will not fit there");
            return;
        }
        break;

    case GG_TOOL_ITEM:
        if (!gg_ground_drop(&e->map, x, y, (gg_item_id)e->item, e->item_count)) {
            say(e, "no room for it there");
            return;
        }
        break;

    case GG_TOOL_ACTOR: {
        const int already = gg_edit_actor_at(e, x, y);
        if (already >= 0) {
            // Clicking somebody selects them rather than stacking another on
            // top, which is what a person expects and what the schedule tool
            // needs anyway.
            e->actor = already;
            e->sched_slot = 0;
            say(e, "%s", e->map.actor[already].name);
            return;
        }
        if (e->map.actors >= GG_MAP_ACTORS_MAX) {
            say(e, "no room for another person");
            return;
        }
        gg_map_actor *a = &e->map.actor[e->map.actors];
        SDL_zerop(a);
        a->x = (int16_t)x;
        a->y = (int16_t)y;
        a->art = (uint8_t)e->art;
        SDL_snprintf(a->name, GG_ACTOR_NAME_MAX, "%s %d",
                     GG_ACTOR_ID_NAME[e->art], e->map.actors + 1);
        e->actor = e->map.actors++;
        e->sched_slot = 0;
        say(e, "%s", a->name);
        break;
    }

    case GG_TOOL_SCHEDULE: {
        if (e->actor < 0 || e->actor >= e->map.actors) {
            say(e, "nobody is chosen - place or click a person first");
            return;
        }
        gg_map_actor *a = &e->map.actor[e->actor];
        const int slot = e->sched_slot;
        if (slot >= GG_SCHEDULE_MAX) return;

        a->sched[slot].x = (int16_t)x;
        a->sched[slot].y = (int16_t)y;
        // Four points around the clock unless the hour was already set, which
        // gives a workable day from four clicks and leaves it editable.
        if (slot >= a->schedn) {
            a->sched[slot].hour = (uint8_t)((slot * 24) / GG_SCHEDULE_MAX);
            a->schedn = (uint8_t)(slot + 1);
        }
        say(e, "%s at %02u:00 goes to %d,%d", a->name, a->sched[slot].hour, x, y);
        e->sched_slot = (slot + 1) % GG_SCHEDULE_MAX;
        break;
    }

    case GG_TOOL_START:
        e->map.start_x = x;
        e->map.start_y = y;
        say(e, "start at %d,%d", x, y);
        break;

    case GG_TOOL_PORTAL: {
        if (!e->portal_to[0]) {
            say(e, "no map for it to lead to - set one first");
            return;
        }
        if (gg_portal_at(&e->map, x, y)) {
            say(e, "there is already a way out there");
            return;
        }
        if (e->map.portals >= GG_PORTALS_MAX) {
            say(e, "no room for another way out");
            return;
        }
        gg_portal *p = &e->map.portal[e->map.portals++];
        SDL_zerop(p);
        p->x = (int16_t)x;
        p->y = (int16_t)y;
        p->to_x = (int16_t)e->portal_x;
        p->to_y = (int16_t)e->portal_y;
        SDL_strlcpy(p->to, e->portal_to, sizeof p->to);
        say(e, "%d,%d leads to %s at %d,%d", x, y, p->to, p->to_x, p->to_y);
        break;
    }

    case GG_TOOL_REGION:
        // Regions are dragged, not clicked. A single click makes a one-tile
        // region, which is at least honest about what happened.
        gg_edit_drag_start(e, x, y);
        gg_edit_drag_end(e, x, y);
        return;

    default:
        return;
    }
    e->dirty = true;
}

void gg_edit_erase(gg_editor *e, int x, int y) {
    if (!e->open || !gg_map_in_bounds(&e->map, x, y)) return;
    gg_cell *c = gg_map_at(&e->map, x, y);
    if (!c) return;

    switch (e->tool) {
    case GG_TOOL_TERRAIN:
        c->terrain = GG_TILE_GRASS;
        c->flags &= (uint8_t)~GG_CELL_WATER;
        break;

    case GG_TOOL_PROP:
        // A prop's footprint is more than the cell it stands on, so rubbing one
        // out has to unblock all of it - otherwise the map keeps invisible
        // walls where a building used to be, which is a bug nobody can see.
        if (GG_HAS_PROP(c)) {
            const gg_prop_id p = GG_PROP_OF(c);
            int x0, y0, x1, y1;
            gg_prop_footprint(p, x, y, &x0, &y0, &x1, &y1);
            for (int fy = y0; fy <= y1; fy++)
                for (int fx = x0; fx <= x1; fx++) {
                    gg_cell *f = gg_map_at(&e->map, fx, fy);
                    if (!f) continue;
                    f->flags &= (uint8_t)~(GG_CELL_BLOCKED | GG_CELL_DOOR |
                                           GG_CELL_INDOORS);
                    if (f->terrain == GG_TILE_WALL_BRICK ||
                        f->terrain == GG_TILE_FLOOR_WOOD)
                        f->terrain = GG_TILE_GRASS;
                }
            c->prop = GG_NO_PROP;
        } else {
            // Not standing on the anchor: find the prop whose footprint covers
            // this cell, so clicking a wall rubs out the building it belongs to.
            for (int i = 0; i < e->map.w * e->map.h; i++) {
                const gg_cell *o = &e->map.cell[i];
                if (!GG_HAS_PROP(o)) continue;
                const int ox = i % e->map.w, oy = i / e->map.w;
                int x0, y0, x1, y1;
                gg_prop_footprint(GG_PROP_OF(o), ox, oy, &x0, &y0, &x1, &y1);
                if (x >= x0 && x <= x1 && y >= y0 && y <= y1) {
                    gg_edit_erase(e, ox, oy);
                    return;
                }
            }
            return;
        }
        break;

    case GG_TOOL_ITEM: {
        const int i = gg_ground_at(&e->map, x, y);
        if (i < 0) return;
        gg_ground_remove(&e->map, i);
        break;
    }

    case GG_TOOL_ACTOR: {
        const int i = gg_edit_actor_at(e, x, y);
        if (i < 0) return;
        e->map.actor[i] = e->map.actor[--e->map.actors];
        // The selection follows the shuffle, or it silently points at somebody
        // else - the same repair the pack's held slot needs.
        if (e->actor == i) e->actor = -1;
        else if (e->actor == e->map.actors) e->actor = i;
        break;
    }

    case GG_TOOL_SCHEDULE: {
        if (e->actor < 0 || e->actor >= e->map.actors) return;
        gg_map_actor *a = &e->map.actor[e->actor];
        if (a->schedn == 0) return;
        a->schedn--;
        e->sched_slot = a->schedn;
        say(e, "%s has %u hours left", a->name, a->schedn);
        break;
    }

    case GG_TOOL_PORTAL: {
        for (int i = 0; i < e->map.portals; i++)
            if (e->map.portal[i].x == x && e->map.portal[i].y == y) {
                e->map.portal[i] = e->map.portal[--e->map.portals];
                say(e, "the way out is closed");
                e->dirty = true;
                return;
            }
        return;
    }

    case GG_TOOL_REGION: {
        for (int i = 0; i < e->map.regions; i++) {
            const gg_region *r = &e->map.region[i];
            if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h) {
                e->map.region[i] = e->map.region[--e->map.regions];
                say(e, "region gone");
                e->dirty = true;
                return;
            }
        }
        return;
    }

    default:
        return;
    }
    e->dirty = true;
}

void gg_edit_drag_start(gg_editor *e, int x, int y) {
    if (e->tool != GG_TOOL_REGION) return;
    e->drag = true;
    e->drag_x = x;
    e->drag_y = y;
}

void gg_edit_drag_end(gg_editor *e, int x, int y) {
    if (!e->drag || e->tool != GG_TOOL_REGION) return;
    e->drag = false;

    if (e->map.regions >= GG_REGION_MAX) {
        say(e, "no room for another region");
        return;
    }
    const int x0 = e->drag_x < x ? e->drag_x : x;
    const int y0 = e->drag_y < y ? e->drag_y : y;
    const int x1 = e->drag_x > x ? e->drag_x : x;
    const int y1 = e->drag_y > y ? e->drag_y : y;

    gg_region *r = &e->map.region[e->map.regions];
    SDL_zerop(r);
    r->x = x0;
    r->y = y0;
    r->w = x1 - x0 + 1;
    r->h = y1 - y0 + 1;
    r->kind = (uint8_t)e->region_kind;
    SDL_snprintf(r->name, GG_MAP_NAME_MAX, "%s %d",
                 REGION_KIND_NAME[e->region_kind], e->map.regions + 1);
    e->map.regions++;
    e->dirty = true;
    say(e, "%s, %dx%d", r->name, r->w, r->h);
}

// ---------------------------------------------------------------------------
// Checking
// ---------------------------------------------------------------------------
int gg_edit_check(const gg_editor *e,
                  char out[GG_EDIT_PROBLEMS_MAX][GG_EDIT_SAY_MAX]) {
    int n = 0;
    #define PROBLEM(...) do { \
        if (n < GG_EDIT_PROBLEMS_MAX) SDL_snprintf(out[n], GG_EDIT_SAY_MAX, __VA_ARGS__); \
        n++; \
    } while (0)

    if (!e->open) { PROBLEM("no map is open"); return n; }

    // The one that makes a map unplayable rather than merely odd.
    if (!gg_map_walkable(&e->map, e->map.start_x, e->map.start_y))
        PROBLEM("the start at %d,%d is inside something",
                e->map.start_x, e->map.start_y);

    if (!e->map.name[0]) PROBLEM("the map has no name");

    for (int i = 0; i < e->map.actors; i++) {
        const gg_map_actor *a = &e->map.actor[i];
        if (!a->name[0]) PROBLEM("somebody at %d,%d has no name", a->x, a->y);
        if (!gg_map_walkable(&e->map, a->x, a->y))
            PROBLEM("%s at %d,%d is standing inside something",
                    a->name[0] ? a->name : "somebody", a->x, a->y);
        // A schedule point nobody can reach has an NPC shove at a wall all day,
        // which is the failure the generator learned to walk its own points out
        // of - and the editor is where it should be caught instead.
        for (int k = 0; k < a->schedn; k++)
            if (!gg_map_walkable(&e->map, a->sched[k].x, a->sched[k].y))
                PROBLEM("%s is sent to %d,%d at %02u:00, which is solid",
                        a->name[0] ? a->name : "somebody",
                        a->sched[k].x, a->sched[k].y, a->sched[k].hour);
    }

    for (int i = 0; i < e->map.grounds; i++) {
        const gg_ground_item *g = &e->map.ground[i];
        if (!gg_map_walkable(&e->map, g->x, g->y))
            PROBLEM("something at %d,%d cannot be reached", g->x, g->y);
    }

    for (int i = 0; i < e->map.portals; i++) {
        const gg_portal *p = &e->map.portal[i];
        if (!p->to[0])
            PROBLEM("the way out at %d,%d leads nowhere", p->x, p->y);
        // A way out you cannot walk onto is a way out nobody can take.
        if (!gg_map_walkable(&e->map, p->x, p->y))
            PROBLEM("the way out at %d,%d is inside something", p->x, p->y);
    }

    #undef PROBLEM
    return n;
}
