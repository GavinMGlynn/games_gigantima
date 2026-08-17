// gg_world.c - map storage, passability, the demo continent, and the map file.
#include "core/gg_world.h"

// Movement cost 0 means "the default", which the turn loop reads as one game
// minute. Rough ground costs more, which is what makes a road worth building
// and a mountain range worth going around.
const gg_terrain_def GG_TERRAIN[GG_TILE_COUNT] = {
    [GG_TILE_GRASS]      = { "grass",      true,  false, 0 },
    [GG_TILE_GRASS_WORN] = { "worn grass", true,  false, 0 },
    [GG_TILE_DIRT]       = { "dirt",       true,  false, 0 },
    [GG_TILE_EARTH_DARK] = { "earth",      true,  false, 0 },
    [GG_TILE_FARMLAND]   = { "farmland",   true,  false, 2 },
    [GG_TILE_ROAD]       = { "road",       true,  false, 0 },
    [GG_TILE_SAND]       = { "sand",       true,  false, 2 },
    [GG_TILE_DESERT]     = { "desert",     true,  false, 2 },
    [GG_TILE_WATER]      = { "water",      false, true,  0 },
    [GG_TILE_WATER_DEEP] = { "deep water", false, true,  0 },
    [GG_TILE_MOUNTAIN]   = { "mountain",   false, false, 0 },
    [GG_TILE_CLIFF]      = { "cliff",      false, false, 0 },
    // Interiors. The floor is walkable; the wall is not, though a building's
    // walls are also flagged GG_CELL_BLOCKED, so this is belt and braces.
    [GG_TILE_FLOOR_WOOD] = { "a wooden floor", true,  false, 0 },
    [GG_TILE_WALL_BRICK] = { "a brick wall",   false, false, 0 },
};

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------
bool gg_map_alloc(gg_map *m, int w, int h) {
    if (w <= 0 || h <= 0) return false;

    SDL_zerop(m);
    // calloc rather than malloc: a zeroed cell is grass with no prop and no
    // flags, which is a valid map, so a partial generator cannot leave
    // uninitialised terrain behind.
    m->cell = SDL_calloc((size_t)w * (size_t)h, sizeof *m->cell);
    if (!m->cell) return false;

    m->w = w;
    m->h = h;
    return true;
}

void gg_map_free(gg_map *m) {
    if (!m) return;
    SDL_free(m->cell);
    m->cell = nullptr;
    m->w = m->h = 0;
}

// ---------------------------------------------------------------------------
// Access
// ---------------------------------------------------------------------------
bool gg_map_walkable(const gg_map *m, int x, int y) {
    const gg_cell *c = gg_map_at_const(m, x, y);
    if (!c) return false;                         // off-map is a wall
    if (c->flags & GG_CELL_BLOCKED) return false;
    if (c->flags & GG_CELL_DOOR) return true;     // a door is a hole in a wall

    if (c->terrain >= GG_TILE_COUNT) return false;
    if (!GG_TERRAIN[c->terrain].passable) return false;

    // A prop blocks its own footprint. Everything except ground cover does:
    // a tree trunk stops you, a lily pad does not.
    if (GG_HAS_PROP(c)) {
        switch (GG_PROP_OF(c)) {
        case GG_PROP_LILYPAD:
            break;
        default:
            return false;
        }
    }
    return true;
}

