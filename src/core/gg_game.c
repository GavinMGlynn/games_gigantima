// gg_game.c - the turn loop, the world clock, and the townsfolk who live by it.
#include "core/gg_game.h"
#include "core/gg_combat.h"
#include "core/gg_magic.h"
#include "core/gg_bestiary.h"
#include "core/gg_quest.h"

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

        // And they arrive able to keep up. Somebody recruited late would
        // otherwise be permanently behind the party they joined - which makes
        // "recruit everybody in the first ten minutes or not at all" the only
        // sensible play, and this game is about choosing who to bring.
        const int behind = gg_player_const(g)->level - g->actor[who].level;
        if (behind > 0) {
            gg_actor *a = &g->actor[who];
            a->level = gg_player_const(g)->level;
            a->hp_max = (int16_t)(a->hp_max + GG_LEVEL_HEALTH * behind);
            a->hp = (int16_t)(a->hp + GG_LEVEL_HEALTH * behind);
        }
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
// over by other topics. That one rule is what lets a rumour cross a town - the
// merchant hands over a word, and it is the gatekeeper and the old woman who
// have something to say about it - without anything in here knowing that a
// rumour exists, or who anybody is.
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
        // A topic that wants something can only be raised while it is in the
        // pack. Otherwise the player would be offered the word that hands over
        // the silver before they have any, and be told no by a line of speech.
        if (t->wants_count > 0 &&
            gg_pack_count(g, (gg_item_id)t->wants) < t->wants_count)
            continue;

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
        gg_emit(g, GG_EV_LEARN);
        gg_log(g, "Thou hast learned of %s.", t->teach);
        gg_conversation_refresh(g);
    }

    // Handing something over. The topic was only offered because the pack held
    // it, so this cannot fail - and the goods go before the flag is raised, so
    // a story that ends here ends with the silver on the counter.
    if (t->wants_count > 0) {
        int left = t->wants_count;
        while (left > 0) {
            const int slot = gg_pack_find(g, (gg_item_id)t->wants);
            if (slot < 0) break;
            const int took = gg_pack_take(g, slot, left);
            if (took <= 0) break;
            left -= took;
        }
        gg_emit(g, GG_EV_DROP);
        gg_log(g, "Thou dost hand over the %s.",
               t->wants_count > 1 ? GG_ITEM[t->wants].many
                                 : GG_ITEM[t->wants].one);
        gg_conversation_refresh(g);
    }

    // And what it hands over. Only when the pack holds none of that kind, so
    // asking twice is not a purse that never empties - the rule is in the
    // book's own documentation and this is the whole of it.
    if (t->gives_count > 0 && gg_pack_count(g, (gg_item_id)t->gives) == 0) {
        const int took = gg_pack_add(g, (gg_item_id)t->gives, t->gives_count);
        if (took > 0) {
            gg_emit(g, GG_EV_TAKE);
            gg_log(g, "Thou art given %s.", took > 1 ? GG_ITEM[t->gives].many
                                                     : GG_ITEM[t->gives].one);
        } else {
            gg_log(g, "Thou canst carry no more.");
        }
    }

    // And what it settles. A flag is how the story hears about a conversation:
    // the quests watch for it, and nothing here knows which quest cares.
    if (t->raises[0] && gg_raise_flag(g, t->raises)) gg_quests_tick(g);

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
// The story
//
// A quest is a number: how many of its stages have been entered. Everything
// else - what it is called, what each stage says, what has to be true to enter
// one - is read out of the book. Only the next stage is ever tested, so a quest
// cannot skip ahead however the world changes.
// ---------------------------------------------------------------------------
bool gg_flag(const gg_game *g, const char *name) {
    if (!name || !*name) return false;
    for (int i = 0; i < g->flags; i++)
        if (SDL_strcasecmp(g->flag[i], name) == 0) return true;
    return false;
}

bool gg_raise_flag(gg_game *g, const char *name) {
    if (!name || !*name) return false;
    if (gg_flag(g, name)) return false;
    if (g->flags >= GG_FLAGS_MAX) return false;
    SDL_strlcpy(g->flag[g->flags++], name, GG_FLAG_MAX);
    return true;
}

// Whether the world is as this stage requires.
// What a map is *called*, out of the path it was loaded from: the last
// component, without its extension.
//
// Without the extension because the same map has two forms - `vale.ggmap` and
// `vale.map.txt` - and they are the same place. The story says `when at vale`,
// a way out says it leads to `vale`, and which file that is on this machine is
// the frontend's business. Playing the text form of the shipped vale otherwise
// left the storyline unable to notice you had ever been anywhere.
static void place_of(const char *path, char *out, size_t n) {
    const char *slash = SDL_strrchr(path, '/');
    const char *back = SDL_strrchr(path, '\\');
    if (back && (!slash || back > slash)) slash = back;
    const char *leaf = slash ? slash + 1 : path;

    SDL_strlcpy(out, leaf, n);
    char *dot = SDL_strchr(out, '.');
    if (dot) *dot = '\0';
}

