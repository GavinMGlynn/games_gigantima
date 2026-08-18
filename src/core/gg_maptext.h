// gg_maptext.h - a map as a file you can read.
//
// `.ggmap` is a binary file the editor writes and the game reads, and that was
// the only way a map could exist. It has two costs. The editor needs a mouse,
// so a map cannot be authored by anything that is not a person at a screen; and
// a map in the repository is an opaque blob, so a change to the format strands
// every map already written in it - which is why creatures were placed from the
// bestiary by map name rather than by the map itself.
//
// This is the same map as text: a picture of the ground, one character to a
// tile, and a list of everything standing on it. It is meant to be **read,
// diffed, hand-edited and generated** - by a person, by a script, by anything.
//
//     map 48 48
//     name The Standing Stones
//     seed 0
//     start 24 44
//
//     legend . GRASS
//     legend # MOUNTAIN BLOCKED
//     legend ~ WATER WATER
//
//     row ......##########......
//     row ......#..........#....
//     ...
//
//     region DUNGEON 14 14 21 21 The Stones
//     prop STONE_TALL 20 18
//     item SILVER 1 24 24
//     person MERCHANT 42 44 Iolo
//       at 06 42 44
//     portal 23 47 40 8 vale.ggmap
//
// **A comment is a line that starts with `#`**, and nothing else is - unlike the
// other content files here, which strip a `#` anywhere on a line. `#` is the
// natural character for a wall and this format is mostly a picture made of
// them.
//
// **Free text goes last on a line** - a region's name, a person's name, the map
// a way out leads to - because names have spaces in them and anything after one
// would be eaten by it. "The Standing Stones" is what found that.
//
// **The legend is written out of the map rather than fixed**, because what a
// character means has to cover every combination of terrain and flags a map
// actually contains, and a fixed alphabet would either miss one or invent
// combinations nobody uses. That is also what makes the round trip exact.
#ifndef GG_MAPTEXT_H
#define GG_MAPTEXT_H

#include "core/gg_common.h"
#include "core/gg_world.h"

// Writes `m` as text. Everything in a `.ggmap` is in it: a map written out and
// read back is the same map, byte for byte, through `gg_map_write`.
bool gg_map_write_text(const gg_map *m, const char *path);

// Reads one back. Every complaint names the file and the line, and a file that
// does not parse leaves nothing allocated behind it.
bool gg_map_read_text(gg_map *m, const char *path);

// Is this file the text form? Read rather than guessed from the name, because
// `--map` takes whatever a player typed and a map called `vale.txt` is still a
// map. A file that cannot be opened is not text.
bool gg_map_is_text(const char *path);

#endif // GG_MAPTEXT_H