int gg_map_region_at(const gg_map *m, int x, int y) {
    for (int i = 0; i < m->regions; i++) {
        const gg_region *r = &m->region[i];
        if (x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h)
            return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Placing props
// ---------------------------------------------------------------------------
void gg_prop_footprint(gg_prop_id p, int x, int y,
                       int *x0, int *y0, int *x1, int *y1) {
    const gg_prop_size *s = &GG_PROP_SIZE[p];
    // The anchor is the footprint's bottom centre. For an even-width
    // footprint the extra column goes to the right, matching how the baker
    // rounds when it derives the anchor.
    *x0 = x - (s->foot_w - 1) / 2;
    *x1 = *x0 + s->foot_w - 1;
    *y1 = y;
    *y0 = y - s->foot_h + 1;
}

bool gg_map_prop_fits(const gg_map *m, int x, int y, gg_prop_id p) {
    int x0, y0, x1, y1;
    gg_prop_footprint(p, x, y, &x0, &y0, &x1, &y1);

    for (int cy = y0; cy <= y1; cy++) {
        for (int cx = x0; cx <= x1; cx++) {
            const gg_cell *c = gg_map_at_const(m, cx, cy);
            if (!c) return false;                        // off the map
            if (c->flags & GG_CELL_WATER) return false;  // nothing is built on water
            if (c->flags & GG_CELL_BLOCKED) return false;
            if (GG_HAS_PROP(c)) return false;
        }
    }
    return true;
}

bool gg_map_place_prop(gg_map *m, int x, int y, gg_prop_id p) {
    if (!gg_map_prop_fits(m, x, y, p)) return false;

    const gg_prop_size *s = &GG_PROP_SIZE[p];
    int x0, y0, x1, y1;
    gg_prop_footprint(p, x, y, &x0, &y0, &x1, &y1);

    for (int cy = y0; cy <= y1; cy++) {
        for (int cx = x0; cx <= x1; cx++) {
            gg_cell *c = gg_map_at(m, cx, cy);

            // A hollow prop is a building: its footprint is walls around a
            // room, not a solid block. The room is walkable *always* - only
            // the roof is hidden when you step in, so collision never changes
            // with the camera or with where the player happens to be.
            const bool wall = !s->hollow ||
                              cx == x0 || cx == x1 || cy == y0 || cy == y1;
            if (wall) {
                c->flags |= GG_CELL_BLOCKED;
                if (s->hollow) c->terrain = GG_TILE_WALL_BRICK;
            } else {
                c->flags |= GG_CELL_INDOORS;
                c->terrain = GG_TILE_FLOOR_WOOD;
            }
            c->flags &= (uint8_t)~GG_CELL_WATER;
        }
    }

    // The sprite hangs off the anchor cell alone; the rest of the footprint is
    // blocking with nothing drawn on it, which is what stops one building being
    // drawn several times.
    gg_map_at(m, x, y)->prop = (uint8_t)(p + 1);

    if (s->door_dx != GG_NO_DOOR) {
        gg_cell *d = gg_map_at(m, x0 + s->door_dx, y1);
        if (d) {
            // A doorway is a hole in the wall: passable, and marked so the
            // simulation can tell it apart from open ground.
            d->flags &= (uint8_t)~GG_CELL_BLOCKED;
            d->flags |= GG_CELL_DOOR;
            if (s->hollow) d->terrain = GG_TILE_FLOOR_WOOD;
        }
    }
    return true;
}

bool gg_prop_interior(gg_prop_id p, int x, int y,
                      int *x0, int *y0, int *x1, int *y1) {
    if (!GG_PROP_SIZE[p].hollow) return false;

    // The room is the footprint inset by its walls.
    gg_prop_footprint(p, x, y, x0, y0, x1, y1);
    (*x0)++; (*y0)++; (*x1)--; (*y1)--;
    return *x0 <= *x1 && *y0 <= *y1;
}

bool gg_prop_interior_contains(gg_prop_id p, int ax, int ay, int x, int y) {
    int x0, y0, x1, y1;
    if (!gg_prop_interior(p, ax, ay, &x0, &y0, &x1, &y1)) return false;
    return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

// ---------------------------------------------------------------------------
// Generation
//
// Not a serious world generator - it exists so there is something coherent to
// walk around while the engine is built, and so every subsystem has real data
// to run against. The hand-authored world arrives with the editor; see
// docs/COMPLETION_PLAN.md.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Things on the ground
// ---------------------------------------------------------------------------
int gg_ground_at(const gg_map *m, int x, int y) {
    for (int i = 0; i < m->grounds; i++)
        if (m->ground[i].x == x && m->ground[i].y == y) return i;
    return -1;
}

void gg_ground_remove(gg_map *m, int index) {
    if (index < 0 || index >= m->grounds) return;
    m->ground[index] = m->ground[--m->grounds];
}

bool gg_ground_drop(gg_map *m, int x, int y, gg_item_id kind, int count) {
    if (count <= 0 || !gg_map_in_bounds(m, x, y)) return false;

    // Merge with what is already underfoot when it is the same kind, so a
    // player dropping coins twice does not leave two piles on one tile.
    const int here = gg_ground_at(m, x, y);
    if (here >= 0 && m->ground[here].kind == kind) {
        const int total = m->ground[here].count + count;
        m->ground[here].count = (uint8_t)(total > 255 ? 255 : total);
        return true;
    }
    if (m->grounds >= GG_GROUND_MAX) return false;

    gg_ground_item *g = &m->ground[m->grounds++];
    g->x = (int16_t)x;
    g->y = (int16_t)y;
    g->kind = (uint8_t)kind;
    g->count = (uint8_t)(count > 255 ? 255 : count);
    return true;
}

static void set_terrain(gg_map *m, int x, int y, gg_tile_id t) {
    gg_cell *c = gg_map_at(m, x, y);
    if (!c) return;
    c->terrain = (uint8_t)t;
    if (GG_TERRAIN[t].water) c->flags |= GG_CELL_WATER;
    else                     c->flags &= (uint8_t)~GG_CELL_WATER;
}

static void set_prop(gg_map *m, int x, int y, gg_prop_id p) {
    gg_cell *c = gg_map_at(m, x, y);
    if (c) c->prop = (uint8_t)(p + 1);
}

// A blob of `t` centred on (cx, cy), with a ragged edge so nothing looks
// stamped. Radius is jittered per row rather than per cell, which gives a
// coastline that wanders instead of one that fizzes.
static void blob(gg_map *m, gg_rng *rng, int cx, int cy, int rx, int ry,
                 gg_tile_id t) {
    for (int y = cy - ry; y <= cy + ry; y++) {
        const int dy = y - cy;
        // Half-width of the ellipse at this row, then jittered.
        const int ry2 = ry > 0 ? ry * ry : 1;
        const int span = (int)((double)rx * SDL_sqrt(1.0 - (double)(dy * dy) /
                                                     (double)ry2));
        const int wob = gg_rand_range(rng, -1, 1);
        for (int x = cx - span + wob; x <= cx + span + wob; x++)
            set_terrain(m, x, y, t);
    }
}

// Cellular smoothing of the water body.
//
// The ellipse jitter that keeps a lake from looking stamped also leaves
// single-tile spurs and notches, and every one of those needs a concave corner
// piece the LPC sheets do not carry - so they render as square steps and the
// coast reads as a staircase. Two passes of "join the majority" removes them
// while leaving the overall outline wandering.
//
// Thresholds are asymmetric on purpose: 4 to drown, 6 to dry. Equal thresholds
// oscillate, with the same cells flipping every pass and the lake never
// settling.
static void smooth_region(gg_map *m, int passes, gg_tile_id target,
                          gg_tile_id outside) {
    for (int pass = 0; pass < passes; pass++) {
        // Decide against the previous state, then apply, or a cell's fate
        // would depend on the scan order of its neighbours.
        uint8_t *want = SDL_calloc((size_t)m->w * (size_t)m->h, 1);
        if (!want) return;

        for (int y = 0; y < m->h; y++) {
            for (int x = 0; x < m->w; x++) {
                const gg_cell *c = gg_map_at_const(m, x, y);
                int in = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (!dx && !dy) continue;
                        const gg_cell *n = gg_map_at_const(m, x + dx, y + dy);
                        // Off-map counts as outside, so a region near the edge
                        // is pulled back from it rather than smeared along it.
                        if (n && n->terrain == target) in++;
                    }
                }
                const bool is_in = c->terrain == target;
                want[(size_t)y * (size_t)m->w + (size_t)x] =
                    (uint8_t)(is_in ? (in >= 4) : (in >= 6));
            }
        }

        for (int y = 0; y < m->h; y++) {
            for (int x = 0; x < m->w; x++) {
                const bool in = want[(size_t)y * (size_t)m->w + (size_t)x] != 0;
                gg_cell *c = gg_map_at(m, x, y);
                if (in == (c->terrain == target)) continue;
                set_terrain(m, x, y, in ? target : outside);
                if (in) c->prop = GG_NO_PROP;
            }
        }
        SDL_free(want);
    }
}

static void scatter_forest(gg_map *m, gg_rng *rng, int count) {
    static const gg_prop_id KINDS[] = {
        GG_PROP_TREE_OAK, GG_PROP_TREE_ELM, GG_PROP_TREE_TALL,
        GG_PROP_TREE_PINE, GG_PROP_TREE_FIR,
    };
    // Trees cluster: pick a centre, then drop a handful around it. Uniform
    // scatter reads as noise; clumps read as woodland.
    for (int i = 0; i < count; i++) {
        const int cx = gg_rand_belowi(rng, m->w);
        const int cy = gg_rand_belowi(rng, m->h);
        const int n = gg_rand_range(rng, 3, 12);
        for (int k = 0; k < n; k++) {
            const int x = cx + gg_rand_range(rng, -4, 4);
            const int y = cy + gg_rand_range(rng, -4, 4);
            gg_cell *c = gg_map_at(m, x, y);
            if (!c || GG_HAS_PROP(c)) continue;
            if (c->terrain != GG_TILE_GRASS && c->terrain != GG_TILE_GRASS_WORN)
                continue;
            set_prop(m, x, y, KINDS[gg_rand_below(rng, GG_COUNTOF(KINDS))]);
        }
    }
}

static void scatter_undergrowth(gg_map *m, gg_rng *rng, int count) {
    static const gg_prop_id KINDS[] = {
        GG_PROP_BUSH_ROUND, GG_PROP_BUSH_LEAFY, GG_PROP_BUSH_CONIFER,
        GG_PROP_FERN, GG_PROP_STUMP,
    };
    for (int i = 0; i < count; i++) {
        const int x = gg_rand_belowi(rng, m->w);
        const int y = gg_rand_belowi(rng, m->h);
        gg_cell *c = gg_map_at(m, x, y);
        if (!c || GG_HAS_PROP(c)) continue;
        if (c->terrain != GG_TILE_GRASS && c->terrain != GG_TILE_GRASS_WORN)
            continue;
        set_prop(m, x, y, KINDS[gg_rand_below(rng, GG_COUNTOF(KINDS))]);
    }
}

// Reeds and lilies only where land meets water, which is the cheap trick that
// makes a generated lake look deliberate.
static void dress_shoreline(gg_map *m, gg_rng *rng) {
    for (int y = 1; y < m->h - 1; y++) {
        for (int x = 1; x < m->w - 1; x++) {
            gg_cell *c = gg_map_at(m, x, y);
            if (!c || GG_HAS_PROP(c)) continue;

            const gg_cell *n[4] = {
                gg_map_at_const(m, x, y - 1), gg_map_at_const(m, x, y + 1),
                gg_map_at_const(m, x - 1, y), gg_map_at_const(m, x + 1, y),
            };
            int wet = 0;
            for (int i = 0; i < 4; i++)
                if (n[i] && (n[i]->flags & GG_CELL_WATER)) wet++;

            if (c->flags & GG_CELL_WATER) {
                if (wet < 4 && gg_rand_below(rng, 6) == 0)
                    set_prop(m, x, y, GG_PROP_LILYPAD);
            } else if (wet > 0 && gg_rand_below(rng, 3) == 0) {
                set_prop(m, x, y, gg_rand_below(rng, 2)
                                  ? GG_PROP_REEDS : GG_PROP_CATTAILS);
            }
        }
    }
}

// A road drawn as a drunkard's walk between two points. It lays ROAD over
// whatever it crosses and clears props, so it is also what guarantees the town
// is reachable from the map edge.
static void carve_road(gg_map *m, gg_rng *rng, int x0, int y0, int x1, int y1) {
    int x = x0, y = y0;
    int guard = (m->w + m->h) * 4;              // cannot loop forever
    while ((x != x1 || y != y1) && guard-- > 0) {
        // Two tiles wide, stamped as a 2x2 so the road is that wide whichever
        // way it happens to be running. One tile does not survive contact with
        // the grass overlay: verges bleed in from both sides at once and swallow
        // the road almost entirely, which is the right behaviour for the
        // overlay and the wrong width for a road.
        for (int oy = 0; oy < 2; oy++) {
            for (int ox = 0; ox < 2; ox++) {
                gg_cell *c = gg_map_at(m, x + ox, y + oy);
                // Skip water rather than paving it. Filling the cell in was
                // what put a brown causeway across the middle of the lake - a
                // road stops at the shore until there is a bridge for it.
                if (!c || (c->flags & GG_CELL_BLOCKED) || (c->flags & GG_CELL_WATER))
                    continue;
                c->terrain = GG_TILE_ROAD;
                c->prop = GG_NO_PROP;
            }
        }
        // Step toward the target, favouring the longer axis, with an
        // occasional sidestep so the road bends.
        if (gg_absi(x1 - x) > gg_absi(y1 - y) || (y == y1))
            x += (x1 > x) ? 1 : -1;
        else
            y += (y1 > y) ? 1 : -1;
        if (gg_rand_below(rng, 7) == 0) {
            if (gg_rand_below(rng, 2)) x += gg_rand_range(rng, -1, 1);
            else                       y += gg_rand_range(rng, -1, 1);
            x = gg_clampi(x, 1, m->w - 2);
            y = gg_clampi(y, 1, m->h - 2);
        }
    }
}

// The three buildings the art gives us. Drawn as props, so a roof overhangs
// the rows behind it and the player can walk there - which is what makes a
// three-quarter view read as a town rather than as a floor plan.
//
// Earlier this stamped a rectangle of wall terrain instead, which meant the
// terrain table was carrying masonry it had no business knowing about, and
// every building was a bare box.
static const gg_prop_id HOUSE_KINDS[] = {
    GG_PROP_HOUSE_BRICK_A, GG_PROP_HOUSE_BRICK_B, GG_PROP_HOUSE_PANELED,
};

// A few things in the room, so stepping inside is worth doing. Kept away from
// the column the door is in: furniture there would block the way in, and a
// house you cannot enter is worse than one that is bare.
static void furnish(gg_map *m, gg_rng *rng, gg_prop_id house, int hx, int hy) {
    static const gg_prop_id STUFF[] = {
        GG_PROP_BARREL, GG_PROP_CRATE, GG_PROP_TABLE,
    };

    int rx0, ry0, rx1, ry1;
    if (!gg_prop_interior(house, hx, hy, &rx0, &ry0, &rx1, &ry1)) return;

    int fx0, fy0, fx1, fy1;
    gg_prop_footprint(house, hx, hy, &fx0, &fy0, &fx1, &fy1);
    const int door_col = fx0 + GG_PROP_SIZE[house].door_dx;

    // A lamp first, and always: a room's light now comes from the lamp in it,
    // not from being a room, so a room that failed to get one is genuinely
    // dark. Placed in a corner away from the door, and retried around the
    // room rather than dropped if the first cell is taken.
    bool lit = false;
    for (int y = ry0; y <= ry1 && !lit; y++)
        for (int x = rx1; x >= rx0 && !lit; x--) {
            if (x == door_col) continue;
            lit = gg_map_place_prop(m, x, y, GG_PROP_LAMP);
        }

    const int want = gg_rand_range(rng, 1, 3);
    for (int i = 0; i < want; i++) {
        const int x = gg_rand_range(rng, rx0, rx1);
        const int y = gg_rand_range(rng, ry0, ry1);
        if (x == door_col) continue;
        // Placement refuses if the cell is taken, so a collision just means
        // one fewer item rather than a stack of them.
        gg_map_place_prop(m, x, y, STUFF[gg_rand_belowi(rng, (int)GG_COUNTOF(STUFF))]);
    }

    // Somebody lives here, so there is something of theirs on the floor. Small
    // things, and only sometimes - a house with a bar of silver lying in it
    // every time is a house nobody would ever leave.
    static const struct { uint8_t kind; uint8_t least, most; } LEFT_ABOUT[] = {
        { GG_ITEM_BREAD,  1, 2 },
        { GG_ITEM_APPLE,  1, 3 },
        { GG_ITEM_GOLD,   3, 20 },
        { GG_ITEM_TORCH,  1, 2 },
        { GG_ITEM_POTION, 1, 1 },
    };
    // One or two things per house. Sparser than this and a player can search
    // a whole town and find nothing, which is how the first pass went: six
    // houses, a one-in-three chance each, and a single pile in the map.
    const int things = gg_rand_range(rng, 1, 2);
    for (int i = 0; i < things; i++) {
        const int k = gg_rand_belowi(rng, (int)GG_COUNTOF(LEFT_ABOUT));
        const int x = gg_rand_range(rng, rx0, rx1);
        const int y = gg_rand_range(rng, ry0, ry1);
        if (x != door_col && gg_map_walkable(m, x, y))
            gg_ground_drop(m, x, y, (gg_item_id)LEFT_ABOUT[k].kind,
                           gg_rand_range(rng, LEFT_ABOUT[k].least,
                                         LEFT_ABOUT[k].most));
    }
}

// Windfalls under the trees, and the odd coin dropped on the road. The point is
// that the wilderness is worth looking at, not that it is a larder: an apple
// beside a trunk reads as something that fell, and a coin on a road as
// something somebody lost.
static void scatter_windfall(gg_map *m, gg_rng *rng, int count) {
    for (int i = 0; i < count; i++) {
        const int x = gg_rand_belowi(rng, m->w);
        const int y = gg_rand_belowi(rng, m->h);
        if (!gg_map_walkable(m, x, y)) continue;
        if (gg_ground_at(m, x, y) >= 0) continue;

        const gg_cell *c = gg_map_at_const(m, x, y);
        if (!c || (c->flags & GG_CELL_INDOORS)) continue;

        // A tree next door makes it a windfall; a road makes it lost change.
        bool wooded = false;
        for (int oy = -1; oy <= 1 && !wooded; oy++)
            for (int ox = -1; ox <= 1 && !wooded; ox++) {
                const gg_cell *n = gg_map_at_const(m, x + ox, y + oy);
                if (n && GG_HAS_PROP(n)) {
                    const gg_prop_id p = GG_PROP_OF(n);
                    wooded = (p >= GG_PROP_TREE_OAK && p <= GG_PROP_TREE_BARE);
                }
            }

        if (wooded)
            gg_ground_drop(m, x, y, GG_ITEM_APPLE, gg_rand_range(rng, 1, 2));
        else if (c->terrain == GG_TILE_ROAD)
            gg_ground_drop(m, x, y, GG_ITEM_GOLD, gg_rand_range(rng, 1, 6));
    }
}

// Puts a house down with its anchor at (x, y), and lays a patch of trodden
// ground in front of the door so the entrance reads from a distance. Returns
// false if it would not fit, leaving the map untouched.
static bool build_house(gg_map *m, gg_rng *rng, int x, int y) {
    const gg_prop_id kind = HOUSE_KINDS[gg_rand_belowi(rng, (int)GG_COUNTOF(HOUSE_KINDS))];
    if (!gg_map_place_prop(m, x, y, kind)) return false;

    const gg_prop_size *s = &GG_PROP_SIZE[kind];
    int x0, y0, x1, y1;
    gg_prop_footprint(kind, x, y, &x0, &y0, &x1, &y1);

    if (s->door_dx != GG_NO_DOOR) {
        const int dx = x0 + s->door_dx;
        for (int j = 1; j <= 2; j++) {
            gg_cell *c = gg_map_at(m, dx, y1 + j);
            if (c && !(c->flags & GG_CELL_BLOCKED) && !(c->flags & GG_CELL_WATER))
                c->terrain = GG_TILE_DIRT;
        }
    }

    furnish(m, rng, kind, x, y);
    return true;
}

static void build_town(gg_map *m, gg_rng *rng, int tx, int ty, int tw, int th) {
    // Flatten and pave the town's ground first, so a building never straddles
    // a lake and the streets read as streets.
    for (int y = ty; y < ty + th; y++) {
        for (int x = tx; x < tx + tw; x++) {
            gg_cell *c = gg_map_at(m, x, y);
            if (!c) continue;
            c->prop = GG_NO_PROP;
            c->flags &= (uint8_t)~(GG_CELL_WATER | GG_CELL_BLOCKED);
            // Mostly worn grass with an occasional bare patch. One-in-four was
            // tried and turned the whole town into a checkerboard: at this
            // tile size, scattered ground variation reads as noise long before
            // it reads as wear.
            c->terrain = (gg_rand_below(rng, 12) == 0) ? GG_TILE_DIRT
                                                       : GG_TILE_GRASS_WORN;
        }
    }

    // A ring of houses around a central square, on a loose grid so the town
    // has streets rather than a random pile of boxes. The anchor is the middle
    // of a house's front wall, so the grid step has to clear the widest
    // footprint - six tiles - plus a street.
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 4; i++) {
            if (j == 1 && (i == 1 || i == 2)) continue;   // leave the square
            const int x = tx + 5 + i * 8;
            const int y = ty + 6 + j * 8;
            // Placement can refuse, and a refusal is fine: the three houses
            // have different footprints, so one may not clear its neighbour,
            // and the gap it leaves reads as a town that grew rather than one
            // that was laid out.
            build_house(m, rng, x, y);
        }
    }

    if (m->regions < GG_REGION_MAX) {
        gg_region *r = &m->region[m->regions++];
        SDL_strlcpy(r->name, "Britain", sizeof r->name);
        r->x = tx; r->y = ty; r->w = tw; r->h = th;
        r->kind = GG_REGION_TOWN;
    }
}