static bool stage_ready(const gg_game *g, const gg_stage *s) {
    switch (s->what) {
    case GG_WHEN_ALWAYS: return true;
    case GG_WHEN_KNOWS:  return gg_knows(g, s->word);
    case GG_WHEN_FLAG:   return gg_flag(g, s->word);
    case GG_WHEN_HAS:    return gg_pack_count(g, (gg_item_id)s->item) >= s->count;
    case GG_WHEN_SLAIN:  return (int)g->slain >= s->count;
    case GG_WHEN_PARTY:  return gg_party_size(g) >= s->count;
    case GG_WHEN_AT: {
        // Compared as *places*: a stage may name `vale` or `vale.ggmap` and
        // mean the same map, because a map's name is its file's without the
        // extension and content written before that was true still reads.
        char want[GG_MAP_NAME_MAX];
        place_of(s->where, want, sizeof want);
        if (SDL_strcasecmp(g->here, want) != 0) return false;
        if (s->radius == 0) return true;
        const gg_actor *p = gg_player_const(g);
        return gg_dist_cheb(p->x, p->y, s->wx, s->wy) <= s->radius;
    }
    default:             return false;
    }
}

void gg_quests_tick(gg_game *g) {
    for (int i = 0; i < gg_quests_count() && i < GG_QUESTS_MAX; i++) {
        const gg_quest *q = gg_quest_at(i);
        if (!q) continue;

        // As many stages as the world currently allows, but one at a time and
        // in order - a stage that is already true when the one before it is
        // entered follows on the same turn, which is what `when` with nothing
        // after it is for.
        for (int guard = 0; guard < GG_STAGES_MAX; guard++) {
            const int at = g->quest[i];
            if (at >= q->stages) break;
            if (!stage_ready(g, &q->stage[at])) break;

            g->quest[i] = (uint8_t)(at + 1);
            if (q->stage[at].sets[0]) gg_raise_flag(g, q->stage[at].sets);
            gg_emit(g, GG_EV_LEARN);
            gg_log(g, "%s: %s", q->name, q->stage[at].journal);
            gg_gain(g, q->stage[at].worth);

            // The end of the story. The world stops taking orders here, the
            // same way it does when the Avatar falls - and for the same
            // reason: what happens next is a screen, not a turn.
            if (q->stage[at].ends) {
                g->story_over = true;
                g->mode = GG_MODE_ENDING;
                return;
            }
        }
    }
}

int gg_journal_lines(const gg_game *g) {
    int n = 0;
    for (int i = 0; i < gg_quests_count() && i < GG_QUESTS_MAX; i++)
        n += g->quest[i];
    return n;
}

