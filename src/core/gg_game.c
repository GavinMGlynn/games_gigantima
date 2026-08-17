// gg_game.c - the turn loop, the world clock, and the townsfolk who live by it.
#include "core/gg_game.h"

#include <stdarg.h>

// ---------------------------------------------------------------------------
// The party
//
// A companion walks where the Avatar walked, not where the Avatar is. Each one
// holds a slot, and slot N walks to the Nth footprint back - so the line files
// through a doorway one at a time instead of four people all trying to stand in
// it. That is the whole formation; there is no arrangement to choose, because
// single file is the only one a one-tile door admits.
// ---------------------------------------------------------------------------
int gg_party_size(const gg_game *g) {
    int n = 0;
    for (int i = 0; i < g->actors; i++)
        if (g->actor[i].active && g->actor[i].party != GG_NOT_IN_PARTY) n++;
    return n;
}

int gg_party_at(const gg_game *g, int slot) {
    for (int i = 0; i < g->actors; i++)
        if (g->actor[i].active && g->actor[i].party == slot) return i;
    return -1;
}

bool gg_party_join(gg_game *g, int who) {
    if (who < 0 || who >= g->actors || who == g->player) return false;
    if (!g->actor[who].active) return false;
    if (g->actor[who].party != GG_NOT_IN_PARTY) return false;
    if (gg_party_size(g) >= GG_PARTY_MAX) return false;

    // The first free slot, so leaving and rejoining does not leave a hole for
    // somebody behind to follow.
    for (int slot = 1; slot <= GG_PARTY_MAX; slot++) {
        if (gg_party_at(g, slot) >= 0) continue;
        g->actor[who].party = (uint8_t)slot;
        // A companion has given up their day. Keeping the schedule would have
        // them wander off to bed in the middle of a journey.
        g->actor[who].schedn = 0;
        return true;
    }
    return false;
}

void gg_party_leave(gg_game *g, int who) {
    if (who < 0 || who >= g->actors) return;
    const uint8_t slot = g->actor[who].party;
    if (slot == GG_NOT_IN_PARTY) return;

    g->actor[who].party = GG_NOT_IN_PARTY;
    // Close the gap, or whoever was behind them follows a footprint nobody is
    // making and the line stretches out across the map.
    for (int i = 0; i < g->actors; i++)
        if (g->actor[i].party > slot) g->actor[i].party--;
}

// Remembers where the Avatar has just been. Newest first, so slot N reads
// trail[N-1] and the person immediately behind walks in the Avatar's last
// footprint rather than trying to share the current one.
static void trail_push(gg_game *g, int x, int y) {
    if (g->trailn > 0 && g->trail_x[0] == x && g->trail_y[0] == y) return;
    for (int i = GG_TRAIL_MAX - 1; i > 0; i--) {
        g->trail_x[i] = g->trail_x[i - 1];
        g->trail_y[i] = g->trail_y[i - 1];
    }
    g->trail_x[0] = (int16_t)x;
    g->trail_y[0] = (int16_t)y;
    if (g->trailn < GG_TRAIL_MAX) g->trailn++;
}

// ---------------------------------------------------------------------------
// Conversation
//
// The vocabulary is the state. There is no dialogue tree, no flags and no
// script: a topic can be asked when its word is known, and words are handed
// over by other topics. That one rule is what lets a rumour cross a town -
// Iolo teaches CARAVAN, and it is Shamino and Nell who have something to say
// about it - without anything in here knowing that a rumour exists.
// ---------------------------------------------------------------------------
bool gg_knows(const gg_game *g, const char *word) {
    if (!word || !*word) return false;
    for (int i = 0; i < g->knownn; i++)
        if (SDL_strcasecmp(g->known[i], word) == 0) return true;
    return false;
}

bool gg_learn(gg_game *g, const char *word) {
    if (!word || !*word) return false;
    if (gg_knows(g, word)) return false;
    if (g->knownn >= GG_KNOWN_MAX) return false;
    SDL_strlcpy(g->known[g->knownn++], word, GG_WORD_MAX);
    return true;
}