bool gg_map_generate(gg_map *m, int w, int h, uint32_t seed) {
    if (!gg_map_alloc(m, w, h)) return false;

    gg_rng rng;
    gg_rng_seed(&rng, seed);
    m->seed = seed;
    SDL_strlcpy(m->name, "The Vale of Gigantima", sizeof m->name);

    for (int i = 0; i < w * h; i++) m->cell[i].terrain = GG_TILE_GRASS;

    // A mountain spine down the east, a desert in the south-west, a lake in
    // the middle north. Enough variety to exercise the renderer and the
    // pathing without pretending to be a real continent.
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (x > w - 8 + (int)gg_rand_below(&rng, 4))
                set_terrain(m, x, y, GG_TILE_MOUNTAIN);
            else if (y > h - 10 && x < 18 + (int)gg_rand_below(&rng, 5))
                set_terrain(m, x, y, GG_TILE_DESERT);
        }
    }

    // Smooth the shallow outline before the deep pool is cut, so the deep
    // water is not re-flooded by a pass that does not know about it; then
    // smooth the deep pool against the shallow it sits in.
    blob(m, &rng, w / 2, h / 4, 11, 7, GG_TILE_WATER);
    smooth_region(m, 2, GG_TILE_WATER, GG_TILE_GRASS);
    blob(m, &rng, w / 2, h / 4, 6, 3, GG_TILE_WATER_DEEP);
    smooth_region(m, 2, GG_TILE_WATER_DEEP, GG_TILE_WATER);
    // A sand rim around the lake, laid before the shoreline dressing so the
    // reeds land on beach rather than on grass.
    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            gg_cell *c = gg_map_at(m, x, y);
            if (!c || (c->flags & GG_CELL_WATER)) continue;
            bool near_water = false;
            for (int dy = -1; dy <= 1 && !near_water; dy++)
                for (int dx = -1; dx <= 1 && !near_water; dx++) {
                    const gg_cell *n = gg_map_at_const(m, x + dx, y + dy);
                    if (n && (n->flags & GG_CELL_WATER)) near_water = true;
                }
            if (near_water) c->terrain = GG_TILE_SAND;
        }
    }

    scatter_forest(m, &rng, w * h / 900);
    scatter_undergrowth(m, &rng, w * h / 220);
    scatter_windfall(m, &rng, w * h / 260);

    const int tw = 34, th = 26;
    const int tx = gg_clampi(w / 2 - tw / 2, 2, w - tw - 2);
    const int ty = gg_clampi(h * 2 / 3, 2, h - th - 2);
    build_town(m, &rng, tx, ty, tw, th);

    // The lake sits due north of the town, so a road run straight up from the
    // gate walks into it. Aim the northern road off to one side of the lake
    // instead; the road still leaves town northward and still reaches the map
    // edge, and it no longer has to be stopped by water half way.
    carve_road(m, &rng, tx + tw / 2, ty, w / 5, 1);
    carve_road(m, &rng, tx + tw / 2, ty + th - 1, w - 10, h - 6);

    dress_shoreline(m, &rng);

    // Start on the town square, which build_town left clear.
    m->start_x = tx + 2 + 8 + 3;
    m->start_y = ty + 2 + 8 + 2;
    if (!gg_map_walkable(m, m->start_x, m->start_y)) {
        // Spiral out until something walkable turns up. A generator that can
        // strand the player is a generator that will, eventually.
        for (int r = 1; r < 40; r++) {
            bool done = false;
            for (int dy = -r; dy <= r && !done; dy++)
                for (int dx = -r; dx <= r && !done; dx++)
                    if (gg_map_walkable(m, m->start_x + dx, m->start_y + dy)) {
                        m->start_x += dx;
                        m->start_y += dy;
                        done = true;
                    }
            if (done) break;
        }
    }
    // A fire on the square. Placed last, because it sits beside the start
    // point and the start point is not settled until the spiral above has
    // finished looking for open ground. It is also the only light outdoors:
    // without it the town is pitch dark at night beyond the avatar's own lamp.
    for (int r = 1; r < 6; r++) {
        if (gg_map_place_prop(m, m->start_x + r, m->start_y + 1, GG_PROP_CAMPFIRE))
            break;
    }
    return true;
}

