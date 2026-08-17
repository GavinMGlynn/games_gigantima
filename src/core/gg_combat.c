// gg_combat.c - blows struck, and who strikes first.
#include "core/gg_combat.h"
#include "core/gg_path.h"

// ---------------------------------------------------------------------------
// Sides
// ---------------------------------------------------------------------------
// Three sides, not two: the Avatar's, the hostiles', and everybody else. A
// townsperson belongs to the third, and nothing can strike them - a stray blow
// that starts a riot is a thing people stop swinging to avoid, and there is no
// crime system for it to mean anything to yet.
static bool with_the_avatar(const gg_game *g, int who) {
    return who == g->player || g->actor[who].party != GG_NOT_IN_PARTY;
}

bool gg_at_odds(const gg_game *g, int a, int b) {
    if (a < 0 || b < 0 || a >= g->actors || b >= g->actors || a == b) return false;
    if (!g->actor[a].active || !g->actor[b].active) return false;

    const bool a_ours = with_the_avatar(g, a), b_ours = with_the_avatar(g, b);
    if (a_ours && g->actor[b].hostile) return true;
    if (b_ours && g->actor[a].hostile) return true;
    return false;
}

// ---------------------------------------------------------------------------
// What a blow is worth
// ---------------------------------------------------------------------------
// Only the Avatar has a pack, so only the Avatar's numbers come out of one.
// Written as one function over `who` rather than two, so that the day a
// companion carries their own kit this is the only place that changes.
static const gg_item_def *held(const gg_game *g, int who, gg_slot_id slot) {
    if (who != g->player) return nullptr;
    const int i = g->equipped[slot];
    if (!gg_pack_slot_ok(g, i)) return nullptr;
    return &GG_ITEM[g->pack[i].kind];
}

int gg_attack_power(const gg_game *g, int who) {
    if (who < 0 || who >= g->actors) return 0;
    const gg_item_def *w = held(g, who, GG_SLOT_WEAPON);
    return g->actor[who].damage + (w ? w->damage : 0);
}

int gg_guard_power(const gg_game *g, int who) {
    if (who < 0 || who >= g->actors) return 0;
    const gg_item_def *a = held(g, who, GG_SLOT_ARMOUR);
    return g->actor[who].guard + (a ? a->guard : 0);
}

int gg_reach(const gg_game *g, int who) {
    if (who < 0 || who >= g->actors) return 1;
    const gg_item_def *w = held(g, who, GG_SLOT_WEAPON);
    return (w && w->reach) ? w->reach : 1;
}

// ---------------------------------------------------------------------------
// Seeing
// ---------------------------------------------------------------------------
bool gg_line_of_sight(const gg_game *g, int fx, int fy, int tx, int ty) {
    // Bresenham, stopping at the first cell that blocks. The starting cell and
    // the target cell are both exempt: you can throw out of a doorway you are
    // standing in, and at somebody standing in one.
    int dx = gg_absi(tx - fx), dy = -gg_absi(ty - fy);
    const int sx = fx < tx ? 1 : -1, sy = fy < ty ? 1 : -1;
    int err = dx + dy;
    int x = fx, y = fy;

    for (;;) {
        if (x == tx && y == ty) return true;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
        if (x == tx && y == ty) return true;

        const gg_cell *c = gg_map_at_const(&g->map, x, y);
        if (!c) return false;
        // A wall stops a stone. Water does not - it is only impassable to feet.
        if ((c->flags & GG_CELL_BLOCKED) && !(c->flags & GG_CELL_DOOR))
            return false;
    }
}

// ---------------------------------------------------------------------------
// Striking
// ---------------------------------------------------------------------------
// Everything they were carrying, onto the tile they fell on. Loot goes through
// the same gg_ground_drop a dropped item does, so it can be picked up by the
// same key and is saved by the same code.
static void die(gg_game *g, int who) {
    gg_actor *a = &g->actor[who];

    // The Avatar is not removed from the world - everything from the camera to
    // gg_player reads through that actor, and a game whose player index points
    // at a dead slot crashes rather than ends. The game ends instead.
    if (who == g->player) {
        gg_log(g, "Thou art slain. Thy journey ends here.");
        a->hp = 0;
        g->mode = GG_MODE_GAMEOVER;
        return;
    }

    gg_log(g, "%s falls.", a->name);

    if (a->loot_count > 0)
        gg_ground_drop(&g->map, a->x, a->y, (gg_item_id)a->loot_kind,
                       a->loot_count);

    // Removed rather than left as a corpse: there is no corpse art, and a
    // body that blocks nothing and can be walked through is worse than none.
    a->active = false;
    a->hostile = false;
    if (a->party != GG_NOT_IN_PARTY) gg_party_leave(g, who);
    if (g->talking_to == who) g->talking_to = -1;
}

