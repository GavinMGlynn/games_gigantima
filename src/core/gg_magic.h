// gg_magic.h - runes, the spells made of them, and what they cost.
//
// Ultima's magic is a *language*. A spell is not an entry in a list you were
// given; it is a phrase - IN LOR, VAS FLAM - and you can cast it when you know
// the words it is made of. So the runes live in exactly the same vocabulary the
// conversation system uses, and are learned the same way: somebody tells you
// one. The mage teaching a rune is the same mechanism as the merchant teaching
// a rumour, and nothing here needed a second one.
//
// The reagents are the other half of the price. They are ordinary items, spent
// out of the ordinary pack, so a spell you cannot afford is a spell whose herbs
// you have not gone and found.
//
// **This is data.** `assets/spells.txt` holds every rune, every spell, its
// price and its effect. Adding one is an edit to a text file.
#ifndef GG_MAGIC_H
#define GG_MAGIC_H

#include "core/gg_common.h"
#include "core/gg_ids.h"
#include "core/gg_dialogue.h"      // GG_WORD_MAX, GG_LINE_MAX

#define GG_SPELL_RUNES_MAX     3   // runes in one phrase
#define GG_SPELL_REAGENTS_MAX  3
#define GG_SPELLS_MAX         16
#define GG_RUNES_MAX          16

// What a spell does. Each one has to be observable in the world, or it is a
// control that does nothing - see docs/PROJECT_STATUS.md.
typedef enum {
    GG_SPELL_NONE,
    GG_SPELL_LIGHT,     // a light of your own, for a while
    GG_SPELL_HEAL,      // health back
    GG_SPELL_HARM,      // damage at a distance
    GG_SPELL_EFFECTS
} gg_spell_effect;

typedef struct {
    char word[GG_WORD_MAX];         // the rune: AN, IN, LOR...
    char meaning[GG_WORD_MAX];      // what it means: negate, create, light
} gg_rune;

typedef struct {
    char name[32];                              // "Great Light"
    char rune[GG_SPELL_RUNES_MAX][GG_WORD_MAX]; // the phrase, in order
    int  runes;
    uint8_t circle;                             // how deep a mage it wants

    uint8_t reagent[GG_SPELL_REAGENTS_MAX];     // gg_item_id
    uint8_t reagent_count[GG_SPELL_REAGENTS_MAX];
    int     reagents;

    uint8_t effect;                             // gg_spell_effect
    int     power;                              // how much, in that effect's units
    int     turns;                              // how long, where that means anything
    int     reach;                              // how far, where that means anything
    char    say[GG_LINE_MAX];                   // what it reads like when it works
} gg_spell;

// Loads the book of spells. All or nothing, and every complaint names the file
// and the line - the author of one of these is writing incantations, not C.
bool gg_magic_load(const char *path);
void gg_magic_clear(void);

int  gg_magic_spells(void);
int  gg_magic_runes(void);
const gg_spell *gg_magic_spell(int i);
const gg_rune  *gg_magic_rune(int i);

// The rune of that word, or nullptr. Case-insensitive: it came from a person.
const gg_rune *gg_magic_find_rune(const char *word);

#endif // GG_MAGIC_H