// ---------------------------------------------------------------------------
// File format
//
// Little-endian, fixed-width, no padding written: the cell array goes out as
// it sits in memory because gg_cell is four bytes of uint8_t and therefore has
// no layout to disagree about across the three supported platforms.
// ---------------------------------------------------------------------------
bool gg_io_w32(SDL_IOStream *io, uint32_t v) {
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    return SDL_WriteIO(io, b, 4) == 4;
}

bool gg_io_r32(SDL_IOStream *io, uint32_t *v) {
    uint8_t b[4];
    if (SDL_ReadIO(io, b, 4) != 4) return false;
    *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
         ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}

bool gg_map_write(const gg_map *m, SDL_IOStream *io) {
    bool ok = SDL_WriteIO(io, GG_MAP_MAGIC, 8) == 8;
    ok = ok && gg_io_w32(io, GG_MAP_VERSION);
    ok = ok && gg_io_w32(io, (uint32_t)m->w);
    ok = ok && gg_io_w32(io, (uint32_t)m->h);
    ok = ok && gg_io_w32(io, m->seed);
    ok = ok && gg_io_w32(io, (uint32_t)m->start_x);
    ok = ok && gg_io_w32(io, (uint32_t)m->start_y);
    ok = ok && SDL_WriteIO(io, m->name, GG_MAP_NAME_MAX) == GG_MAP_NAME_MAX;
    ok = ok && gg_io_w32(io, (uint32_t)m->regions);
    for (int i = 0; ok && i < m->regions; i++) {
        const gg_region *r = &m->region[i];
        ok = ok && SDL_WriteIO(io, r->name, GG_MAP_NAME_MAX) == GG_MAP_NAME_MAX;
        ok = ok && gg_io_w32(io, (uint32_t)r->x) && gg_io_w32(io, (uint32_t)r->y);
        ok = ok && gg_io_w32(io, (uint32_t)r->w) && gg_io_w32(io, (uint32_t)r->h);
        ok = ok && gg_io_w32(io, r->kind);
    }

    const size_t bytes = (size_t)m->w * (size_t)m->h * sizeof *m->cell;
    ok = ok && SDL_WriteIO(io, m->cell, bytes) == bytes;

    // Things lying about, after the cells: a reader that gave up on the grid
    // never reaches them, and the grid is the part worth failing fast on.
    ok = ok && gg_io_w32(io, (uint32_t)m->grounds);
    for (int i = 0; ok && i < m->grounds; i++) {
        const gg_ground_item *it = &m->ground[i];
        ok = gg_io_w32(io, (uint32_t)it->x) && gg_io_w32(io, (uint32_t)it->y) &&
             gg_io_w32(io, it->kind) && gg_io_w32(io, it->count);
    }
    return ok;
}

