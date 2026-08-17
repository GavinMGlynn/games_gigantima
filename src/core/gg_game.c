// gg_game.c - the turn loop, the world clock, and the townsfolk who live by it.
#include "core/gg_game.h"

#include <stdarg.h>

const char *const GG_ITEM_NAME[GG_ITEM_COUNT] = {
    [GG_ITEM_FOOD]   = "food",
    [GG_ITEM_GOLD]   = "gold",
    [GG_ITEM_TORCH]  = "torches",
    [GG_ITEM_KEY]    = "keys",
    [GG_ITEM_GEM]    = "gems",
    [GG_ITEM_POTION] = "potions",
};

// ---------------------------------------------------------------------------
// Message log
// ---------------------------------------------------------------------------
void gg_log(gg_game *g, const char *fmt, ...) {
    if (g->logn == GG_LOG_LINES) {
        // Scroll. A ring buffer would avoid the copy, but the log is five
        // short lines and the HUD wants them in order - the memmove is free
        // and the code that reads it stays obvious.
        SDL_memmove(g->log[0], g->log[1], sizeof g->log[0] * (GG_LOG_LINES - 1));
        g->logn--;
    }
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(g->log[g->logn], GG_LOG_WIDTH, fmt, ap);
    va_end(ap);
    g->logn++;
}

// ---------------------------------------------------------------------------
// Clock
// ---------------------------------------------------------------------------
int gg_game_hour(const gg_game *g)   { return (int)(g->minutes / 60); }
int gg_game_minute(const gg_game *g) { return (int)(g->minutes % 60); }