void gg_conversation_refresh(gg_game *g) {
    g->askables = 0;
    if (!g->speaker) return;

    for (int i = 0; i < g->speaker->topics && g->askables < GG_TOPICS_MAX; i++) {
        const gg_topic *t = &g->speaker->topic[i];
        // Any synonym will do, but the first one is what gets shown - so a
        // player who learned "market" is offered "job", which is the word the
        // author chose to label it with.
        for (int w = 0; w < t->words; w++) {
            if (!gg_knows(g, t->word[w])) continue;
            SDL_strlcpy(g->askable[g->askables++], t->word[0], GG_WORD_MAX);
            break;
        }
    }
    if (g->ask_cursor >= g->askables) g->ask_cursor = 0;
    if (g->ask_cursor < 0) g->ask_cursor = 0;
}

// What they say, replacing whatever they said before.
static void speaker_says(gg_game *g, const char *const *lines, int n) {
    g->saids = 0;
    for (int i = 0; i < n && g->saids < GG_TOPIC_LINES_MAX; i++)
        SDL_strlcpy(g->said[g->saids++], lines[i], GG_LINE_MAX);
}

// Walking up to somebody. The book is looked up by the actor's name; a person
// with no entry still greets, because a town half-written should read as a town
// whose people are quiet, not as a broken one.
static void begin_conversation(gg_game *g, int who) {
    g->talking_to = who;
    g->mode = GG_MODE_CONVERSE;
    g->speaker = gg_dialogue_find(g->actor[who].name);
    g->ask_cursor = 0;

    const char *hail = g->speaker && g->speaker->greet[0] ? g->speaker->greet
                     : g->actor[who].greeting ? g->actor[who].greeting
                     : "Hail.";
    const char *one[1] = { hail };
    speaker_says(g, one, 1);
    gg_conversation_refresh(g);
    gg_log(g, "%s: \"%s\"", g->actor[who].name, hail);
}

static void end_conversation(gg_game *g) {
    if (g->speaker && g->speaker->bye[0])
        gg_log(g, "%s: \"%s\"", g->actor[g->talking_to].name, g->speaker->bye);
    g->mode = GG_MODE_PLAY;
    g->talking_to = -1;
    g->speaker = nullptr;
    g->saids = 0;
    g->askables = 0;
}

void gg_conversation_ask(gg_game *g) {
    if (g->mode != GG_MODE_CONVERSE || !g->speaker) return;
    if (g->ask_cursor < 0 || g->ask_cursor >= g->askables) return;

    const gg_topic *t = gg_speaker_topic(g->speaker, g->askable[g->ask_cursor]);
    if (!t) return;

    const char *lines[GG_TOPIC_LINES_MAX];
    for (int i = 0; i < t->says; i++) lines[i] = t->say[i];
    speaker_says(g, lines, t->says);

    // Learning a word is the only thing a conversation changes in the world,
    // and it may make this very speaker answerable to something new - so the
    // list is rebuilt before the player looks at it again.
    if (t->teach[0] && gg_learn(g, t->teach)) {
        gg_log(g, "Thou hast learned of %s.", t->teach);
        gg_conversation_refresh(g);
    }

    // Recruiting is the one thing a conversation does beyond handing over a
    // word, and it is declared in the book rather than here.
    if (t->joins && g->talking_to >= 0) {
        gg_actor *a = &g->actor[g->talking_to];
        if (a->party != GG_NOT_IN_PARTY) {
            // The same word both ways. Asking somebody who is already with you
            // to come is the only thing it can sensibly mean, and it saves the
            // book needing a parting topic for every companion.
            gg_party_leave(g, g->talking_to);
            gg_log(g, "%s stays behind.", a->name);
        } else if (gg_party_join(g, g->talking_to)) {
            gg_log(g, "%s joins thee.", a->name);
        } else {
            gg_log(g, "Thou canst lead no more than %d.", GG_PARTY_MAX);
        }
    }
}

