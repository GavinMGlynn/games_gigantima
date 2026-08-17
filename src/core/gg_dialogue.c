// gg_dialogue.c - reading the book of what people say.
//
// The format is lines of `keyword rest-of-line`, because the people who will
// write these files are writing prose. A binary format would need a tool before
// a single word could be put in anybody's mouth, and the tool is a later item
// than the conversation is.
//
//     # comments run to the end of the line
//     person Iolo
//     art    MERCHANT            <- which sprite; also means "place them"
//     at     06 -6 -4            <- at this hour, be this far from the square
//     greet  Hail, Avatar!
//     topic  job market            <- synonyms, first one is the label
//       say  I keep the stall by the square.
//       say  Ask of the CARAVAN if thou wouldst know why.
//       teach caravan              <- asking this hands over the word
//       joins                      <- asking this takes them with you
//     bye    Fare thee well.
//
// Indentation is decoration; it is stripped. What binds a `say` to a topic is
// that it follows one.
#include "core/gg_dialogue.h"

static gg_speaker g_speaker[GG_SPEAKERS_MAX];
static int        g_speakers;

void gg_dialogue_clear(void) {
    SDL_zeroa(g_speaker);
    g_speakers = 0;
}

int gg_dialogue_speakers(void) { return g_speakers; }

// ---------------------------------------------------------------------------
// Words
// ---------------------------------------------------------------------------
// Case-insensitive, because a keyword is something a person typed or picked and
// "Caravan" is the same question as "caravan".
static bool word_eq(const char *a, const char *b) {
    return SDL_strcasecmp(a, b) == 0;
}

const gg_topic *gg_speaker_topic(const gg_speaker *s, const char *word) {
    if (!s || !word || !*word) return nullptr;
    for (int i = 0; i < s->topics; i++)
        for (int w = 0; w < s->topic[i].words; w++)
            if (word_eq(s->topic[i].word[w], word)) return &s->topic[i];
    return nullptr;
}

const gg_speaker *gg_dialogue_speaker(int i) {
    return (i >= 0 && i < g_speakers) ? &g_speaker[i] : nullptr;
}