bool gg_journal_line(const gg_game *g, int i, const char **quest_name,
                     const char **text, bool *done) {
    if (i < 0) return false;
    int at = 0;
    for (int k = 0; k < gg_quests_count() && k < GG_QUESTS_MAX; k++) {
        const gg_quest *q = gg_quest_at(k);
        if (!q) continue;
        for (int s = 0; s < g->quest[k] && s < q->stages; s++) {
            if (at++ != i) continue;
            if (quest_name) *quest_name = q->name;
            if (text) *text = q->stage[s].journal;
            // Finished when this was its last stage and nothing follows.
            if (done) *done = (g->quest[k] >= q->stages && s == q->stages - 1);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Magic
//
// A spell is known when its runes are known. There is no spellbook to be
// given and no list to be added to - the words are the book, and they are the
// same words a conversation hands over, kept in the same place.
// ---------------------------------------------------------------------------
bool gg_spell_known(const gg_game *g, int spell) {
    const gg_spell *s = gg_magic_spell(spell);
    if (!s) return false;
    for (int i = 0; i < s->runes; i++)
        if (!gg_knows(g, s->rune[i])) return false;
    return true;
}

bool gg_spell_afford(const gg_game *g, int spell) {
    const gg_spell *s = gg_magic_spell(spell);
    if (!s) return false;
    for (int i = 0; i < s->reagents; i++)
        if (gg_pack_count(g, (gg_item_id)s->reagent[i]) < s->reagent_count[i])
            return false;
    return true;
}

int gg_spell_next(const gg_game *g, int from, int step) {
    const int n = gg_magic_spells();
    if (n <= 0) return -1;

    int at = from;
    for (int tries = 0; tries < n; tries++) {
        if (at >= 0 && at < n && gg_spell_known(g, at)) return at;
        at += step ? step : 1;
        if (at >= n) at = 0;
        if (at < 0)  at = n - 1;
    }
    return -1;
}

// Takes the price out of the pack. Called only once everything else has been
// checked, so it never has to put anything back.
static void spend_reagents(gg_game *g, const gg_spell *s) {
    for (int i = 0; i < s->reagents; i++) {
        int want = s->reagent_count[i];
        while (want > 0) {
            const int slot = gg_pack_find(g, (gg_item_id)s->reagent[i]);
            if (slot < 0) break;
            const int took = gg_pack_take(g, slot, want);
            if (took <= 0) break;
            want -= took;
        }
    }
}

bool gg_cast(gg_game *g, int spell) {
    const gg_spell *s = gg_magic_spell(spell);
    if (!s) return false;

    if (!gg_spell_known(g, spell)) {
        gg_log(g, "Thou knowest not the words of %s.", s->name);
        return false;
    }
    if (gg_player_const(g)->level < s->circle) {
        gg_log(g, "%s is of the %d circle, and thou art not.", s->name, s->circle);
        return false;
    }
    if (!gg_spell_afford(g, spell)) {
        // Which reagent is missing, because "thou hast not the reagents" sends
        // a player to count their pack by hand.
        for (int i = 0; i < s->reagents; i++)
            if (gg_pack_count(g, (gg_item_id)s->reagent[i]) < s->reagent_count[i]) {
                gg_log(g, "Thou hast not the %s for %s.",
                       GG_ITEM[s->reagent[i]].short_name, s->name);
                break;
            }
        return false;
    }

    // Harm needs something to aim at, and that is checked *before* anything is
    // spent - a spell that eats its reagents and fizzles is a spell nobody
    // casts twice.
    int target = -1;
    if (s->effect == GG_SPELL_HARM) {
        const gg_actor *p = gg_player_const(g);
        int best_d = 0;
        for (int i = 0; i < g->actors; i++) {
            if (!gg_at_odds(g, g->player, i)) continue;
            const int d = gg_dist_cheb(p->x, p->y, g->actor[i].x, g->actor[i].y);
            if (d > s->reach) continue;
            if (!gg_line_of_sight(g, p->x, p->y, g->actor[i].x, g->actor[i].y))
                continue;
            if (target < 0 || d < best_d) { target = i; best_d = d; }
        }
        if (target < 0) {
            gg_log(g, "There is nothing within reach of %s.", s->name);
            return false;
        }
    }

    spend_reagents(g, s);
    gg_emit(g, GG_EV_CAST);
    if (s->say[0]) gg_log(g, "%s", s->say);

    switch (s->effect) {
    case GG_SPELL_LIGHT:
        g->light_power = s->power;
        g->light_turns = s->turns;
        break;

    case GG_SPELL_HEAL: {
        gg_actor *me = gg_player(g);
        const int before = me->hp;
        me->hp = (int16_t)gg_clampi(me->hp + s->power, 0, me->hp_max);
        gg_log(g, "Thou art the better by %d.", me->hp - before);
        break;
    }

    case GG_SPELL_HARM: {
        gg_actor *foe = &g->actor[target];
        foe->hp = (int16_t)(foe->hp - s->power);
        gg_log(g, "%s takes %d.", foe->name, s->power);
        if (foe->hp <= 0) {
            // Through the ordinary killing, so what it was carrying falls the
            // same way it would from a hammer blow.
            foe->hp = 1;
            gg_strike(g, g->player, target);
            if (foe->hp > 0) { foe->hp = 0; gg_strike(g, g->player, target); }
        }
        break;
    }

    default:
        break;
    }
    return true;
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
    // An arm's length, so somebody who drops their last torch is in the dark
    // rather than blind.
    int best = 1;

    const int held = g->equipped[GG_SLOT_LIGHT];
    if (gg_pack_slot_ok(g, held)) {
        const int r = GG_ITEM[g->pack[held].kind].light;
        if (r > best) best = r;
    }
    // A spell of light is a second source, not a replacement: letting one lapse
    // must not put out the torch in your other hand.
    if (g->light_turns > 0 && g->light_power > best) best = g->light_power;
    return best;
}

// ---------------------------------------------------------------------------
// Things worth hearing
// ---------------------------------------------------------------------------
void gg_emit(gg_game *g, gg_event e) {
    if (e >= GG_EV_COUNT) return;
    if (g->events >= GG_EVENTS_MAX) return;
    g->event[g->events++] = (uint8_t)e;
}

int gg_events_drain(gg_game *g, gg_event *out, int max) {
    const int n = g->events < max ? g->events : max;
    for (int i = 0; i < n; i++) out[i] = (gg_event)g->event[i];
    g->events = 0;
    return n;
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

    // A spell of light burns down by the turn, like a torch would if torches
    // burned down. When it lapses it simply stops being the brightest thing.
    if (g->light_turns > 0 && --g->light_turns == 0) {
        g->light_power = 0;
        gg_log(g, "Thy light gutters and goes out.");
    }
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

        // A companion who has something at arm's length deals with it before
        // thinking about the line. Following into a fight and standing there
        // is the behaviour that makes a party feel like luggage.
        {
            int foe = -1;
            for (int k = 0; k < g->actors; k++)
                if (gg_at_odds(g, i, k) &&
                    gg_dist_cheb(a->x, a->y, g->actor[k].x, g->actor[k].y) <= 1)
                    foe = k;
            if (foe >= 0) { gg_strike(g, i, foe); continue; }
        }

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

    // Then whatever wants to kill you, in initiative order.
    gg_combat_turn(g);

    // And whether any of that moved the story on. Once a turn is enough: a
    // stage is only ever entered once, and nothing in the world changes
    // between turns.
    gg_quests_tick(g);

    const int hour = gg_game_hour(g);
    for (int i = 0; i < g->actors; i++) {
        gg_actor *a = &g->actor[i];
        if (i == g->player || !a->active) continue;
        // Somebody walking with you has given up their day; they were moved
        // above, by the trail rather than by a schedule. Anything hostile was
        // moved by gg_combat_turn, and keeps no schedule at all.
        if (a->party != GG_NOT_IN_PARTY || a->hostile) continue;

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
            // An enemy in the way is struck. Walking into somebody already
            // means "deal with this person"; which dealing it is depends on
            // whose side they are on, and nothing else.
            if (gg_at_odds(g, g->player, i)) {
                gg_strike(g, g->player, i);
                world_turn(g, GG_MINUTES_PER_TURN);
                return;
            }
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
        gg_emit(g, GG_EV_BUMP);
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

    // A way out. The simulation says where to; the frontend says where that
    // map lives, and calls gg_game_travel - src/core must not know where
    // content is kept on disk.
    const gg_portal *way = gg_portal_at(&g->map, nx, ny);
    if (way && way->to[0]) {
        SDL_strlcpy(g->travel_to, way->to, sizeof g->travel_to);
        g->travel_x = way->to_x;
        g->travel_y = way->to_y;
        g->want_travel = true;
    }
    gg_emit(g, (c && (c->flags & GG_CELL_DOOR)) ? GG_EV_DOOR : GG_EV_STEP);
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
        gg_emit(g, kind == GG_ITEM_GOLD ? GG_EV_COIN : GG_EV_TAKE);
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
    gg_emit(g, GG_EV_DROP);

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

    gg_emit(g, GG_EV_TAKE);
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

// Striking without stepping. At arm's length it hits whatever is in front;
// with something readied that has reach, it throws at the nearest thing it can
// see. Walking into an enemy does the first of those anyway, so this exists
// for the second - and for hitting something you would rather not walk into.
static void do_fight(gg_game *g) {
    const gg_actor *p = gg_player_const(g);
    static const int DX[4] = { 0, -1, 0, 1 };
    static const int DY[4] = { -1, 0, 1, 0 };

    // In front first, so a deliberate swing at somebody you are facing is
    // never turned into a throw at somebody else.
    const int fx = p->x + DX[p->facing], fy = p->y + DY[p->facing];
    for (int i = 0; i < g->actors; i++)
        if (g->actor[i].active && g->actor[i].x == fx && g->actor[i].y == fy &&
            gg_at_odds(g, g->player, i)) {
            gg_strike(g, g->player, i);
            world_turn(g, GG_MINUTES_PER_TURN);
            return;
        }

    // Then the nearest thing a readied throw can reach, if anything is.
    const int reach = gg_reach(g, g->player);
    if (reach > 1) {
        int best = -1, best_d = 0;
        for (int i = 0; i < g->actors; i++) {
            if (!gg_at_odds(g, g->player, i)) continue;
            const int d = gg_dist_cheb(p->x, p->y, g->actor[i].x, g->actor[i].y);
            if (d > reach || !gg_line_of_sight(g, p->x, p->y,
                                               g->actor[i].x, g->actor[i].y))
                continue;
            if (best < 0 || d < best_d) { best = i; best_d = d; }
        }
        if (best >= 0 && gg_throw_at(g, g->player, g->actor[best].x,
                                     g->actor[best].y)) {
            world_turn(g, GG_MINUTES_PER_TURN);
            return;
        }
    }

    gg_log(g, "There is nothing within reach to strike.");
}

static void do_open(gg_game *g) {
    gg_actor *p = gg_player(g);
    static const int DX[4] = { 0, -1, 0, 1 };
    static const int DY[4] = { -1, 0, 1, 0 };
    gg_cell *c = gg_map_at(&g->map, p->x + DX[p->facing], p->y + DY[p->facing]);
    if (c && (c->flags & GG_CELL_DOOR)) {
        gg_emit(g, GG_EV_DOOR);
        gg_log(g, "The door stands open.");
        world_turn(g, GG_MINUTES_PER_TURN);
    } else {
        gg_log(g, "Nothing there to open.");
    }
}

void gg_game_act(gg_game *g, gg_action a) {
    // A world that has ended takes no more orders. Both endings - the Avatar's
    // and the story's - are a screen rather than a turn, and a world that kept
    // taking steps after either would be a world where being slain is a state
    // you walk out of.
    if (g->mode == GG_MODE_GAMEOVER || g->mode == GG_MODE_ENDING) return;

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

    // The journal is open. Nothing in it can be acted on - it is a record, not
    // a menu - so the directions scroll and anything else closes it.
    if (g->mode == GG_MODE_JOURNAL) {
        int dx, dy;
        if (gg_action_delta(a, &dx, &dy)) {
            const int n = gg_journal_lines(g);
            if (dy && n > 0)
                g->journal_cursor = gg_clampi(g->journal_cursor + (dy > 0 ? 1 : -1),
                                              0, n - 1);
            return;
        }
        g->mode = GG_MODE_PLAY;
        return;
    }

    // The book is open: the directions run down it and choosing speaks. Only
    // spells whose runes are known are in it at all, so the list is the answer
    // to "what can I do", not a catalogue of what somebody else can.
    if (g->mode == GG_MODE_SPELL) {
        int dx, dy;
        if (gg_action_delta(a, &dx, &dy)) {
            if (dy) {
                const int step = dy > 0 ? 1 : -1;
                const int next = gg_spell_next(g, g->spell_cursor + step, step);
                if (next >= 0) g->spell_cursor = next;
            }
            return;
        }
        switch (a) {
        case GG_ACT_CAST: case GG_ACT_WAIT: case GG_ACT_OPEN:
            g->mode = GG_MODE_PLAY;
            break;
        case GG_ACT_USE: case GG_ACT_TALK: case GG_ACT_FIGHT:
            // Casting always costs the turn, whether or not it worked: the
            // words were spoken either way.
            if (gg_cast(g, g->spell_cursor)) g->mode = GG_MODE_PLAY;
            world_turn(g, GG_MINUTES_PER_TURN);
            break;
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
    case GG_ACT_FIGHT: do_fight(g); break;

    case GG_ACT_JOURNAL:
        g->mode = GG_MODE_JOURNAL;
        g->journal_cursor = 0;
        break;

    case GG_ACT_CAST: {
        const int first = gg_spell_next(g, g->spell_cursor, 1);
        if (first < 0) {
            gg_log(g, "Thou knowest no words of power at all.");
            break;
        }
        g->spell_cursor = first;
        g->mode = GG_MODE_SPELL;
        break;
    }

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
//
// Who lives in the generated town comes out of assets/dialogue.txt, which is
// also where everything they say lives - one block per person, so adding a
// townsperson is an edit to one file rather than an edit to a file and a table
// in C that had to agree with it by hand.
//
// Nothing in this file knows anybody's name.
// ---------------------------------------------------------------------------
// The centre of the region called `place`, or false if this map has no such
// place. Every resident's day is written as offsets from it.
static bool place_centre(const gg_game *g, const char *place, int *cx, int *cy) {
    for (int i = 0; i < g->map.regions; i++)
        if (SDL_strcasecmp(g->map.region[i].name, place) == 0) {
            *cx = g->map.region[i].x + g->map.region[i].w / 2;
            *cy = g->map.region[i].y + g->map.region[i].h / 2;
            return true;
        }
    return false;
}

static void place_townsfolk(gg_game *g) {

    for (int i = 0; i < gg_dialogue_speakers() && g->actors < GG_ACTORS_MAX; i++) {
        const gg_speaker *d = gg_dialogue_speaker(i);
        // Somebody in the book with no sprite is a voice, not a resident -
        // which is how a person can exist to be talked to in an authored map
        // without the generator putting a copy of them in every town.
        if (!d || !d->lives) continue;

        // Not if the map placed them itself. An authored map's version of
        // somebody is the authoritative one - it knows where their house is,
        // which the book's offsets from the square only approximate.
        bool already = false;
        for (int k = 0; k < g->actors; k++)
            if (SDL_strcmp(g->actor[k].name, d->name) == 0) already = true;
        if (already) continue;

        // And not unless this map is where they live.
        int cx = 0, cy = 0;
        if (!place_centre(g, d->home, &cx, &cy)) continue;

        gg_actor *a = &g->actor[g->actors];
        SDL_zerop(a);
        a->active = true;
        a->art = d->art;
        a->def = (uint8_t)i;
        a->greeting = d->greet;
        SDL_strlcpy(a->name, d->name, sizeof a->name);

        for (int k = 0; k < d->schedn && k < GG_SCHEDULE_MAX; k++) {
            int sx = gg_clampi(cx + d->sched[k].x, 1, g->map.w - 2);
            int sy = gg_clampi(cy + d->sched[k].y, 1, g->map.h - 2);
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
            a->sched[k].hour = d->sched[k].hour;
            a->sched[k].x = (int16_t)sx;
            a->sched[k].y = (int16_t)sy;
        }
        a->schedn = (uint8_t)d->schedn;

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

// Whoever lives in the map that is loaded: the people it names, and then the
// book's residents whose home is a place this map actually has.
//
// **A person lives somewhere by name.** The book says "home Britain" and gives
// their hours as offsets from it; a map with a region called Britain is where
// those hours mean something, and a map without one has never heard of them.
// Without that rule the first crossing brought eight townsfolk along and stood
// them on a hillside - and, later, moved the whole of Britain into the next
// town somebody authored.
//
// The map's own people come first and win: an authored map knows where
// somebody's house is, which the book's offsets only approximate.
static void populate_from_map(gg_game *g) {
    {
        for (int i = 0; i < g->map.actors && g->actors < GG_ACTORS_MAX; i++) {
            const gg_map_actor *m = &g->map.actor[i];
            gg_actor *a = &g->actor[g->actors++];
            SDL_zerop(a);
            a->active = true;
            a->art = m->art < GG_ACTOR_COUNT ? m->art : 0;
            a->def = GG_ACTOR_NO_DEF;
            a->facing = GG_FACE_DOWN;
            a->x = m->x;
            a->y = m->y;
            a->from_x = a->x;
            a->from_y = a->y;
            a->hp = a->hp_max = 18;
            a->level = 1;
            a->party = GG_NOT_IN_PARTY;
            SDL_strlcpy(a->name, m->name, sizeof a->name);
            a->schedn = m->schedn;
            for (int k = 0; k < m->schedn && k < GG_SCHEDULE_MAX; k++)
                a->sched[k] = m->sched[k];
        }
    }

    place_townsfolk(g);
}

// What haunts the map underfoot, out of the bestiary.
//
// `first_world` is true only for the map a game begins in, which is the one
// that gets the creatures with nowhere in particular to be. A creature that
// names a map is put in that map instead, the first time it is walked into -
// and a creature that names a tile as well stands on it, which is how a story
// puts its villain where the story says without a line of C knowing where that
// is.
//
// Called once per map per world: a map walked back into is remembered rather
// than re-read, so nothing is stocked twice.
static void stock_creatures(gg_game *g, bool first_world) {
    // Away from the town's own doorstep for the ones with nowhere to be: the
    // vale should be safe to leave a house in, and dangerous to walk out of.
    int cx = g->map.start_x, cy = g->map.start_y;
    for (int i = 0; i < g->map.regions; i++)
        if (g->map.region[i].kind == GG_REGION_TOWN) {
            cx = g->map.region[i].x + g->map.region[i].w / 2;
            cy = g->map.region[i].y + g->map.region[i].h / 2;
            break;
        }

    for (int kind = 0; kind < gg_bestiary_count(); kind++) {
        const gg_beast *b = gg_bestiary_at(kind);
        if (!b || b->haunts == 0) continue;

        const bool named = b->haunt_map[0] != '\0';
        if (named) {
            // As a place, not as a filename - the bestiary may say `stones` or
            // `stones.ggmap` and mean the same map. See place_of.
            char want[GG_MAP_NAME_MAX];
            place_of(b->haunt_map, want, sizeof want);
            if (SDL_strcasecmp(want, g->here) != 0) continue;
        } else if (!first_world) {
            continue;
        }

        // A tile of its own: exactly there, or the nearest place it fits.
        // Somewhere near where the story says beats nowhere at all.
        if (named && b->haunt_x >= 0) {
            bool placed = false;
            for (int r = 0; r < 12 && !placed; r++)
                for (int oy = -r; oy <= r && !placed; oy++)
                    for (int ox = -r; ox <= r && !placed; ox++)
                        if (gg_spawn_foe(g, kind, b->haunt_x + ox,
                                         b->haunt_y + oy) >= 0)
                            placed = true;
            if (!placed)
                SDL_Log("gigantima: nowhere to put %s at %d,%d in %s", b->id,
                        b->haunt_x, b->haunt_y, g->here);
            if (b->haunts <= 1) continue;
        }

        int placed = 0;
        for (int tries = 0; tries < 300 && placed < b->haunts; tries++) {
            const int x = gg_rand_belowi(&g->rng, g->map.w);
            const int y = gg_rand_belowi(&g->rng, g->map.h);
            if (!named && gg_dist_cheb(cx, cy, x, y) < 24) continue;
            const gg_cell *c = gg_map_at_const(&g->map, x, y);
            if (!c || (c->flags & GG_CELL_INDOORS)) continue;
            if (gg_spawn_foe(g, kind, x, y) >= 0) placed++;
        }
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

    populate_from_map(g);

    stock_creatures(g, true);

    g->mode = GG_MODE_PLAY;
    gg_log(g, "%s. Day %u, %s.", g->map.name, g->day, gg_game_place(g));
    gg_log(g, "Thou art the Avatar. Seek the vale's troubles.");
    return true;
}

gg_action gg_action_toward(int dx, int dy) {
    static const gg_action BY[3][3] = {
        { GG_ACT_NW, GG_ACT_N,    GG_ACT_NE },
        { GG_ACT_W,  GG_ACT_WAIT, GG_ACT_E  },
        { GG_ACT_SW, GG_ACT_S,    GG_ACT_SE },
    };
    return BY[gg_clampi(dy, -1, 1) + 1][gg_clampi(dx, -1, 1) + 1];
}

bool gg_step_toward(gg_game *g, int who, int tx, int ty, int *nx, int *ny) {
    if (who < 0 || who >= g->actors) return false;
    const gg_actor *a = &g->actor[who];
    if (a->x == tx && a->y == ty) return false;

    gg_walk_ctx ctx = { .g = g, .self = who };
    return gg_path_next_step(&g->path, path_passable, &ctx, a->x, a->y, tx, ty,
                             GG_PATH_BUDGET, nx, ny);
}

int gg_gain(gg_game *g, int worth) {
    if (worth <= 0) return 0;
    g->exp += worth;

    // The Avatar's level is the party's level: everybody rises together, and
    // the Avatar is the one the thresholds are read against.
    int rose = 0;
    gg_actor *me = gg_player(g);
    while (me->level < GG_LEVEL_MAX && g->exp >= gg_level_cost(me->level + 1)) {
        me->level++;
        rose++;
    }
    if (rose == 0) return 0;

    // What a level buys, for everybody walking: a body that takes more, and
    // the difference healed rather than left as a gap. The heal is the point
    // at which a player notices - a level that only changed a number would be
    // a number.
    for (int i = 0; i < g->actors; i++) {
        gg_actor *a = &g->actor[i];
        if (!a->active) continue;
        if (i != g->player && a->party == GG_NOT_IN_PARTY) continue;

        a->level = me->level;
        const int add = GG_LEVEL_HEALTH * rose;
        a->hp_max = (int16_t)(a->hp_max + add);
        a->hp = (int16_t)gg_clampi(a->hp + add, 0, a->hp_max);
    }

    gg_emit(g, GG_EV_LEVEL);
    gg_log(g, rose > 1 ? "Thou art level %d, and stronger for it."
                       : "Thou art level %d.", me->level);
    return rose;
}

bool gg_ending(const gg_game *g, const char **quest, const char **words) {
    if (!g->story_over) return false;
    for (int i = 0; i < gg_quests_count() && i < GG_QUESTS_MAX; i++) {
        const gg_quest *q = gg_quest_at(i);
        if (!q || g->quest[i] < 1) continue;
        const gg_stage *last = &q->stage[g->quest[i] - 1];
        if (!last->ends) continue;
        if (quest) *quest = q->name;
        if (words) *words = last->journal;
        return true;
    }
    // The story is over and the book no longer says so - somebody edited
    // quests.txt between one session and the next. A world that ended is still
    // a world that ended.
    if (quest) *quest = "";
    if (words) *words = "";
    return true;
}

void gg_game_rebind_actors(gg_game *g) {
    // Puts back what a save file cannot carry. The greeting is a pointer into
    // the loaded book, so it is rebuilt rather than written out - a pointer in
    // a file is a pointer into the wrong process.
    //
    // Found by name rather than by the index the save records, because the book
    // is a text file somebody may have edited between saves and an index into
    // it is a promise it never made. A name that is no longer in the book
    // simply comes back mute, which is a world where somebody moved away.
    for (int i = 0; i < g->actors; i++) {
        gg_actor *a = &g->actor[i];
        const gg_speaker *s = gg_dialogue_find(a->name);
        a->greeting = s ? s->greet : nullptr;
    }

    // And everybody waiting in a map that is remembered but not underfoot -
    // they walk back into the world without passing through a loader again.
    for (int m = 0; m < g->visiteds; m++)
        for (int i = 0; i < g->visited[m].whos; i++) {
            gg_actor *a = &g->visited[m].who[i];
            const gg_speaker *s = gg_dialogue_find(a->name);
            a->greeting = s ? s->greet : nullptr;
        }
}

bool gg_game_new(gg_game *g, uint32_t seed, const char *profile) {
    SDL_zerop(g);
    gg_rng_seed(&g->rng, seed);
    g->talking_to = -1;
    g->generated = true;

    if (!gg_map_generate(&g->map, 192, 160, seed)) return false;
    return finish_new_game(g, profile);
}

bool gg_game_new_from_map(gg_game *g, const char *path, const char *profile,
                          uint32_t seed) {
    SDL_zerop(g);
    g->talking_to = -1;

    if (!gg_map_load(&g->map, path)) return false;
    // What this map is called, so leaving and coming back knows which it was.
    place_of(path, g->here, sizeof g->here);
    // Seeded from what the caller was given, not from the map. The map's own
    // seed is what its *terrain* was generated from - zero for a map drawn by
    // hand - and using it made every journey through the vale identical.
    //
    // The map keeps the seed of the world built on it, so a save carries it and
    // a bug report can name it.
    gg_rng_seed(&g->rng, seed);
    g->map.seed = seed;
    return finish_new_game(g, profile);
}

// Where a map is kept in mind, or -1.
static int visited_index(const gg_game *g, const char *leaf) {
    for (int i = 0; i < g->visiteds; i++)
        if (SDL_strcasecmp(g->visited[i].leaf, leaf) == 0) return i;
    return -1;
}

static void visited_free(gg_visited *v) {
    gg_map_free(&v->map);
    SDL_free(v->who);
    SDL_zerop(v);
}

// Puts the map now under the Avatar's feet away, as it is. Ownership of its
// cells and of the people standing in it moves into `visited`; the caller must
// not free them afterwards.
static void stash_here(gg_game *g) {
    if (!g->here[0] || !g->map.cell) return;
    if (!g->visited) {
        g->visited = SDL_calloc(GG_VISITED_MAX, sizeof *g->visited);
        if (!g->visited) return;         // no room to remember; re-read instead
        g->visiteds = 0;
    }

    // Everybody still standing here: not the Avatar, not walking away with
    // them, and still alive. Whoever fell is simply not written down, which is
    // the whole of "who fell on it stays fallen".
    int staying = 0;
    for (int i = 0; i < g->actors; i++)
        if (i != g->player && g->actor[i].active &&
            g->actor[i].party == GG_NOT_IN_PARTY) staying++;

    gg_actor *who = nullptr;
    if (staying > 0) {
        who = SDL_calloc((size_t)staying, sizeof *who);
        if (!who) return;                // as above: forget rather than lie
        int n = 0;
        for (int i = 0; i < g->actors && n < staying; i++) {
            if (i == g->player || !g->actor[i].active ||
                g->actor[i].party != GG_NOT_IN_PARTY) continue;
            who[n] = g->actor[i];
            // Mid-stride is not a state to keep: the slide belongs to the
            // frame it was drawn in, and this map will not be drawn again for
            // a while. They arrive standing on the tile they were walking to.
            who[n].from_x = who[n].x;
            who[n].from_y = who[n].y;
            who[n].step = 0;
            n++;
        }
    }

    // The map's own placements have been superseded by that list, and leaving
    // them would put a second copy of everybody on the floor on the way back.
    g->map.actors = 0;

    int at = visited_index(g, g->here);
    if (at < 0) {
        if (g->visiteds >= GG_VISITED_MAX) {
            // Full. The oldest is let go, which is the one least likely to be
            // walked back into - and losing it costs a re-read, not a crash.
            visited_free(&g->visited[0]);
            for (int i = 1; i < g->visiteds; i++) g->visited[i - 1] = g->visited[i];
            g->visiteds--;
        }
        at = g->visiteds++;
    } else {
        visited_free(&g->visited[at]);
    }
    SDL_strlcpy(g->visited[at].leaf, g->here, GG_MAP_NAME_MAX);
    g->visited[at].map = g->map;
    g->visited[at].who = who;
    g->visited[at].whos = staying;
    SDL_zero(g->map);                    // ownership has moved
}

bool gg_game_travel(gg_game *g, const char *path, int x, int y) {
    char leaf[GG_MAP_NAME_MAX];
    place_of(path, leaf, sizeof leaf);

    // A map already in mind is taken back out rather than re-read, which is
    // what makes a return journey find things as they were left.
    gg_visited taken;
    SDL_zero(taken);
    const int known = visited_index(g, leaf);
    if (known >= 0) {
        taken = g->visited[known];
        for (int i = known + 1; i < g->visiteds; i++)
            g->visited[i - 1] = g->visited[i];
        g->visiteds--;
    } else if (!gg_map_load(&taken.map, path)) {
        gg_log(g, "The way is shut.");
        g->want_travel = false;
        return false;
    }

    // Who comes along: the Avatar and anybody walking with them, in order, so
    // the line arrives in the same order it left.
    gg_actor going[1 + GG_PARTY_MAX];
    int goings = 0;
    going[goings++] = g->actor[g->player];
    for (int slot = 1; slot <= GG_PARTY_MAX; slot++) {
        const int who = gg_party_at(g, slot);
        if (who >= 0) going[goings++] = g->actor[who];
    }

    // The world is replaced; everything about the party is not. The pack, the
    // words, the quests, the flags, the clock and the RNG all live on the game
    // rather than on the map, so they simply are not touched here.
    //
    // The map being left is put away rather than freed, so walking back into
    // it finds it as it was left.
    stash_here(g);
    gg_map_free(&g->map);
    g->map = taken.map;
    SDL_zero(taken.map);
    SDL_strlcpy(g->here, leaf, sizeof g->here);
    g->actors = 0;
    g->talking_to = -1;
    g->speaker = nullptr;
    g->saids = 0;
    g->askables = 0;
    g->mode = GG_MODE_PLAY;

    // The pathfinder is sized to the map, so a map of another size needs a new
    // one. Freed first, or travelling leaks one per journey.
    gg_path_free(&g->path);
    if (!gg_path_init(&g->path, g->map.w, g->map.h)) {
        SDL_Log("gigantima: could not allocate the pathfinder");
        return false;
    }

    // The Avatar first, so g->player stays 0 as everything else assumes.
    g->player = 0;
    g->actor[0] = going[0];
    g->actor[0].x = (int16_t)gg_clampi(x, 0, g->map.w - 1);
    g->actor[0].y = (int16_t)gg_clampi(y, 0, g->map.h - 1);
    g->actor[0].from_x = g->actor[0].x;
    g->actor[0].from_y = g->actor[0].y;
    g->actor[0].step = 0;
    g->actors = 1;

    // The party, put down on the nearest clear ground - they cannot all stand
    // on the tile the Avatar arrived on.
    for (int i = 1; i < goings; i++) {
        gg_actor *a = &g->actor[g->actors];
        *a = going[i];
        a->step = 0;

        bool placed = false;
        for (int r = 1; r < 8 && !placed; r++)
            for (int oy = -r; oy <= r && !placed; oy++)
                for (int ox = -r; ox <= r && !placed; ox++) {
                    const int nx = g->actor[0].x + ox, ny = g->actor[0].y + oy;
                    if (!gg_map_walkable(&g->map, nx, ny)) continue;
                    if (gg_actor_occupied(g->actor, g->actors, nx, ny, -1)) continue;
                    a->x = (int16_t)nx;
                    a->y = (int16_t)ny;
                    placed = true;
                }
        // Nowhere to stand is not a reason to lose somebody: they arrive on
        // top of the Avatar and walk off on the next turn.
        if (!placed) { a->x = g->actor[0].x; a->y = g->actor[0].y; }
        a->from_x = a->x;
        a->from_y = a->y;
        g->actors++;
    }

    // The footprints are from a map that is no longer under anybody's feet.
    g->trailn = 0;
    trail_push(g, g->actor[0].x, g->actor[0].y);

    // And whoever is here. A map walked in before hands back the people who
    // were standing in it when it was left, wounds and all; one being seen for
    // the first time hands back whoever the map says lives there.
    // Remembered, or new. A map walked in before hands back exactly the people
    // it was left with - including nobody, if they all fell - and one being
    // seen for the first time is peopled and stocked.
    if (known >= 0) {
        for (int i = 0; i < taken.whos && g->actors < GG_ACTORS_MAX; i++)
            g->actor[g->actors++] = taken.who[i];
    } else {
        populate_from_map(g);
        stock_creatures(g, false);
    }
    SDL_free(taken.who);

    g->want_travel = false;
    g->travel_to[0] = '\0';
    gg_log(g, "%s.", g->map.name[0] ? g->map.name : "Somewhere else");
    return true;
}

void gg_game_free(gg_game *g) {
    if (!g) return;
    gg_path_free(&g->path);
    gg_map_free(&g->map);
    if (g->visited) {
        for (int i = 0; i < g->visiteds; i++) visited_free(&g->visited[i]);
        SDL_free(g->visited);
        g->visited = nullptr;
        g->visiteds = 0;
    }
}