// ---------------------------------------------------------------------------
// The pack
// ---------------------------------------------------------------------------
int gg_pack_count(const gg_game *g, gg_item_id kind) {
    int n = 0;
    for (int i = 0; i < g->packn; i++)
        if (g->pack[i].kind == kind) n += g->pack[i].count;
    return n;
}

int gg_pack_find(const gg_game *g, gg_item_id kind) {
    for (int i = 0; i < g->packn; i++)
        if (g->pack[i].kind == kind) return i;
    return -1;
}

int gg_pack_weight(const gg_game *g) {
    int w = 0;
    for (int i = 0; i < g->packn; i++)
        w += GG_ITEM[g->pack[i].kind].weight * g->pack[i].count;
    return w;
}

int gg_pack_add(gg_game *g, gg_item_id kind, int count) {
    if (count <= 0 || kind >= GG_ITEM_COUNT) return 0;

    const gg_item_def *d = &GG_ITEM[kind];
    int taken = 0;

    while (taken < count) {
        // Weight first: it refuses the next one whatever the slots say.
        if (d->weight && gg_pack_weight(g) + d->weight > GG_CARRY_MAX) break;

        int slot = -1;
        if (d->stack) {
            for (int i = 0; i < g->packn; i++)
                if (g->pack[i].kind == kind && g->pack[i].count < 255) {
                    slot = i;
                    break;
                }
        }
        if (slot < 0) {
            if (g->packn >= GG_PACK_MAX) break;
            slot = g->packn++;
            g->pack[slot].kind = (uint8_t)kind;
            g->pack[slot].count = 0;
        }
        g->pack[slot].count++;
        taken++;
    }
    return taken;
}

int gg_pack_take(gg_game *g, int index, int count) {
    if (!gg_pack_slot_ok(g, index) || count <= 0) return 0;

    const int had = g->pack[index].count;
    const int gone = count < had ? count : had;
    g->pack[index].count = (uint8_t)(had - gone);
    if (g->pack[index].count > 0) return gone;

    // The slot is empty, so it goes. Anything held in it stops being held, and
    // the last slot moves down into the gap - which means every index above it
    // has to be repaired, or a held torch would silently become a held loaf.
    for (int s = 0; s < GG_SLOT_COUNT; s++)
        if (g->equipped[s] == index) g->equipped[s] = -1;

    const int last = g->packn - 1;
    g->pack[index] = g->pack[last];
    g->packn--;
    for (int s = 0; s < GG_SLOT_COUNT; s++)
        if (g->equipped[s] == last) g->equipped[s] = index;

    if (g->pack_cursor >= g->packn) g->pack_cursor = g->packn - 1;
    if (g->pack_cursor < 0) g->pack_cursor = 0;
    return gone;
}

int gg_light_radius(const gg_game *g) {
    const int held = g->equipped[GG_SLOT_LIGHT];
    if (gg_pack_slot_ok(g, held)) {
        const int r = GG_ITEM[g->pack[held].kind].light;
        if (r > 0) return r;
    }
    // Nothing held: an arm's length, so a player who drops their last torch is
    // in the dark rather than blind.
    return 1;
}

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
// What the pathfinder is allowed to know: the map, and who is standing where.
// Passed as a context rather than compiled in, so gg_path stays testable
// against a hand-drawn maze with no world at all.
typedef struct {
    const gg_game *g;
    int self;              // the actor being moved, which is not its own obstacle
} gg_walk_ctx;

