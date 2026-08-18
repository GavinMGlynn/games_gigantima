// gg_dialogue.h - what people say, and the words that unlock it.
//
// Ultima's conversation is a vocabulary, not a tree. You know some words; you
// say one to somebody; if they have something to say about it they say it, and
// what they say may teach you words you did not have. There is no branching
// script anywhere - the "branches" are which words you have collected.
//
// That gives the gating for free. A topic is askable exactly when you know one
// of its keywords, so a topic that teaches CARAVAN is the only thing that makes
// anyone's CARAVAN topic reachable - including a different person's, which is
// how a rumour travels across a town without a flag system to carry it.
//
// **This is data.** The game reads a file; nothing here is compiled in. The
// path comes from the caller, because src/core must not know where assets live.
#ifndef GG_DIALOGUE_H
#define GG_DIALOGUE_H

#include "core/gg_common.h"
#include "core/gg_ids.h"

#define GG_WORD_MAX        20   // a keyword, with its terminator

// What a thing costs, in gold coins: its value in the item table over the value
// of a coin, never less than one. A merchant sells at that and buys at half of
// it, which is the oldest arrangement there is and explains itself.
int gg_price_to_buy(gg_item_id kind);
int gg_price_to_sell(gg_item_id kind);
#define GG_LINE_MAX        88   // one line of speech
#define GG_TOPIC_WORDS_MAX  3   // synonyms for one topic
#define GG_TOPIC_LINES_MAX  3
#define GG_TOPIC_TEACH_MAX  4
// Per person. Somebody who teaches a language legitimately has a great many -
// the vale's mage answers to eight runes plus the ordinary courtesies.
#define GG_TOPICS_MAX      16
#define GG_SPEAKERS_MAX    16

// The words everybody knows before they have learned anything. Ultima's own
// two: they are what makes a first conversation possible at all.
#define GG_WORD_NAME "name"
#define GG_WORD_JOB  "job"

typedef struct {
    char word[GG_TOPIC_WORDS_MAX][GG_WORD_MAX];
    int  words;                         // the first is what the panel shows
    char say[GG_TOPIC_LINES_MAX][GG_LINE_MAX];
    int  says;
    // The words this hands over. Several, because a shopkeeper naming his
    // stock hands over three or four in one breath, and splitting that into
    // three topics nobody can ask yet is the tail wagging the dog.
    char teach[GG_TOPIC_TEACH_MAX][GG_WORD_MAX];
    int  teaches;

    // Asking this takes the speaker into the party. In the file rather than in
    // C, so who can be recruited - and what they have to be asked - is content
    // like everything else they say.
    bool joins;

    // What asking it hands over, and what it settles. A topic that wants
    // something can only be asked while it is in the pack, and asking it gives
    // it away - which is how a thing is handed to somebody rather than merely
    // carried past them. The flag is what the story watches for.
    uint8_t wants;                      // gg_item_id
    uint8_t wants_count;                // zero means it wants nothing

    // And what asking it hands *over*, which is how somebody arms you or pays
    // you. Given only when the pack holds none of that kind already, so a
    // topic asked twice is not a purse that never empties.
    uint8_t gives;                      // gg_item_id
    uint8_t gives_count;                // zero means it gives nothing

    // Money changes hands as well as goods. `trade` turns a `gives` into a
    // purchase and a `wants` into a sale, at prices taken from the item table
    // rather than written here - a price in the book is a price that drifts
    // from what the thing is worth the moment either is edited.
    bool    trade;

    char    raises[GG_FLAG_MAX];

    // And what it *orders*, for a topic asked of somebody walking with you.
    // A companion is a person, so an order is a thing you say to them in the
    // same conversation as everything else, and each one answers it in their
    // own words. `gg_stance` plus one; zero orders nothing.
    uint8_t orders;
} gg_topic;

typedef struct {
    char name[32];
    char greet[GG_LINE_MAX];
    char bye[GG_LINE_MAX];
    gg_topic topic[GG_TOPICS_MAX];
    int  topics;

    // Who they are, as well as what they say. The file that lists everybody by
    // name is the natural home for the rest of a person: one block, one person,
    // and adding a townsperson is an edit to one file rather than an edit to a
    // file and a table in C that had to agree with it.
    //
    // The schedule's x and y are OFFSETS FROM THE TOWN CENTRE, not tiles: the
    // generator puts the town somewhere different every seed, so an absolute
    // position would land these people in a field. An authored map records
    // absolute positions instead - see gg_map_actor.
    uint8_t art;                               // gg_actor_id
    gg_sched_entry sched[GG_SCHEDULE_MAX];
    int  schedn;
    bool lives;                                // has an art: the world places them

    // And what they are in a fight, for the ones who will come with you. Four
    // companions who differ in nothing are one companion with four names, so
    // this is what makes choosing between them a choice - and it belongs in the
    // book beside their hours and their words, because it is part of who they
    // are and not a table in C that has to agree with this file.
    //
    // Zero means "an ordinary townsperson", which is what everybody who is not
    // going anywhere with you stays. See gg_person_stats.
    int16_t health;
    uint8_t damage, guard, speed, reach;

    // The place they live, which is a region name a map may or may not have -
    // "Britain" for everyone in the vale. A map with a region of that name is
    // peopled by them, around its centre; a map without one has never heard of
    // them. That is what keeps the vale's eight out of the next town's square
    // and off the hillside at the standing stones.
    char home[GG_PLACE_MAX];
} gg_speaker;

// Loads the whole book. Held in one place rather than per game, because
// dialogue is content shared by every save, and only *which words you know* is
// state. Returns false and loads nothing if the file cannot be read or does not
// parse; a partly-loaded book would put half a conversation in somebody's mouth.
//
// Every complaint names the file and the line, because the author of a dialogue
// file is going to be somebody writing prose, not reading C.
bool gg_dialogue_load(const char *path);

// Throws the book away. Loading again does this first.
void gg_dialogue_clear(void);

// Who is loaded, for tests and for the editor.
int gg_dialogue_speakers(void);

// The i'th speaker, or nullptr. The world populates a generated town by walking
// these, so the book is the roll of who lives there as well as what they say.
const gg_speaker *gg_dialogue_speaker(int i);

// The speaker of that name, or nullptr. Names are matched exactly: a townsman
// with no entry simply has nothing to say beyond a greeting, which is a
// perfectly good state for a world still being written.
const gg_speaker *gg_dialogue_find(const char *name);

// The topic this speaker answers to `word`, or nullptr. Matches any synonym,
// and ignores case, because the word came from a player.
const gg_topic *gg_speaker_topic(const gg_speaker *s, const char *word);

#endif // GG_DIALOGUE_H