uint8_t gg_game_daylight(const gg_game *g) {
    // A triangle wave peaking at noon, then squared to flatten the middle of
    // the day and steepen dawn and dusk - a linear ramp spends far too much of
    // the day in a half-light that never looks like either.
    const int m = (int)g->minutes;
    const int from_noon = gg_absi(m - GG_MINUTES_PER_DAY / 2);
    const int lit = GG_MINUTES_PER_DAY / 2 - from_noon;      // 0..720
    const int frac = lit * 255 / (GG_MINUTES_PER_DAY / 2);   // 0..255
    return (uint8_t)(frac * frac / 255);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
bool gg_action_delta(gg_action a, int *dx, int *dy) {
    switch (a) {
    case GG_ACT_N:  *dx =  0; *dy = -1; return true;
    case GG_ACT_S:  *dx =  0; *dy =  1; return true;
    case GG_ACT_E:  *dx =  1; *dy =  0; return true;
    case GG_ACT_W:  *dx = -1; *dy =  0; return true;
    case GG_ACT_NE: *dx =  1; *dy = -1; return true;
    case GG_ACT_NW: *dx = -1; *dy = -1; return true;
    case GG_ACT_SE: *dx =  1; *dy =  1; return true;
    case GG_ACT_SW: *dx = -1; *dy =  1; return true;
    default: *dx = *dy = 0; return false;
    }
}

int gg_game_facing_actor(const gg_game *g) {
    const gg_actor *p = gg_player_const(g);
    static const int DX[4] = { 0, -1, 0, 1 };   // matches gg_facing order
    static const int DY[4] = { -1, 0, 1, 0 };
    const int tx = p->x + DX[p->facing], ty = p->y + DY[p->facing];
    for (int i = 0; i < g->actors; i++) {
        if (i == g->player || !g->actor[i].active) continue;
        if (g->actor[i].x == tx && g->actor[i].y == ty) return i;
    }
    return -1;
}

const char *gg_game_place(const gg_game *g) {
    const gg_actor *p = gg_player_const(g);
    const int r = gg_map_region_at(&g->map, p->x, p->y);
    return r >= 0 ? g->map.region[r].name : "the wilderness";
}

// ---------------------------------------------------------------------------
// The world's half of a turn
// ---------------------------------------------------------------------------
static void world_turn(gg_game *g, int minutes) {
    g->turn++;
    g->minutes += (uint32_t)minutes;
    while (g->minutes >= GG_MINUTES_PER_DAY) {
        g->minutes -= GG_MINUTES_PER_DAY;
        g->day++;
    }

    const int hour = gg_game_hour(g);
    for (int i = 0; i < g->actors; i++) {
        gg_actor *a = &g->actor[i];
        if (i == g->player || !a->active) continue;

        int tx, ty;
        if (!gg_actor_target_at(a, hour, &tx, &ty)) continue;

        // Already where it should be: idle, with an occasional shuffle so a
        // town at rest is not a town of statues.
        if (a->x == tx && a->y == ty) {
            if (gg_rand_below(&g->rng, 12) == 0)
                a->facing = (uint8_t)gg_rand_below(&g->rng, 4);
            continue;
        }
        gg_actor_step_toward(a, &g->map, g->actor, g->actors, tx, ty, &g->rng);
    }
}

// ---------------------------------------------------------------------------
// The player's half
// ---------------------------------------------------------------------------
static void do_move(gg_game *g, int dx, int dy) {
    gg_actor *p = gg_player(g);
    const int nx = p->x + dx, ny = p->y + dy;

    p->facing = gg_facing_from_delta(dx, dy);

    // An occupied tile is a conversation, not a shove. Ultima VI let you walk
    // into somebody to talk; doing the same here means the common case needs
    // no key at all.
    for (int i = 0; i < g->actors; i++) {
        if (i == g->player || !g->actor[i].active) continue;
        if (g->actor[i].x == nx && g->actor[i].y == ny) {
            g->talking_to = i;
            g->mode = GG_MODE_CONVERSE;
            gg_log(g, "%s: \"%s\"", g->actor[i].name,
                   g->actor[i].greeting ? g->actor[i].greeting : "Hail.");
            return;
        }
    }

    if (!gg_map_walkable(&g->map, nx, ny)) {
        const gg_cell *c = gg_map_at_const(&g->map, nx, ny);
        g->blocked_bump = true;
        if (!c)
            gg_log(g, "Thou canst go no further.");
        else if (c->flags & GG_CELL_WATER)
            gg_log(g, "Thou wouldst drown. A boat is needed.");
        else if (GG_HAS_PROP(c))
            gg_log(g, "Blocked.");
        else
            gg_log(g, "%s bars the way.", GG_TERRAIN[c->terrain].name);
        // A refused move still costs the world nothing: bumping a wall is not
        // a turn, or a player could starve by walking into a rock.
        return;
    }

    const gg_cell *c = gg_map_at_const(&g->map, nx, ny);
    const int cost = (c && GG_TERRAIN[c->terrain].cost)
                     ? GG_TERRAIN[c->terrain].cost : GG_MINUTES_PER_TURN;

    gg_actor_move_to(p, nx, ny);
    world_turn(g, cost);
}

static void do_look(gg_game *g) {
    const gg_actor *p = gg_player_const(g);
    const gg_cell *c = gg_map_at_const(&g->map, p->x, p->y);
    if (!c) return;

    if (GG_HAS_PROP(c))
        gg_log(g, "Thou seest %s, upon %s.",
               "growth", GG_TERRAIN[c->terrain].name);
    else
        gg_log(g, "Thou standest upon %s, in %s.",
               GG_TERRAIN[c->terrain].name, gg_game_place(g));
    world_turn(g, GG_MINUTES_PER_TURN);
}

static void do_talk(gg_game *g) {
    const int who = gg_game_facing_actor(g);
    if (who < 0) {
        gg_log(g, "There is no one there.");
        return;
    }
    g->talking_to = who;
    g->mode = GG_MODE_CONVERSE;
    gg_log(g, "%s: \"%s\"", g->actor[who].name,
           g->actor[who].greeting ? g->actor[who].greeting : "Hail.");
}

static void do_open(gg_game *g) {
    gg_actor *p = gg_player(g);
    static const int DX[4] = { 0, -1, 0, 1 };
    static const int DY[4] = { -1, 0, 1, 0 };
    gg_cell *c = gg_map_at(&g->map, p->x + DX[p->facing], p->y + DY[p->facing]);
    if (c && (c->flags & GG_CELL_DOOR)) {
        gg_log(g, "The door stands open.");
        world_turn(g, GG_MINUTES_PER_TURN);
    } else {
        gg_log(g, "Nothing there to open.");
    }
}

void gg_game_act(gg_game *g, gg_action a) {
    if (g->mode == GG_MODE_CONVERSE) {
        // Any key leaves the conversation for now. The keyword system that
        // makes this worth entering is a named plan item.
        g->mode = GG_MODE_PLAY;
        g->talking_to = -1;
        return;
    }
    if (g->mode != GG_MODE_PLAY) return;

    int dx, dy;
    if (gg_action_delta(a, &dx, &dy)) {
        do_move(g, dx, dy);
        return;
    }

    switch (a) {
    case GG_ACT_WAIT: gg_log(g, "Thou dost wait."); world_turn(g, GG_MINUTES_PER_TURN); break;
    case GG_ACT_LOOK: do_look(g); break;
    case GG_ACT_TALK: do_talk(g); break;
    case GG_ACT_OPEN: do_open(g); break;
    default: break;
    }
}

void gg_game_animate(gg_game *g) {
    for (int i = 0; i < g->actors; i++)
        if (g->actor[i].active) gg_actor_animate(&g->actor[i]);
}

// ---------------------------------------------------------------------------
// World population
// ---------------------------------------------------------------------------
// A townsperson: a name, a look, and a day. The schedule is what makes them a
// person rather than a wandering sprite, so every one of them gets a real one.
typedef struct {
    const char *name;
    uint8_t     art;
    const char *greeting;
    uint8_t     hours[4];
    int8_t      dx[4], dy[4];   // offsets from the town centre
} gg_townsfolk_def;

static const gg_townsfolk_def TOWNSFOLK[] = {
    { "Iolo",     GG_ACTOR_MERCHANT, "Hail, Avatar! The market opens at dawn.",
      { 6, 12, 18, 22 }, { -6, 0, 6, -6 }, { -4, -6, -4, 2 } },
    { "Shamino",  GG_ACTOR_GUARD,    "The gate is watched. Pass freely.",
      { 5, 11, 17, 23 }, { 0, 8, 0, -8 }, { 8, 0, -8, 0 } },
    { "Dupre",    GG_ACTOR_GUARD,    "Well met. Keep thy blade keen.",
      { 7, 13, 19, 1 },  { 8, -8, 2, 2 },  { 2, 2, 8, -6 } },
    { "Katrina",  GG_ACTOR_HEALER,   "Art thou wounded? I have herbs.",
      { 6, 12, 20, 23 }, { -8, -2, -8, -8 }, { 0, 4, 0, 4 } },
    { "Nystul",   GG_ACTOR_MAGE,     "The stars speak, if thou wilt listen.",
      { 9, 15, 21, 2 },  { 4, 4, -4, -4 }, { -6, 4, 6, 6 } },
    { "Nell",     GG_ACTOR_ELDER,    "I have seen eighty winters in this vale.",
      { 8, 14, 19, 21 }, { -2, 2, -6, -6 }, { -2, -2, 6, 6 } },
    { "Gwenno",   GG_ACTOR_HEALER,   "Dost thou bring news from the north?",
      { 7, 12, 18, 22 }, { 6, -4, 6, 6 },  { 6, 6, -2, 6 } },
    { "Chuckles", GG_ACTOR_MERCHANT, "A riddle! Why does the Avatar cross the vale?",
      { 10, 14, 18, 23 }, { 0, -6, 6, 0 }, { 0, 0, 6, -6 } },
};

static void place_townsfolk(gg_game *g) {
    // Anchor everyone on the town's centre so the schedule offsets land inside
    // the walls wherever the generator put the town.
    int cx = g->map.start_x, cy = g->map.start_y;
    for (int i = 0; i < g->map.regions; i++) {
        if (g->map.region[i].kind == GG_REGION_TOWN) {
            cx = g->map.region[i].x + g->map.region[i].w / 2;
            cy = g->map.region[i].y + g->map.region[i].h / 2;
            break;
        }
    }

    for (size_t i = 0; i < GG_COUNTOF(TOWNSFOLK) && g->actors < GG_ACTORS_MAX; i++) {
        const gg_townsfolk_def *d = &TOWNSFOLK[i];
        gg_actor *a = &g->actor[g->actors];
        SDL_zerop(a);
        a->active = true;
        a->art = d->art;
        a->greeting = d->greeting;
        SDL_strlcpy(a->name, d->name, sizeof a->name);

        for (int k = 0; k < 4; k++) {
            int sx = gg_clampi(cx + d->dx[k], 1, g->map.w - 2);
            int sy = gg_clampi(cy + d->dy[k], 1, g->map.h - 2);
            // A schedule point inside a wall would have the NPC shoving at it
            // all day, so walk outward until the target is somewhere it can
            // actually stand.
            if (!gg_map_walkable(&g->map, sx, sy)) {
                bool found = false;
                for (int r = 1; r < 12 && !found; r++)
                    for (int oy = -r; oy <= r && !found; oy++)
                        for (int ox = -r; ox <= r && !found; ox++)
                            if (gg_map_walkable(&g->map, sx + ox, sy + oy)) {
                                sx += ox; sy += oy; found = true;
                            }
            }
            a->sched[k].hour = d->hours[k];
            a->sched[k].x = (int16_t)sx;
            a->sched[k].y = (int16_t)sy;
        }
        a->schedn = 4;

        // Start each of them where their day says they should be, so the
        // opening frame is a town mid-morning rather than a crowd at spawn.
        int tx, ty;
        if (gg_actor_target_at(a, gg_game_hour(g), &tx, &ty)) {
            a->x = (int16_t)tx;
            a->y = (int16_t)ty;
        }
        g->actors++;
    }
}

bool gg_game_new(gg_game *g, uint32_t seed, const char *profile) {
    SDL_zerop(g);
    gg_rng_seed(&g->rng, seed);
    g->talking_to = -1;

    if (!gg_map_generate(&g->map, 192, 160, seed)) return false;

    SDL_strlcpy(g->profile, profile && *profile ? profile : "Avatar",
                sizeof g->profile);

    // Start at 8am on day one: a town already awake, so the schedules are
    // visibly doing something within the first few turns.
    g->minutes = 8 * 60;
    g->day = 1;
    g->hp = g->hp_max = 30;
    g->level = 1;
    g->item[GG_ITEM_FOOD] = 20;
    g->item[GG_ITEM_GOLD] = 100;
    g->item[GG_ITEM_TORCH] = 3;

    // The player is actor 0 so that `player` never has to be re-found.
    gg_actor *p = &g->actor[0];
    SDL_zerop(p);
    p->active = true;
    p->art = GG_ACTOR_AVATAR;
    p->facing = GG_FACE_DOWN;
    p->x = (int16_t)g->map.start_x;
    p->y = (int16_t)g->map.start_y;
    SDL_strlcpy(p->name, g->profile, sizeof p->name);
    g->player = 0;
    g->actors = 1;

    place_townsfolk(g);

    g->mode = GG_MODE_PLAY;
    gg_log(g, "%s. Day %u, %s.", g->map.name, g->day, gg_game_place(g));
    gg_log(g, "Thou art the Avatar. Seek the vale's troubles.");
    return true;
}

void gg_game_free(gg_game *g) {
    if (!g) return;
    gg_map_free(&g->map);
}