static bool path_passable(void *vctx, int x, int y) {
    const gg_walk_ctx *c = vctx;
    if (!gg_map_walkable(&c->g->map, x, y)) return false;
    return !gg_actor_occupied(c->g->actor, c->g->actors, x, y, c->self);
}
static void world_turn(gg_game *g, int minutes) {
    g->turn++;
    g->minutes += (uint32_t)minutes;
    while (g->minutes >= GG_MINUTES_PER_DAY) {
        g->minutes -= GG_MINUTES_PER_DAY;
        g->day++;
    }

    // The party moves first, and in slot order, so slot 2 steps into the tile
    // slot 1 has just left rather than finding it occupied. Doing this after
    // the townsfolk would have the line shuffle one step per turn behind.
    for (int slot = 1; slot <= GG_PARTY_MAX; slot++) {
        const int i = gg_party_at(g, slot);
        if (i < 0) continue;
        gg_actor *a = &g->actor[i];

        // The Nth footprint back. Before there are enough footprints - just
        // recruited, or just loaded - the oldest one is the best there is.
        const int want = slot - 1 < g->trailn ? slot - 1 : g->trailn - 1;
        if (want < 0) continue;
        const int tx = g->trail_x[want], ty = g->trail_y[want];
        if (a->x == tx && a->y == ty) continue;

        gg_walk_ctx ctx = { .g = g, .self = i };
        int nx, ny;
        if (gg_path_next_step(&g->path, path_passable, &ctx,
                              a->x, a->y, tx, ty, GG_PATH_BUDGET, &nx, &ny) &&
            gg_map_walkable(&g->map, nx, ny) &&
            !gg_actor_occupied(g->actor, g->actors, nx, ny, i)) {
            gg_actor_move_to(a, nx, ny);
        } else {
            gg_actor_step_toward(a, &g->map, g->actor, g->actors, tx, ty, &g->rng);
        }
    }

    const int hour = gg_game_hour(g);
    for (int i = 0; i < g->actors; i++) {
        gg_actor *a = &g->actor[i];
        if (i == g->player || !a->active) continue;
        // Somebody walking with you has given up their day; they were moved
        // above, by the trail rather than by a schedule.
        if (a->party != GG_NOT_IN_PARTY) continue;

        int tx, ty;
        if (!gg_actor_target_at(a, hour, &tx, &ty)) continue;

        // Already where it should be: idle, with an occasional shuffle so a
        // town at rest is not a town of statues.
        if (a->x == tx && a->y == ty) {
            if (gg_rand_below(&g->rng, 12) == 0)
                a->facing = (uint8_t)gg_rand_below(&g->rng, 4);
            continue;
        }
        // A* first. Greedy stepping is kept as the fallback for the case the
        // search cannot help with at all - boxed in on every side - where a
        // random legal step is what unpicks a knot in a doorway.
        gg_walk_ctx ctx = { .g = g, .self = i };
        int nx, ny;
        if (gg_path_next_step(&g->path, path_passable, &ctx,
                              a->x, a->y, tx, ty, GG_PATH_BUDGET, &nx, &ny) &&
            gg_map_walkable(&g->map, nx, ny) &&
            !gg_actor_occupied(g->actor, g->actors, nx, ny, i)) {
            gg_actor_move_to(a, nx, ny);
        } else {
            gg_actor_step_toward(a, &g->map, g->actor, g->actors, tx, ty, &g->rng);
        }
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
            // Somebody walking with you steps aside rather than being talked
            // at. Without this the party can wall you into a doorway they
            // followed you through, which is the exact failure the plan's
            // verification is about.
            if (g->actor[i].party != GG_NOT_IN_PARTY) {
                gg_actor *c = &g->actor[i];
                const int cx = c->x, cy = c->y;
                const int px = p->x, py = p->y;
                gg_actor_move_to(c, px, py);
                gg_actor_move_to(p, cx, cy);

                // The footprint is the tile the *Avatar* left, exactly as an
                // ordinary step lays one. Pushing the tile they moved into
                // instead sent the companion chasing the Avatar's own square,
                // which it can never stand on - so it shuffled sideways every
                // time the two swapped.
                trail_push(g, px, py);
                world_turn(g, GG_MINUTES_PER_TURN);
                return;
            }
            begin_conversation(g, i);
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

    // The footprint is the tile being left, not the one being entered: the
    // person behind wants to stand where you were, not where you are.
    trail_push(g, p->x, p->y);
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
    begin_conversation(g, who);
}

// How a quantity of something reads in a sentence: "a loaf of bread", or
// "3 loaves of bread". One place, so every message agrees.
static void say_amount(char *out, size_t n, gg_item_id kind, int count) {
    const gg_item_def *d = &GG_ITEM[kind];
    if (count == 1) SDL_strlcpy(out, d->one, n);
    else            SDL_snprintf(out, n, "%d %s", count, d->many);
}

static void do_get(gg_game *g) {
    const gg_actor *p = gg_player_const(g);
    if (gg_ground_at(&g->map, p->x, p->y) < 0) {
        gg_log(g, "There is nothing here to take.");
        return;
    }

    // A tile may hold more than one kind - a house floor with bread on it and
    // a phial beside it - and gg_ground_at only ever finds the first. Taking
    // one and stopping would leave the rest of the tile unreachable for good,
    // so this clears what it can and only then gives up.
    int got = 0;
    char what[96];
    for (;;) {
        const int i = gg_ground_at(&g->map, p->x, p->y);
        if (i < 0) break;

        const gg_item_id kind = (gg_item_id)g->map.ground[i].kind;
        const int there = g->map.ground[i].count;
        const int taken = gg_pack_add(g, kind, there);

        if (taken == 0) {
            // Too heavy, or the pack is full. Say so about this pile and stop:
            // a lighter one underneath is not worth the message it would take
            // to explain, and the player can see what is left.
            say_amount(what, sizeof what, kind, there);
            gg_log(g, got ? "Thou canst carry no more; %s remains."
                          : "Thou canst not carry %s.", what);
            break;
        }

        say_amount(what, sizeof what, kind, taken);
        got++;
        if (taken < there) {
            g->map.ground[i].count = (uint8_t)(there - taken);
            gg_log(g, "Thou takest %s, and canst carry no more.", what);
            break;
        }
        gg_ground_remove(&g->map, i);
        gg_log(g, "Thou takest %s.", what);
    }

    if (got) world_turn(g, GG_MINUTES_PER_TURN);
}

static void do_drop(gg_game *g) {
    if (!gg_pack_slot_ok(g, g->pack_cursor)) {
        gg_log(g, "Thou carriest nothing to set down.");
        return;
    }
    const gg_actor *p = gg_player_const(g);
    const gg_item_id kind = (gg_item_id)g->pack[g->pack_cursor].kind;
    const int count = g->pack[g->pack_cursor].count;

    // The whole slot at once. Dropping one of a stack wants a number to be
    // typed, and there is nowhere to type it that a gamepad can reach.
    if (!gg_ground_drop(&g->map, p->x, p->y, kind, count)) {
        gg_log(g, "There is no room here to set that down.");
        return;
    }
    gg_pack_take(g, g->pack_cursor, count);

    char what[96];
    say_amount(what, sizeof what, kind, count);
    gg_log(g, "Thou settest down %s.", what);
    world_turn(g, GG_MINUTES_PER_TURN);
}

static void do_use(gg_game *g) {
    if (!gg_pack_slot_ok(g, g->pack_cursor)) {
        gg_log(g, "Thou carriest nothing to use.");
        return;
    }
    const gg_item_id kind = (gg_item_id)g->pack[g->pack_cursor].kind;
    const gg_item_def *d = &GG_ITEM[kind];

    if (d->use == GG_USE_NONE) {
        gg_log(g, "Thou canst think of nothing to do with %s.", d->one);
        return;
    }
    gg_actor *me = gg_player(g);
    if (me->hp >= me->hp_max) {
        gg_log(g, "Thou art already hale.");
        return;
    }

    const int before = me->hp;
    me->hp = (int16_t)gg_clampi(me->hp + d->heal, 0, me->hp_max);
    gg_pack_take(g, g->pack_cursor, 1);

    gg_log(g, d->use == GG_USE_EAT ? "Thou eatest %s, and art the better for it (+%d)."
                                   : "Thou drinkest %s, and art the better for it (+%d).",
           d->one, me->hp - before);
    world_turn(g, GG_MINUTES_PER_TURN);
}

static void do_equip(gg_game *g) {
    if (!gg_pack_slot_ok(g, g->pack_cursor)) {
        gg_log(g, "Thou carriest nothing to take up.");
        return;
    }
    const int here = g->pack_cursor;
    const gg_item_id kind = (gg_item_id)g->pack[here].kind;
    const gg_item_def *d = &GG_ITEM[kind];

    if (d->slot == GG_SLOT_NONE) {
        gg_log(g, "Thou canst not hold %s ready.", d->one);
        return;
    }
    if (g->equipped[d->slot] == here) {
        g->equipped[d->slot] = -1;
        gg_log(g, "Thou puttest away %s.", d->one);
    } else {
        g->equipped[d->slot] = here;
        gg_log(g, "Thou holdest %s.", d->one);
    }
    world_turn(g, GG_MINUTES_PER_TURN);
}

// The pack's cursor, moved with the same directions that walk the world. Only
// up and down do anything: it is a column.
static void pack_move(gg_game *g, int dy) {
    if (g->packn <= 0) return;
    g->pack_cursor = (g->pack_cursor + dy + g->packn) % g->packn;
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
    // In a conversation the directions run down the list of words this person
    // will answer to, and asking is the same button that started the talk.
    // Nothing here advances the world: a conversation costs no time, which is
    // Ultima's own rule and the reason you can afford to ask everything.
    if (g->mode == GG_MODE_CONVERSE) {
        int dx, dy;
        if (gg_action_delta(a, &dx, &dy)) {
            if (dy && g->askables > 0)
                g->ask_cursor = (g->ask_cursor + (dy > 0 ? 1 : -1) + g->askables)
                              % g->askables;
            return;
        }
        switch (a) {
        case GG_ACT_TALK: case GG_ACT_USE:  gg_conversation_ask(g); break;
        case GG_ACT_WAIT: case GG_ACT_OPEN:
        case GG_ACT_PACK:                   end_conversation(g); break;
        default: break;
        }
        return;
    }

    // The pack is open: the directions steer its cursor instead of the avatar,
    // and the verbs act on whatever the cursor is on. Time still passes when
    // something actually happens - eating is a turn - but scrolling is free.
    if (g->mode == GG_MODE_PACK) {
        int dx, dy;
        if (gg_action_delta(a, &dx, &dy)) {
            pack_move(g, dy);
            return;
        }
        // The pad's four face buttons carry world verbs, and in here they carry
        // the pack's - which is why each case takes two actions. A uses, Y
        // readies, X sets down, B closes: the same four shapes as the
        // keyboard's U, R, P and I, so neither device is the poor relation.
        switch (a) {
        case GG_ACT_USE:   case GG_ACT_TALK: do_use(g); break;
        case GG_ACT_EQUIP: case GG_ACT_OPEN: do_equip(g); break;
        case GG_ACT_DROP:  case GG_ACT_LOOK: do_drop(g); break;
        case GG_ACT_GET:   do_get(g); break;
        case GG_ACT_PACK:  case GG_ACT_WAIT: g->mode = GG_MODE_PLAY; break;
        default: break;
        }
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
    case GG_ACT_GET:  do_get(g); break;

    case GG_ACT_PACK:
        g->mode = GG_MODE_PACK;
        if (g->pack_cursor >= g->packn) g->pack_cursor = 0;
        break;

    // The three that need a chosen thing open the pack rather than refusing:
    // the player asked to use something, and the next question is which.
    case GG_ACT_USE:
    case GG_ACT_EQUIP:
    case GG_ACT_DROP:
        if (g->packn == 0) {
            gg_log(g, "Thou carriest nothing at all.");
            break;
        }
        g->mode = GG_MODE_PACK;
        if (g->pack_cursor >= g->packn) g->pack_cursor = 0;
        break;

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
        a->def = (uint8_t)i;
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
        // Stats of their own, so a companion is somebody the world can hurt
        // rather than a sprite that follows. Modest and uniform for now:
        // what makes them differ is a later item than what makes them exist.
        a->hp = a->hp_max = 18;
        a->level = 1;
        a->party = GG_NOT_IN_PARTY;

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

// Everything a new game needs once its map exists, however the map got there.
static bool finish_new_game(gg_game *g, const char *profile) {
    if (!gg_path_init(&g->path, g->map.w, g->map.h)) {
        SDL_Log("gigantima: could not allocate the pathfinder");
        return false;
    }

    SDL_strlcpy(g->profile, profile && *profile ? profile : "Avatar",
                sizeof g->profile);

    // Start at 8am on day one: a town already awake, so the schedules are
    // visibly doing something within the first few turns.
    g->minutes = 8 * 60;
    g->day = 1;
    g->exp = 0;

    // What the Avatar sets out with. Everything below goes through the same
    // gg_pack_add a picked-up thing does, so the starting kit obeys the weight
    // limit like anything else and cannot quietly exceed it.
    g->packn = 0;
    g->pack_cursor = 0;
    for (int s = 0; s < GG_SLOT_COUNT; s++) g->equipped[s] = -1;

    gg_pack_add(g, GG_ITEM_BREAD, 3);
    gg_pack_add(g, GG_ITEM_APPLE, 2);
    gg_pack_add(g, GG_ITEM_TORCH, 2);
    gg_pack_add(g, GG_ITEM_GOLD, 100);

    // The two words everybody starts with. Every other word in the book has to
    // be given by somebody, which is what makes asking around the point.
    g->knownn = 0;
    gg_learn(g, GG_WORD_NAME);
    gg_learn(g, GG_WORD_JOB);
    g->talking_to = -1;

    // The player is actor 0 so that `player` never has to be re-found.
    gg_actor *p = &g->actor[0];
    SDL_zerop(p);
    p->active = true;
    p->art = GG_ACTOR_AVATAR;
    p->def = GG_ACTOR_NO_DEF;
    p->facing = GG_FACE_DOWN;
    p->x = (int16_t)g->map.start_x;
    p->y = (int16_t)g->map.start_y;
    p->hp = p->hp_max = 30;
    p->level = 1;
    p->party = GG_NOT_IN_PARTY;

    // The first footprint, so a companion recruited before the Avatar has
    // taken a step still has somewhere to stand.
    g->trailn = 0;
    trail_push(g, p->x, p->y);
    SDL_strlcpy(p->name, g->profile, sizeof p->name);
    g->player = 0;
    g->actors = 1;

    place_townsfolk(g);

    g->mode = GG_MODE_PLAY;
    gg_log(g, "%s. Day %u, %s.", g->map.name, g->day, gg_game_place(g));
    gg_log(g, "Thou art the Avatar. Seek the vale's troubles.");
    return true;
}

void gg_game_rebind_actors(gg_game *g) {
    // Puts back what a save file cannot carry. The greeting is a pointer into
    // a static table, so it is rebuilt from the actor's `def` index rather
    // than written out - a pointer in a file is a pointer into the wrong
    // process.
    for (int i = 0; i < g->actors; i++) {
        gg_actor *a = &g->actor[i];
        a->greeting = (a->def < GG_COUNTOF(TOWNSFOLK)) ? TOWNSFOLK[a->def].greeting
                                                       : nullptr;
    }
}

bool gg_game_new(gg_game *g, uint32_t seed, const char *profile) {
    SDL_zerop(g);
    gg_rng_seed(&g->rng, seed);
    g->talking_to = -1;

    if (!gg_map_generate(&g->map, 192, 160, seed)) return false;
    return finish_new_game(g, profile);
}

bool gg_game_new_from_map(gg_game *g, const char *path, const char *profile) {
    SDL_zerop(g);
    g->talking_to = -1;

    if (!gg_map_load(&g->map, path)) return false;
    // The map carries the seed it was generated from, so a loaded world still
    // has a reproducible RNG for whatever the simulation decides afterwards.
    gg_rng_seed(&g->rng, g->map.seed);
    return finish_new_game(g, profile);
}

void gg_game_free(gg_game *g) {
    if (!g) return;
    gg_path_free(&g->path);
    gg_map_free(&g->map);
}
