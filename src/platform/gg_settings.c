// gg_settings.c - reading and writing the options file.
#include "platform/gg_settings.h"
#include "core/gg_replay.h"   // the action names, which the key lines use

void gg_settings_default_keys(gg_settings *s) {
    SDL_zeroa(s->key);
    SDL_zeroa(s->alt);

    // The arrows and the keypad first, then WASD as the second binding. The
    // keypad matters: it is the only layout that makes the four diagonals
    // reachable as single keys, which a grid game with eight-way movement
    // genuinely wants - so it takes the primary slot for the diagonals.
    s->key[GG_ACT_N]  = SDL_SCANCODE_UP;     s->alt[GG_ACT_N]  = SDL_SCANCODE_W;
    s->key[GG_ACT_S]  = SDL_SCANCODE_DOWN;   s->alt[GG_ACT_S]  = SDL_SCANCODE_S;
    s->key[GG_ACT_W]  = SDL_SCANCODE_LEFT;   s->alt[GG_ACT_W]  = SDL_SCANCODE_A;
    s->key[GG_ACT_E]  = SDL_SCANCODE_RIGHT;  s->alt[GG_ACT_E]  = SDL_SCANCODE_D;
    s->key[GG_ACT_NW] = SDL_SCANCODE_KP_7;
    s->key[GG_ACT_NE] = SDL_SCANCODE_KP_9;
    s->key[GG_ACT_SW] = SDL_SCANCODE_KP_1;
    s->key[GG_ACT_SE] = SDL_SCANCODE_KP_3;

    // Ultima's own verbs, less the ones WASD has already claimed: D is "walk
    // right" here, so dropping is P for "put down" and equipping is R for
    // "ready", which is what Ultima called it anyway.
    s->key[GG_ACT_WAIT]    = SDL_SCANCODE_SPACE;
    s->key[GG_ACT_TALK]    = SDL_SCANCODE_T;
    s->key[GG_ACT_LOOK]    = SDL_SCANCODE_L;
    s->key[GG_ACT_OPEN]    = SDL_SCANCODE_O;
    s->key[GG_ACT_GET]     = SDL_SCANCODE_G;
    s->key[GG_ACT_FIGHT]   = SDL_SCANCODE_F;
    s->key[GG_ACT_CAST]    = SDL_SCANCODE_C;
    s->key[GG_ACT_JOURNAL] = SDL_SCANCODE_J;
    // Z, because Ultima put the character summary behind Z and called it
    // ztats, and anybody who played it will try that key first.
    s->key[GG_ACT_SHEET]   = SDL_SCANCODE_Z;
    s->key[GG_ACT_PACK]    = SDL_SCANCODE_I;
    s->key[GG_ACT_USE]     = SDL_SCANCODE_U;
    s->key[GG_ACT_EQUIP]   = SDL_SCANCODE_R;
    s->key[GG_ACT_DROP]    = SDL_SCANCODE_P;

    // The keypad's own five, which has meant "wait" in every roguelike since
    // before there was a word for the genre.
    s->alt[GG_ACT_WAIT] = SDL_SCANCODE_KP_5;
}

void gg_settings_defaults(gg_settings *s) {
    SDL_zerop(s);
    s->scale = 1;
    s->fullscreen = false;
    s->rumble = true;
    s->music = 7;
    s->effects = 8;
    s->text_scale = 1;
    s->plain_colours = false;
    gg_settings_default_keys(s);
}

// Trims leading and trailing blanks in place, and returns the start.
static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *end = s + SDL_strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\r' || end[-1] == '\n'))
        *--end = '\0';
    return s;
}

static bool truthy(const char *v) {
    return SDL_strcasecmp(v, "yes") == 0 || SDL_strcasecmp(v, "true") == 0 ||
           SDL_strcmp(v, "1") == 0;
}

