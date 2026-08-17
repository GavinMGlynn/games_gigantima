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
#define GG_LINE_MAX        88   // one line of speech
#define GG_TOPIC_WORDS_MAX  3   // synonyms for one topic
#define GG_TOPIC_LINES_MAX  3
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
    char teach[GG_WORD_MAX];            // empty when it teaches nothing

    // Asking this takes the speaker into the party. In the file rather than in
    // C, so who can be recruited - and what they have to be asked - is content
    // like everything else they say.
    bool joins;
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
