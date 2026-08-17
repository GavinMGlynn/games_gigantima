// gg_settings.c - reading and writing the options file.
#include "platform/gg_settings.h"

void gg_settings_defaults(gg_settings *s) {
    SDL_zerop(s);
    s->scale = 1;
    s->fullscreen = false;
    s->rumble = true;
    s->music = 7;
    s->effects = 8;
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
            else if (SDL_strcmp(k, "last_profile") == 0)
                SDL_strlcpy(s->last_profile, v, sizeof s->last_profile);
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

    char buf[1024];
    const int n = SDL_snprintf(buf, sizeof buf,
        "# gigantima settings. Written by the game; safe to edit by hand.\n"
        "scale        = %d\n"
        "fullscreen   = %s\n"
        "rumble       = %s\n"
        "music        = %d\n"
        "effects      = %d\n"
        "last_profile = %s\n",
        s->scale, s->fullscreen ? "yes" : "no", s->rumble ? "yes" : "no",
        s->music, s->effects, s->last_profile);

    const bool ok = n > 0 && SDL_WriteIO(io, buf, (size_t)n) == (size_t)n;
    SDL_CloseIO(io);
    if (!ok) SDL_Log("gigantima: short write on %s", path);
    return ok;
}