bool gg_settings_load(gg_settings *s, const char *path) {
    gg_settings_defaults(s);

    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io) return false;                    // a first run, not a failure

    Sint64 size = SDL_GetIOSize(io);
    if (size <= 0 || size > 64 * 1024) {      // a settings file is tiny
        SDL_CloseIO(io);
        return false;
    }
    char *buf = SDL_malloc((size_t)size + 1);
    if (!buf) { SDL_CloseIO(io); return false; }

    const size_t got = SDL_ReadIO(io, buf, (size_t)size);
    SDL_CloseIO(io);
    buf[got] = '\0';

    char *line = buf;
    while (line && *line) {
        char *next = SDL_strchr(line, '\n');
        if (next) *next++ = '\0';

        char *hash = SDL_strchr(line, '#');
        if (hash) *hash = '\0';

        char *eq = SDL_strchr(line, '=');
        if (eq) {
            *eq = '\0';
            const char *k = trim(line);
            const char *v = trim(eq + 1);

            if (SDL_strcmp(k, "scale") == 0)           s->scale = gg_clampi(SDL_atoi(v), 1, 4);
            else if (SDL_strcmp(k, "fullscreen") == 0) s->fullscreen = truthy(v);
            else if (SDL_strcmp(k, "rumble") == 0)     s->rumble = truthy(v);
            else if (SDL_strcmp(k, "music") == 0)      s->music = gg_clampi(SDL_atoi(v), 0, 10);
            else if (SDL_strcmp(k, "effects") == 0)    s->effects = gg_clampi(SDL_atoi(v), 0, 10);
            else if (SDL_strcmp(k, "text_scale") == 0)
                s->text_scale = gg_clampi(SDL_atoi(v), 1, 2);
            else if (SDL_strcmp(k, "plain_colours") == 0)
                s->plain_colours = truthy(v);
            else if (SDL_strcmp(k, "last_profile") == 0)
                SDL_strlcpy(s->last_profile, v, sizeof s->last_profile);
            else if (SDL_strncmp(k, "key.", 4) == 0 ||
                     SDL_strncmp(k, "alt.", 4) == 0) {
                // `key.TALK = T`. Both halves are names rather than numbers:
                // a settings file is a thing a player may open, and `key.TALK
                // = 23` tells them nothing they can act on.
                const gg_action a = gg_action_from_name(k + 4);
                if (a >= 0 && a < GG_ACT_COUNT) {
                    uint16_t *into = (k[0] == 'k' ? s->key : s->alt);
                    const SDL_Scancode sc = SDL_GetScancodeFromName(v);
                    into[a] = (uint16_t)(sc == SDL_SCANCODE_UNKNOWN ? 0 : sc);
                    if (sc == SDL_SCANCODE_UNKNOWN && SDL_strcasecmp(v, "none") != 0)
                        SDL_Log("gigantima: %s: there is no key called '%s'",
                                path, v);
                } else {
                    SDL_Log("gigantima: %s: there is no action called '%s'",
                            path, k + 4);
                }
            }
            // Anything else is left alone rather than complained about: a file
            // from a newer build should not produce a wall of warnings.
        }
        line = next;
    }
    SDL_free(buf);
    return true;
}

bool gg_settings_save(const gg_settings *s, const char *path) {
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) {
        SDL_Log("gigantima: cannot write %s: %s", path, SDL_GetError());
        return false;
    }

    char buf[4096];
    int n = SDL_snprintf(buf, sizeof buf,
        "# gigantima settings. Written by the game; safe to edit by hand.\n"
        "scale         = %d\n"
        "fullscreen    = %s\n"
        "rumble        = %s\n"
        "music         = %d\n"
        "effects       = %d\n"
        "text_scale    = %d\n"
        "plain_colours = %s\n"
        "last_profile  = %s\n"
        "\n"
        "# What each key does. The names on the right are SDL's own; the\n"
        "# positions are what is stored, so these are the same physical keys\n"
        "# on any layout. `none` unbinds one.\n",
        s->scale, s->fullscreen ? "yes" : "no", s->rumble ? "yes" : "no",
        s->music, s->effects, s->text_scale,
        s->plain_colours ? "yes" : "no", s->last_profile);

    for (int a = 1; a < GG_ACT_COUNT && n > 0; a++) {
        for (int alt = 0; alt < 2 && n > 0; alt++) {
            const uint16_t sc = alt ? s->alt[a] : s->key[a];
            const char *name = sc ? SDL_GetScancodeName((SDL_Scancode)sc) : nullptr;
            // Padded by hand rather than with "%-8s": SDL's own printf is a
            // small implementation and the left-justify flag is not something
            // to lean on in a file format.
            char label[16];
            SDL_snprintf(label, sizeof label, "%s.%s", alt ? "alt" : "key",
                         gg_action_name((gg_action)a));
            for (size_t pad = SDL_strlen(label); pad < 12; pad++) {
                label[pad] = ' ';
                label[pad + 1] = '\0';
            }
            const int wrote = SDL_snprintf(buf + n, sizeof buf - (size_t)n,
                                           "%s = %s\n", label,
                                           name && *name ? name : "none");
            if (wrote < 0 || (size_t)(n + wrote) >= sizeof buf) { n = -1; break; }
            n += wrote;
        }
    }

    const bool ok = n > 0 && SDL_WriteIO(io, buf, (size_t)n) == (size_t)n;
    SDL_CloseIO(io);
    if (!ok) SDL_Log("gigantima: short write on %s", path);
    return ok;
}