int gg_strike(gg_game *g, int attacker, int defender) {
    if (!gg_at_odds(g, attacker, defender)) return 0;

    gg_actor *at = &g->actor[attacker];
    gg_actor *de = &g->actor[defender];

    at->facing = gg_facing_from_delta(de->x - at->x, de->y - at->y);

    // One roll, through the game's RNG so the fight is part of the seeded
    // world. Level counts for the striker, guard for the defender.
    const int roll = 1 + (int)gg_rand_below(&g->rng, 20);
    const int against = GG_HIT_TARGET + gg_guard_power(g, defender);
    if (roll + at->level < against) {
        gg_log(g, "%s misses %s.", at->name, de->name);
        return 0;
    }

    // Damage is never zero on a hit: a blow that connects and does nothing
    // reads as a bug however the arithmetic got there.
    const int power = gg_attack_power(g, attacker);
    const int hurt = 1 + power + (int)gg_rand_below(&g->rng, 3);

    de->hp = (int16_t)(de->hp - hurt);
    gg_log(g, "%s strikes %s for %d.", at->name, de->name, hurt);

    if (de->hp <= 0) {
        de->hp = 0;
        die(g, defender);
    }
    return hurt;
}

bool gg_throw_at(gg_game *g, int who, int tx, int ty) {
    if (who < 0 || who >= g->actors) return false;

    const int reach = gg_reach(g, who);
    if (reach <= 1) return false;

    const gg_actor *me = &g->actor[who];
    if (gg_dist_cheb(me->x, me->y, tx, ty) > reach) return false;
    if (!gg_line_of_sight(g, me->x, me->y, tx, ty)) return false;

    int target = -1;
    for (int i = 0; i < g->actors; i++)
        if (g->actor[i].active && g->actor[i].x == tx && g->actor[i].y == ty &&
            gg_at_odds(g, who, i))
            target = i;
    if (target < 0) return false;

    // The stone leaves the hand whether or not it lands, and lies where it
    // was thrown - which is what makes a fight worth walking back across.
    const int slot = (who == g->player) ? g->equipped[GG_SLOT_WEAPON] : -1;
    gg_item_id thrown = GG_ITEM_COUNT;
    if (gg_pack_slot_ok(g, slot)) {
        thrown = (gg_item_id)g->pack[slot].kind;
        gg_pack_take(g, slot, 1);
    }

    gg_strike(g, who, target);
    if (thrown != GG_ITEM_COUNT)
        gg_ground_drop(&g->map, tx, ty, thrown, 1);
    return true;
}

// ---------------------------------------------------------------------------
// The hostiles' turn
// ---------------------------------------------------------------------------
// Who acts, in what order. Sorted by speed, and ties broken by actor index so
// the order is total - two brigands of the same speed must not swap places
// between runs of the same seed, or a scripted fight stops being scripted.
typedef struct { int who, speed; } gg_turn_order;

static int order_cmp(const void *va, const void *vb) {
    const gg_turn_order *a = va, *b = vb;
    if (a->speed != b->speed) return a->speed > b->speed ? -1 : 1;
    return a->who < b->who ? -1 : 1;
}

// The nearest thing on the other side, by index on a tie - again, so that the
// same seed picks the same quarrel every time.
static int nearest_foe(const gg_game *g, int who) {
    int best = -1, best_d = 0;
    for (int i = 0; i < g->actors; i++) {
        if (!gg_at_odds(g, who, i)) continue;
        const int d = gg_dist_cheb(g->actor[who].x, g->actor[who].y,
                                   g->actor[i].x, g->actor[i].y);
        if (best < 0 || d < best_d) { best = i; best_d = d; }
    }
    return best;
}

static bool passable_for(void *vctx, int x, int y);

typedef struct { const gg_game *g; int self; } gg_walk_here;

static bool passable_for(void *vctx, int x, int y) {
    const gg_walk_here *c = vctx;
    if (!gg_map_walkable(&c->g->map, x, y)) return false;
    return !gg_actor_occupied(c->g->actor, c->g->actors, x, y, c->self);
}

