// gg_world.h - the tile map: storage, passability, generation and the file format.
//
// Gigantima is single-scale in the Ultima VI sense: a town is not a separate
// map you enter, it is a cluster of walls and doors standing in the same grid
// as the wilderness around it, at the same scale. So there is exactly one map
// class, and "town", "dungeon" and "wilderness" are descriptions of a region
// of it rather than different kinds of thing.
#ifndef GG_WORLD_H
#define GG_WORLD_H

#include "core/gg_common.h"
#include "core/gg_ids.h"

// A cell's prop, biased by one so that zero means "no prop" and the enum's
// first real value is not stolen for it.
#define GG_NO_PROP 0
#define GG_PROP_OF(cell) ((gg_prop_id)((cell)->prop - 1))
#define GG_HAS_PROP(cell) ((cell)->prop != GG_NO_PROP)

enum {
    GG_CELL_BLOCKED = 1u << 0,  // impassable regardless of terrain
    GG_CELL_DOOR    = 1u << 1,  // passable, but blocks sight while shut
    GG_CELL_INDOORS = 1u << 2,  // under a roof: excluded from outdoor lighting
    GG_CELL_WATER   = 1u << 3,  // needs a boat; set from terrain at load
};

typedef struct {
    uint8_t terrain;   // gg_tile_id
    uint8_t prop;      // gg_prop_id + 1, or GG_NO_PROP
    uint8_t flags;     // GG_CELL_*
    uint8_t region;    // region index, for naming and for NPC home lookup
} gg_cell;

#define GG_MAP_NAME_MAX 48
#define GG_REGION_MAX   32

typedef struct {
    char name[GG_MAP_NAME_MAX];
    int  x, y, w, h;        // bounding box, for "thou art in Britain"
    uint8_t kind;           // GG_REGION_*
} gg_region;

enum { GG_REGION_WILD, GG_REGION_TOWN, GG_REGION_DUNGEON, GG_REGION_CASTLE };

typedef struct {
    int  w, h;
    gg_cell *cell;                     // w*h, row-major
    char name[GG_MAP_NAME_MAX];
    gg_region region[GG_REGION_MAX];
    int  regions;
    int  start_x, start_y;             // where a new game begins
    uint32_t seed;                     // the seed this map was generated from
} gg_map;

// --- terrain properties ----------------------------------------------------
// One row per gg_tile_id. Kept as a table rather than a switch so the level
// editor can show the same facts the simulation uses.
typedef struct {
    const char *name;
    bool passable;
    bool water;
    uint8_t cost;      // movement cost in game minutes; 0 means use the default
} gg_terrain_def;

extern const gg_terrain_def GG_TERRAIN[GG_TILE_COUNT];

// --- lifetime --------------------------------------------------------------
bool gg_map_alloc(gg_map *m, int w, int h);
void gg_map_free(gg_map *m);

// --- access ----------------------------------------------------------------
static inline bool gg_map_in_bounds(const gg_map *m, int x, int y) {
    return x >= 0 && y >= 0 && x < m->w && y < m->h;
}

// Out of bounds returns nullptr rather than clamping: a caller that wanted the
// edge tile should say so, and silently substituting one hides walk-off bugs.
static inline gg_cell *gg_map_at(gg_map *m, int x, int y) {
    return gg_map_in_bounds(m, x, y) ? &m->cell[(size_t)y * (size_t)m->w + (size_t)x]
                                     : nullptr;
}

static inline const gg_cell *gg_map_at_const(const gg_map *m, int x, int y) {
    return gg_map_in_bounds(m, x, y) ? &m->cell[(size_t)y * (size_t)m->w + (size_t)x]
                                     : nullptr;
}

// Can a walking actor stand here? Off-map is impassable, which is what makes
// the map edge a wall without a border of blocking tiles.
bool gg_map_walkable(const gg_map *m, int x, int y);

// Which region contains this tile, or -1. Linear over regions on purpose:
// there are at most GG_REGION_MAX of them and this is not on a hot path.
int gg_map_region_at(const gg_map *m, int x, int y);

// --- generation ------------------------------------------------------------
// Builds the demo continent: terrain bands, a lake, forest, a road and a
// walled town. Deterministic in `seed` - the same number always produces the
// same world, which is what makes a screenshot or a bug report reproducible.
bool gg_map_generate(gg_map *m, int w, int h, uint32_t seed);

// --- file format -----------------------------------------------------------
// Maps are data, not code: the game reads them and the editor writes them, and
// neither has map content compiled in. See docs/COMPLETION_PLAN.md for why
// this exists before there is much content to put in it.
#define GG_MAP_MAGIC   "GGMAP\0\0\0"
#define GG_MAP_VERSION 1

bool gg_map_save(const gg_map *m, const char *path);
bool gg_map_load(gg_map *m, const char *path);

#endif // GG_WORLD_H