bool gg_map_save(const gg_map *m, const char *path) {
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) {
        SDL_Log("gigantima: cannot write %s: %s", path, SDL_GetError());
        return false;
    }
    const bool ok = gg_map_write(m, io);
    SDL_CloseIO(io);
    if (!ok) SDL_Log("gigantima: short write on %s", path);
    return ok;
}

bool gg_map_read(gg_map *m, SDL_IOStream *io) {
    // Owns nothing: the caller opened the stream and the caller closes it.
    // That is what lets a save file carry a map inside it and keep reading
    // afterwards.
    char magic[8];
    uint32_t version = 0, w = 0, h = 0, regions = 0, sx = 0, sy = 0, seed = 0;

    if (SDL_ReadIO(io, magic, 8) != 8 || SDL_memcmp(magic, GG_MAP_MAGIC, 8) != 0) {
        SDL_Log("gigantima: not a map - the magic does not match");
        return false;
    }
    if (!gg_io_r32(io, &version) || version != GG_MAP_VERSION) {
        SDL_Log("gigantima: map version %u, this build reads %d",
                version, GG_MAP_VERSION);
        return false;
    }

    bool ok = gg_io_r32(io, &w) && gg_io_r32(io, &h) && gg_io_r32(io, &seed) &&
              gg_io_r32(io, &sx) && gg_io_r32(io, &sy);
    // Bound the dimensions before allocating: this file may not be ours.
    if (!ok || w == 0 || h == 0 || w > 4096 || h > 4096) {
        SDL_Log("gigantima: map has implausible dimensions %ux%u", w, h);
        return false;
    }

    if (!gg_map_alloc(m, (int)w, (int)h)) return false;
    m->seed = seed;
    m->start_x = (int)sx;
    m->start_y = (int)sy;

    ok = SDL_ReadIO(io, m->name, GG_MAP_NAME_MAX) == GG_MAP_NAME_MAX;
    m->name[GG_MAP_NAME_MAX - 1] = '\0';
    ok = ok && gg_io_r32(io, &regions) && regions <= GG_REGION_MAX;
    m->regions = ok ? (int)regions : 0;
    for (int i = 0; ok && i < m->regions; i++) {
        gg_region *r = &m->region[i];
        uint32_t rx = 0, ry = 0, rw = 0, rh = 0, kind = 0;
        ok = SDL_ReadIO(io, r->name, GG_MAP_NAME_MAX) == GG_MAP_NAME_MAX;
        r->name[GG_MAP_NAME_MAX - 1] = '\0';
        ok = ok && gg_io_r32(io, &rx) && gg_io_r32(io, &ry) &&
             gg_io_r32(io, &rw) && gg_io_r32(io, &rh) && gg_io_r32(io, &kind);
        r->x = (int)rx; r->y = (int)ry; r->w = (int)rw; r->h = (int)rh;
        r->kind = (uint8_t)kind;
    }

    const size_t bytes = (size_t)m->w * (size_t)m->h * sizeof *m->cell;
    ok = ok && SDL_ReadIO(io, m->cell, bytes) == bytes;

    uint32_t grounds = 0;
    ok = ok && gg_io_r32(io, &grounds) && grounds <= GG_GROUND_MAX;
    for (uint32_t i = 0; ok && i < grounds; i++) {
        uint32_t gx = 0, gy = 0, kind = 0, count = 0;
        ok = gg_io_r32(io, &gx) && gg_io_r32(io, &gy) &&
             gg_io_r32(io, &kind) && gg_io_r32(io, &count);
        // This file may not be ours. A kind past the table would index off the
        // end of GG_ITEM, and a pile off the map could never be picked up.
        if (ok && (kind >= GG_ITEM_COUNT || count == 0 || count > 255 ||
                   !gg_map_in_bounds(m, (int)(int32_t)gx, (int)(int32_t)gy))) {
            SDL_Log("gigantima: the map holds an item this build cannot place");
            ok = false;
        }
        if (ok) {
            m->ground[i].x = (int16_t)(int32_t)gx;
            m->ground[i].y = (int16_t)(int32_t)gy;
            m->ground[i].kind = (uint8_t)kind;
            m->ground[i].count = (uint8_t)count;
        }
    }
    m->grounds = ok ? (int)grounds : 0;

    if (!ok) {
        SDL_Log("gigantima: the map is truncated");
        gg_map_free(m);
        return false;
    }

    // A hostile or corrupt file must not be able to index off the end of the
    // tile tables, so clamp every id as it comes in rather than trusting it.
    for (int i = 0; i < m->w * m->h; i++) {
        if (m->cell[i].terrain >= GG_TILE_COUNT)
            m->cell[i].terrain = GG_TILE_GRASS;
        if (m->cell[i].prop > GG_PROP_COUNT)
            m->cell[i].prop = GG_NO_PROP;
    }
    return true;
}

bool gg_map_load(gg_map *m, const char *path) {
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io) return false;
    const bool ok = gg_map_read(m, io);
    SDL_CloseIO(io);
    if (!ok) SDL_Log("gigantima: %s could not be read as a map", path);
    return ok;
}