static void take_one_action(gg_game *g, int who) {
    gg_actor *a = &g->actor[who];
    const int foe = nearest_foe(g, who);
    if (foe < 0) return;

    const int d = gg_dist_cheb(a->x, a->y, g->actor[foe].x, g->actor[foe].y);

    // Out of sight, out of mind. They hold their ground until somebody comes
    // near enough to be worth robbing, with the occasional turn of the head so
    // a waiting brigand is not a statue.
    if (d > GG_NOTICE_RANGE) {
        if (gg_rand_below(&g->rng, 16) == 0)
            a->facing = (uint8_t)gg_rand_below(&g->rng, 4);
        return;
    }

    if (d <= 1) {
        gg_strike(g, who, foe);
        return;
    }
    if (d <= gg_reach(g, who) &&
        gg_throw_at(g, who, g->actor[foe].x, g->actor[foe].y))
        return;

    gg_walk_here ctx = { .g = g, .self = who };
    int nx, ny;
    if (gg_path_next_step(&g->path, passable_for, &ctx, a->x, a->y,
                          g->actor[foe].x, g->actor[foe].y,
                          GG_PATH_BUDGET, &nx, &ny) &&
        gg_map_walkable(&g->map, nx, ny) &&
        !gg_actor_occupied(g->actor, g->actors, nx, ny, who)) {
        gg_actor_move_to(a, nx, ny);
    } else {
        gg_actor_step_toward(a, &g->map, g->actor, g->actors,
                             g->actor[foe].x, g->actor[foe].y, &g->rng);
    }
}

void gg_combat_turn(gg_game *g) {
    gg_turn_order order[GG_ACTORS_MAX];
    int n = 0;

    for (int i = 0; i < g->actors && n < GG_ACTORS_MAX; i++) {
        if (!g->actor[i].active || !g->actor[i].hostile) continue;
        // Energy fills by speed and empties by an action. A creature with no
        // speed set would never move, so treat unset as ordinary.
        const int speed = g->actor[i].speed ? g->actor[i].speed
                                            : GG_ENERGY_PER_ACTION;
        g->actor[i].energy = (int16_t)(g->actor[i].energy + speed);
        order[n++] = (gg_turn_order){ .who = i, .speed = speed };
    }
    if (n == 0) return;

    SDL_qsort(order, (size_t)n, sizeof *order, order_cmp);

    for (int k = 0; k < n; k++) {
        const int who = order[k].who;
        // A quick one acts twice; a slow one waits its turn out. Bounded so a
        // silly speed cannot spend a whole frame in here.
        for (int acts = 0; acts < 4; acts++) {
            if (!g->actor[who].active) break;
            if (g->actor[who].energy < GG_ENERGY_PER_ACTION) break;
            g->actor[who].energy = (int16_t)(g->actor[who].energy -
                                             GG_ENERGY_PER_ACTION);
            take_one_action(g, who);
        }
    }
}

int gg_spawn_foe(gg_game *g, gg_actor_id art, int x, int y) {
    if (g->actors >= GG_ACTORS_MAX) return -1;
    if (!gg_map_walkable(&g->map, x, y)) return -1;
    if (gg_actor_occupied(g->actor, g->actors, x, y, -1)) return -1;

    const int i = g->actors++;
    gg_actor *a = &g->actor[i];
    SDL_zerop(a);
    a->active = true;
    a->art = (uint8_t)art;
    a->def = GG_ACTOR_NO_DEF;
    a->facing = GG_FACE_DOWN;
    a->x = (int16_t)x;
    a->y = (int16_t)y;
    a->from_x = a->x;
    a->from_y = a->y;
    a->hostile = true;
    a->party = GG_NOT_IN_PARTY;
    a->level = 1;

    // An outlaw is quicker and lighter; a brigand hits harder and stands up to
    // more. Two shapes rather than one, so initiative and guard are both
    // visible in an ordinary fight instead of being numbers nobody meets.
    if (art == GG_ACTOR_OUTLAW) {
        SDL_strlcpy(a->name, "an outlaw", sizeof a->name);
        a->hp = a->hp_max = 10;
        a->speed = 150;
        a->damage = 2;
        a->guard = 0;
        a->loot_kind = GG_ITEM_STONE;
        a->loot_count = 3;
    } else {
        SDL_strlcpy(a->name, "a brigand", sizeof a->name);
        a->hp = a->hp_max = 16;
        a->speed = 100;
        a->damage = 3;
        a->guard = 2;
        a->loot_kind = GG_ITEM_GOLD;
        a->loot_count = 12;
    }
    return i;
}
