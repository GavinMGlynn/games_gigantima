// gg_bestiary.h - what lives in the world, out of a file rather than out of C.
//
// A creature is a row: what it looks like, what it can take and deal, how it
// behaves, what it leaves and how many of it the world holds. Adding one is an
// edit to `assets/bestiary.txt` and nothing else - which is the whole point of
// the item, and the reason `gg_spawn_foe` takes an index into this rather than
// a sprite id with a switch behind it.
#ifndef GG_BESTIARY_H
#define GG_BESTIARY_H

#include "core/gg_common.h"
#include "core/gg_ids.h"
#include "core/gg_world.h"

#define GG_BEASTS_MAX     16
// How many places one kind of creature may be found in.
#define GG_BEAST_HAUNTS_MAX 4
#define GG_BEAST_LOOT_MAX  3
#define GG_BEAST_NAME_MAX 24

// One place a creature is found, and how many of it are there. An empty `map`
// means "any world the generator builds", which is the only place that is not a
// map with a name.
typedef struct {
    char    map[GG_MAP_NAME_MAX];
    int16_t x, y;                     // -1,-1 unless a tile was named
    uint8_t howmany;
} gg_haunt;

// One line of a loot table: some of a thing, some of the time.
typedef struct {
    uint8_t kind;        // gg_item_id
    uint8_t least, most; // inclusive
    uint8_t chance;      // per cent
} gg_loot;

typedef struct {
    char    id[GG_BEAST_NAME_MAX];    // BRIGAND - what a data file calls it
    char    name[GG_BEAST_NAME_MAX];  // "a brigand" - what the log calls it
    uint8_t art;                      // gg_actor_id

    int16_t health;
    uint8_t level;
    uint8_t speed;      // energy per turn; 100 is one action a turn
    uint8_t damage, guard;
    uint8_t reach;      // 1 is arm's length; more is something that throws

    // Behaviour. `notice` is how near you have to come before it cares, and
    // `flees` the health below which it would rather be elsewhere - which is
    // the difference between a creature and a number that walks at you.
    uint8_t notice;
    int16_t flees;

    gg_loot loot[GG_BEAST_LOOT_MAX];
    int     loots;

    // What killing it teaches, in experience. Defaults to its health, which is
    // the plainest rule there is - what a thing can take is what it is worth -
    // and a line in the file overrides it for anything the rule flatters or
    // cheats.
    uint16_t worth;

    // Where it is found, and there may be several answers: a brigand is in the
    // vale *and* in whatever the generator builds, and a map that names none of
    // them has none of it. `haunts N` alone means a generated world; `haunts N
    // PLACE` means that map, the first time it is walked into; `haunts N PLACE
    // X Y` puts them on that tile, which is how a story's villain stands where
    // the story says with nothing in C knowing where that is.
    gg_haunt haunt[GG_BEAST_HAUNTS_MAX];
    int      haunts;
} gg_beast;

// All or nothing, and every complaint names the file and the line.
bool gg_bestiary_load(const char *path);
void gg_bestiary_clear(void);

int gg_bestiary_count(void);
const gg_beast *gg_bestiary_at(int i);

// By the name a data file uses, or -1.
int gg_bestiary_find(const char *id);

#endif // GG_BESTIARY_H