const gg_speaker *gg_dialogue_find(const char *name) {
    if (!name || !*name) return nullptr;
    for (int i = 0; i < g_speakers; i++)
        if (word_eq(g_speaker[i].name, name)) return &g_speaker[i];
    return nullptr;
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------
static char *skip_spaces(char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

// Splits a line into its leading keyword and the rest, both trimmed. Returns
// nullptr for a line with nothing on it.
static char *split_word(char *line, char **rest) {
    char *p = skip_spaces(line);

    // A comment is the whole line from `#`, so a stray hash in prose has to be
    // escaped by not being at the start. Simpler than quoting, and dialogue has
    // no use for a hash.
    char *hash = SDL_strchr(p, '#');
    if (hash) *hash = '\0';

    if (!*p) return nullptr;

    char *q = p;
    while (*q && *q != ' ' && *q != '\t') q++;
    if (*q) {
        *q = '\0';
        *rest = skip_spaces(q + 1);
    } else {
        *rest = q;      // points at the terminator: an empty remainder
    }

    // Trim the tail of the remainder, which otherwise carries the line ending
    // and any trailing spaces into the text people read.
    size_t n = SDL_strlen(*rest);
    while (n > 0 && ((*rest)[n - 1] == ' ' || (*rest)[n - 1] == '\t' ||
                     (*rest)[n - 1] == '\r' || (*rest)[n - 1] == '\n'))
        (*rest)[--n] = '\0';

    return p;
}

static void complain(const char *path, int line, const char *what) {
    SDL_Log("gigantima: %s:%d: %s", path, line, what);
}

bool gg_dialogue_load(const char *path) {
    gg_dialogue_clear();

    size_t size = 0;
    char *text = SDL_LoadFile(path, &size);
    if (!text) {
        SDL_Log("gigantima: cannot read %s: %s", path, SDL_GetError());
        return false;
    }

    gg_speaker *who = nullptr;
    gg_topic   *topic = nullptr;
    bool ok = true;
    int lineno = 0;

    char *cursor = text;
    while (ok && cursor && *cursor) {
        char *nl = SDL_strchr(cursor, '\n');
        if (nl) *nl = '\0';
        char *line = cursor;
        cursor = nl ? nl + 1 : nullptr;
        lineno++;

        char *rest = nullptr;
        char *key = split_word(line, &rest);
        if (!key) continue;

        if (word_eq(key, "person")) {
            if (!*rest) { complain(path, lineno, "person with no name"); ok = false; break; }
            if (g_speakers >= GG_SPEAKERS_MAX) {
                complain(path, lineno, "too many people in one book");
                ok = false;
                break;
            }
            who = &g_speaker[g_speakers++];
            topic = nullptr;
            SDL_strlcpy(who->name, rest, sizeof who->name);
            continue;
        }

        if (!who) {
            complain(path, lineno, "this line comes before any `person`");
            ok = false;
            break;
        }

        if (word_eq(key, "art")) {
            int art = -1;
            for (int i = 0; i < GG_ACTOR_COUNT; i++)
                if (word_eq(GG_ACTOR_ID_NAME[i], rest)) art = i;
            if (art < 0) {
                SDL_Log("gigantima: %s:%d: there is no art called '%s'",
                        path, lineno, rest);
                ok = false;
                break;
            }
            who->art = (uint8_t)art;
            who->lives = true;
            topic = nullptr;
        } else if (word_eq(key, "home")) {
            if (!*rest) {
                complain(path, lineno, "a home with no place");
                ok = false;
                break;
            }
            SDL_strlcpy(who->home, rest, sizeof who->home);
            topic = nullptr;
        } else if (word_eq(key, "at")) {
            if (who->schedn >= GG_SCHEDULE_MAX) {
                complain(path, lineno, "more hours than a day has room for");
                ok = false;
                break;
            }
            // `at HOUR DX DY`, all three required: a schedule entry missing its
            // place would put somebody at the town centre by accident.
            int v[3] = { 0, 0, 0 };
            char *p = rest;
            bool got = true;
            for (int k = 0; k < 3 && got; k++) {
                char *w = p;
                while (*p && *p != ' ' && *p != '\t') p++;
                if (w == p) { got = false; break; }
                const char save = *p;
                *p = '\0';
                const bool neg = (*w == '-');
                if (neg) w++;
                if (!*w) got = false;
                int n = 0;
                for (const char *d = w; *d && got; d++) {
                    if (*d < '0' || *d > '9') got = false;
                    else n = n * 10 + (*d - '0');
                }
                v[k] = neg ? -n : n;
                *p = save;
                p = skip_spaces(p);
            }
            if (!got || v[0] < 0 || v[0] > 23) {
                complain(path, lineno, "`at` wants an hour and two offsets");
                ok = false;
                break;
            }
            who->sched[who->schedn].hour = (uint8_t)v[0];
            who->sched[who->schedn].x = (int16_t)v[1];
            who->sched[who->schedn].y = (int16_t)v[2];
            who->schedn++;
            topic = nullptr;
        } else if (word_eq(key, "greet")) {
            SDL_strlcpy(who->greet, rest, sizeof who->greet);
            topic = nullptr;
        } else if (word_eq(key, "bye")) {
            SDL_strlcpy(who->bye, rest, sizeof who->bye);
            topic = nullptr;
        } else if (word_eq(key, "topic")) {
            if (who->topics >= GG_TOPICS_MAX) {
                complain(path, lineno, "too many topics for one person");
                ok = false;
                break;
            }
            topic = &who->topic[who->topics++];
            SDL_zerop(topic);

            // The rest of the line is one or more synonyms.
            char *w = rest;
            while (*w && topic->words < GG_TOPIC_WORDS_MAX) {
                char *end = w;
                while (*end && *end != ' ' && *end != '\t') end++;
                const char save = *end;
                *end = '\0';
                SDL_strlcpy(topic->word[topic->words++], w, GG_WORD_MAX);
                *end = save;
                w = skip_spaces(end);
            }
            if (topic->words == 0) {
                complain(path, lineno, "topic with no word to ask it by");
                ok = false;
                break;
            }
        } else if (word_eq(key, "say")) {
            if (!topic) {
                complain(path, lineno, "`say` outside any topic");
                ok = false;
                break;
            }
            if (topic->says >= GG_TOPIC_LINES_MAX) {
                complain(path, lineno, "too many lines in one answer");
                ok = false;
                break;
            }
            SDL_strlcpy(topic->say[topic->says++], rest, GG_LINE_MAX);
        } else if (word_eq(key, "joins")) {
            if (!topic) {
                complain(path, lineno, "`joins` outside any topic");
                ok = false;
                break;
            }
            topic->joins = true;
        } else if (word_eq(key, "teach")) {
            if (!topic) {
                complain(path, lineno, "`teach` outside any topic");
                ok = false;
                break;
            }
            SDL_strlcpy(topic->teach, rest, sizeof topic->teach);
        } else {
            SDL_Log("gigantima: %s:%d: no idea what `%s` means", path, lineno, key);
            ok = false;
            break;
        }
    }

    SDL_free(text);

    // Every person needs a greeting, or walking up to them says nothing at all
    // and the conversation looks broken rather than empty.
    for (int i = 0; ok && i < g_speakers; i++) {
        // Somebody the world is meant to place needs a day, or they stand on
        // the town square from dawn to dawn - which is what a schedule exists
        // to prevent.
        if (g_speaker[i].lives && g_speaker[i].schedn == 0) {
            SDL_Log("gigantima: %s: %s has a sprite but no day to keep",
                    path, g_speaker[i].name);
            ok = false;
        }
        // And somewhere to keep it. Their hours are offsets from a place's
        // centre, so a resident with no place is a person the world is told to
        // put down and not told where.
        if (g_speaker[i].lives && !g_speaker[i].home[0]) {
            SDL_Log("gigantima: %s: %s has a sprite but no home to live in",
                    path, g_speaker[i].name);
            ok = false;
        }
        if (!g_speaker[i].greet[0]) {
            SDL_Log("gigantima: %s: %s has no greeting", path, g_speaker[i].name);
            ok = false;
        }
        for (int t = 0; ok && t < g_speaker[i].topics; t++) {
            if (g_speaker[i].topic[t].says == 0) {
                SDL_Log("gigantima: %s: %s's topic `%s` has nothing to say",
                        path, g_speaker[i].name, g_speaker[i].topic[t].word[0]);
                ok = false;
            }
        }
    }

    // All or nothing. A book that half-loaded would put half a conversation in
    // somebody's mouth, which is worse than a silent town.
    if (!ok) {
        gg_dialogue_clear();
        return false;
    }

    SDL_Log("gigantima: %d people have something to say", g_speakers);
    return true;
}
