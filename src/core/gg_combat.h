// gg_combat.h - blows struck, and who strikes first.
//
// Combat is turn-based like everything else: it happens inside gg_game_act and
// at no other time. What it adds to a turn is an order. Everyone carries an
// energy budget that fills by their speed each turn and empties by 100 per
// action, so a quick creature acts twice where a slow one acts every other
// turn - and among those acting, the quickest goes first. That is initiative,
// and it is integer and seeded like the rest of the simulation, so a fight
// with a given seed resolves the same way every time it is run.
//
// Every roll goes through the game's own RNG, which is saved. A fight is
// therefore part of the reproducible world, not something on top of it.
#ifndef GG_COMBAT_H
#define GG_COMBAT_H

#include "core/gg_common.h"
#include "core/gg_game.h"

// Energy needed for one action. Speed is added per turn, so speed 100 is one
// action a turn, 200 is two, and 50 is one every other turn.
#define GG_ENERGY_PER_ACTION 100

// The number a blow has to reach. Rolled 1..20 and added to the striker's
// level; the defender contributes their guard. Ten is the middle of a d20, so
// an unarmoured, unskilled pair trade blows about half the time.
#define GG_HIT_TARGET 10

// What striking somebody who is not looking at you is worth. Everything turns
// to face what it strikes, so a back is turned only when its owner is busy with
// somebody else - which makes this the reward for bringing company rather than
// a bonus anybody can farm. A roll of twenty is a telling blow: it lands
// whatever the guard, and it lands twice as hard.
#define GG_FLANK_BONUS  4
#define GG_FLANK_DAMAGE 2

// The most guard a ward may add, however a save file was written. It is a
// ceiling on a number out of a file rather than a balance figure: at ten a
// warded Avatar is hit by an ordinary brigand about one blow in five, and
// beyond that the fight stops being one.
#define GG_WARD_MAX 10

// How near the Avatar has to come before something takes an interest, for
// anything that does not say. Each creature may set its own; without any limit
// at all a brigand hunts from anywhere on the map, and a player who stands
// still for a few hundred turns is killed by somebody who set out from the far
// side of the continent - which is not menace, it is bookkeeping.
#define GG_NOTICE_RANGE 8

// Are these two on opposite sides? The Avatar and their party on one, anything
// hostile on the other. Townsfolk are on nobody's side and are never struck by
// accident - a game where a stray blow starts a riot is a game people stop
// swinging in.
bool gg_at_odds(const gg_game *g, int a, int b);

// What `who` strikes with, and what they turn aside. Natural ability plus
// whatever is held, so the Avatar's numbers come from the pack and a brigand's
// come from being a brigand.
int gg_attack_power(const gg_game *g, int who);
int gg_guard_power(const gg_game *g, int who);

// How far `who` can strike, in tiles. One unless something with reach is
// readied, which is what makes a stone worth carrying.
int gg_reach(const gg_game *g, int who);

// Can `who` see from their tile to (x, y)? A straight walk that stops at the
// first thing that blocks. Used for throwing, so a stone cannot be lobbed
// through a wall.
bool gg_line_of_sight(const gg_game *g, int fx, int fy, int tx, int ty);

// One blow from `attacker` at `defender`. Returns the damage dealt, or 0 for a
// miss, and logs what happened either way. Kills, loots and removes the
// defender if it takes them below one.
int gg_strike(gg_game *g, int attacker, int defender);

// Throws whatever `who` has readied at the actor on (tx, ty). Returns false -
// having done nothing and spent nothing - if there is no reach, no line, or
// nothing there to hit.
bool gg_throw_at(gg_game *g, int who, int tx, int ty);

// Everything hostile takes its turn, in initiative order. Called from the
// world's half of a turn.
void gg_combat_turn(gg_game *g);

// Puts one of the bestiary's creatures at (x, y). `beast` is an index into
// gg_bestiary_at. Returns its actor index, or -1 if there is no room, the tile
// will not take one, or there is no such creature.
//
// Nothing in C knows what a brigand is: this reads the row and copies it.
int gg_spawn_foe(gg_game *g, int beast, int x, int y);

// The same, by the name the bestiary uses - "BRIGAND". For tests and for the
// day a quest wants to put something specific somewhere.
int gg_spawn_named(gg_game *g, const char *id, int x, int y);

#endif // GG_COMBAT_H
