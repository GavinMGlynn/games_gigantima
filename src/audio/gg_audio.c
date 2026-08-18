// gg_audio.c - one stream, mixed here.
#include "audio/gg_audio.h"

#include <SDL3/SDL.h>

// Everything is converted to this on load, so the mixer never has to think
// about rates or channel counts. Mono, because nothing in this game is to one
// side of anything.
#define MIX_RATE   22050
#define MIX_FORMAT SDL_AUDIO_S16

typedef struct {
    int16_t *pcm;     // MIX_RATE mono
    int      frames;
} gg_clip;

typedef struct {
    const gg_clip *clip;
    int at;                 // frames played
} gg_voice;

static SDL_AudioStream *g_out;
static bool  g_ready;
static int   g_music_vol = 7;      // 0..10
static int   g_fx_vol    = 7;

static gg_clip g_fx[GG_EV_COUNT];

// The tunes, and the order gg_audio_tune_for indexes them by.
static const char *const TUNE_NAME[] = {
    "wild_day", "wild_night", "town_day", "town_night", "dungeon",
};
#define TUNES ((int)SDL_arraysize(TUNE_NAME))
static gg_clip g_music[TUNES];

static gg_voice g_voice[GG_VOICES];

// The music, and the one it is fading into. Two rather than one so a change of
// region or of hour is a crossfade instead of a cut.
static int g_tune = -1, g_next_tune = -1;
static int g_music_at, g_next_at;
static int g_fade;                  // frames of crossfade remaining
#define FADE_FRAMES (MIX_RATE / 2)  // half a second

// The file each effect comes from. Kept beside the enum so a missing case is a
// missing file rather than a silent nothing.
static const char *const FX_FILE[GG_EV_COUNT] = {
    [GG_EV_STEP]  = "fx_step.wav",
    [GG_EV_BUMP]  = "fx_bump.wav",
    [GG_EV_BLOW]  = "fx_blow.wav",
    [GG_EV_HURT]  = "fx_hurt.wav",
    [GG_EV_DIE]   = "fx_die.wav",
    [GG_EV_TAKE]  = "fx_take.wav",
    [GG_EV_DROP]  = "fx_drop.wav",
    [GG_EV_COIN]  = "fx_coin.wav",
    [GG_EV_DOOR]  = "fx_door.wav",
    [GG_EV_CAST]  = "fx_cast.wav",
    [GG_EV_LEARN] = "fx_learn.wav",
    [GG_EV_LEVEL] = "fx_level.wav",
};

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------
// Reads a WAV and converts it to the mix format, so nothing downstream has to
// care that the tunes were baked at half the rate of the effects.
static bool load_clip(const char *dir, const char *name, gg_clip *out) {
    char path[1024];
    SDL_snprintf(path, sizeof path, "%s%s", dir, name);

    SDL_AudioSpec spec;
    uint8_t *buf = nullptr;
    uint32_t len = 0;
    if (!SDL_LoadWAV(path, &spec, &buf, &len)) {
        SDL_Log("gigantima: cannot read %s: %s", path, SDL_GetError());
        return false;
    }

    const SDL_AudioSpec want = { .format = MIX_FORMAT, .channels = 1,
                                 .freq = MIX_RATE };
    uint8_t *conv = nullptr;
    int conv_len = 0;
    const bool ok = SDL_ConvertAudioSamples(&spec, buf, (int)len,
                                            &want, &conv, &conv_len);
    SDL_free(buf);
    if (!ok) {
        SDL_Log("gigantima: cannot convert %s: %s", path, SDL_GetError());
        return false;
    }

    out->pcm = (int16_t *)conv;
    out->frames = conv_len / (int)sizeof(int16_t);
    return true;
}

static void free_clip(gg_clip *c) {
    SDL_free(c->pcm);
    c->pcm = nullptr;
    c->frames = 0;
}

// ---------------------------------------------------------------------------
// Mixing
// ---------------------------------------------------------------------------
// One frame of music, following the loop and the crossfade. Returns the sample
// scaled by the music volume.
static int music_frame(void) {
    if (g_music_vol <= 0) return 0;

    int v = 0;
    if (g_tune >= 0 && g_music[g_tune].frames > 0) {
        const gg_clip *c = &g_music[g_tune];
        int s = c->pcm[g_music_at];
        if (g_fade > 0) s = s * g_fade / FADE_FRAMES;   // fading out
        v += s;
        if (++g_music_at >= c->frames) g_music_at = 0;
    }
    if (g_next_tune >= 0 && g_music[g_next_tune].frames > 0) {
        const gg_clip *c = &g_music[g_next_tune];
        int s = c->pcm[g_next_at];
        s = s * (FADE_FRAMES - g_fade) / FADE_FRAMES;   // fading in
        v += s;
        if (++g_next_at >= c->frames) g_next_at = 0;
    }

    if (g_fade > 0 && --g_fade == 0) {
        // The crossfade is over: what was arriving is now simply the music.
        g_tune = g_next_tune;
        g_music_at = g_next_at;
        g_next_tune = -1;
    }
    return v * g_music_vol / 10;
}

static void mix_into(int16_t *out, int frames) {
    for (int i = 0; i < frames; i++) {
        int v = music_frame();

        if (g_fx_vol > 0) {
            for (int k = 0; k < GG_VOICES; k++) {
                gg_voice *voice = &g_voice[k];
                if (!voice->clip) continue;
                v += voice->clip->pcm[voice->at] * g_fx_vol / 10;
                if (++voice->at >= voice->clip->frames) voice->clip = nullptr;
            }
        }

        // Clipped rather than wrapped: a wrap is a bang, and the whole point of
        // baking below full scale was to make this rare.
        out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

// SDL asks for more; we give it exactly what it asked for.
static void SDLCALL feed(void *user, SDL_AudioStream *stream,
                         int additional, int total) {
    (void)user;
    (void)total;
    if (additional <= 0) return;

    int16_t chunk[1024];
    while (additional > 0) {
        const int want = additional / (int)sizeof(int16_t);
        int frames = want < (int)SDL_arraysize(chunk) ? want
                                                      : (int)SDL_arraysize(chunk);
        if (frames <= 0) break;
        mix_into(chunk, frames);
        SDL_PutAudioStreamData(stream, chunk, frames * (int)sizeof(int16_t));
        additional -= frames * (int)sizeof(int16_t);
    }
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------
// Loads every clip and nothing else. Split out because a capture wants the
// mixer without a device: with one open, SDL is pulling frames out of the same
// mixer at the speed of a sound card, and a recording taken beside that is
// missing whatever the card took.
bool gg_audio_load(const char *sounds_dir) {
    if (g_ready) return true;
    bool any = false;
    for (int i = 0; i < GG_EV_COUNT; i++)
        if (FX_FILE[i] && load_clip(sounds_dir, FX_FILE[i], &g_fx[i])) any = true;
    for (int i = 0; i < TUNES; i++) {
        char name[64];
        SDL_snprintf(name, sizeof name, "mus_%s.wav", TUNE_NAME[i]);
        if (load_clip(sounds_dir, name, &g_music[i])) any = true;
    }
    return any;
}

bool gg_audio_init(const char *sounds_dir) {
    if (g_ready) return true;

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("gigantima: no audio subsystem: %s", SDL_GetError());
        return false;
    }

    bool any = false;
    for (int i = 0; i < GG_EV_COUNT; i++)
        if (FX_FILE[i] && load_clip(sounds_dir, FX_FILE[i], &g_fx[i])) any = true;
    for (int i = 0; i < TUNES; i++) {
        char name[64];
        SDL_snprintf(name, sizeof name, "mus_%s.wav", TUNE_NAME[i]);
        if (load_clip(sounds_dir, name, &g_music[i])) any = true;
    }
    if (!any) {
        SDL_Log("gigantima: no sounds could be read; the world will be silent");
        return false;
    }

    const SDL_AudioSpec spec = { .format = MIX_FORMAT, .channels = 1,
                                 .freq = MIX_RATE };
    g_out = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                      &spec, feed, nullptr);
    if (!g_out) {
        SDL_Log("gigantima: cannot open an audio device: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(g_out);

    g_ready = true;
    SDL_Log("gigantima: audio open at %d Hz", MIX_RATE);
    return true;
}

void gg_audio_quit(void) {
    if (g_out) {
        SDL_DestroyAudioStream(g_out);
        g_out = nullptr;
    }
    for (int i = 0; i < GG_EV_COUNT; i++) free_clip(&g_fx[i]);
    for (int i = 0; i < TUNES; i++) free_clip(&g_music[i]);
    SDL_zeroa(g_voice);
    g_tune = g_next_tune = -1;
    g_ready = false;
}

bool gg_audio_ready(void) { return g_ready; }

void gg_audio_volumes(int music, int effects) {
    g_music_vol = music < 0 ? 0 : music > 10 ? 10 : music;
    g_fx_vol = effects < 0 ? 0 : effects > 10 ? 10 : effects;
}

void gg_audio_play(gg_event e) {
    if (!g_ready || e >= GG_EV_COUNT || g_fx[e].frames == 0) return;

    // The oldest voice is stolen when they are all busy, so the newest thing
    // that happened is always the thing you hear.
    int pick = -1, oldest = -1;
    for (int i = 0; i < GG_VOICES; i++) {
        if (!g_voice[i].clip) { pick = i; break; }
        if (g_voice[i].at > oldest) { oldest = g_voice[i].at; pick = i; }
    }
    if (pick < 0) return;

    SDL_LockAudioStream(g_out);
    g_voice[pick].clip = &g_fx[e];
    g_voice[pick].at = 0;
    SDL_UnlockAudioStream(g_out);
}

// ---------------------------------------------------------------------------
// Which tune
// ---------------------------------------------------------------------------
int gg_audio_tune_count(void) { return TUNES; }

const char *gg_audio_tune_name(int i) {
    return (i >= 0 && i < TUNES) ? TUNE_NAME[i] : "";
}

int gg_audio_tune_for(const gg_game *g) {
    const gg_actor *p = gg_player_const(g);
    const int r = gg_map_region_at(&g->map, p->x, p->y);
    const int kind = r >= 0 ? g->map.region[r].kind : GG_REGION_WILD;

    // Underground has one mood whatever the hour: there is no hour down there.
    if (kind == GG_REGION_DUNGEON) return 4;

    // Dawn to dusk is day. The boundaries match the HUD's own words for the
    // time, so what a player reads and what they hear agree.
    const int hour = gg_game_hour(g);
    const bool day = hour >= 6 && hour < 20;
    const bool town = (kind == GG_REGION_TOWN || kind == GG_REGION_CASTLE);

    if (town) return day ? 2 : 3;
    return day ? 0 : 1;
}

// Starts, or crossfades to, one tune by index. The selection is in
// gg_audio_music_for; this is the part that moves the mixer, split out so a
// capture can ask for a tune the world is not currently in.
void gg_audio_music_play(int want) {
    if (want < 0 || want >= TUNES) return;
    if (want == g_tune && g_next_tune < 0) return;
    if (want == g_next_tune) return;      // already on its way

    if (g_out) SDL_LockAudioStream(g_out);
    if (g_tune < 0) {
        // Nothing playing: start, rather than fade in from silence.
        g_tune = want;
        g_music_at = 0;
        g_next_tune = -1;
        g_fade = 0;
    } else {
        g_next_tune = want;
        g_next_at = 0;
        g_fade = FADE_FRAMES;
    }
    if (g_out) SDL_UnlockAudioStream(g_out);
}

// Silence, and forget where everything was. The mixer keeps where each tune has
// got to, which is what makes it a mixer; a capture that wants to hear one tune
// from its beginning has to be able to say so.
void gg_audio_music_stop(void) {
    if (g_out) SDL_LockAudioStream(g_out);
    g_tune = g_next_tune = -1;
    g_music_at = g_next_at = 0;
    g_fade = 0;
    if (g_out) SDL_UnlockAudioStream(g_out);
}

// Exactly what the device would be handed, without a device. This is how the
// music is *checked*: a crossfade that clicks or drops to silence is a fault
// nobody can see in a screenshot and nobody should have to catch by ear.
void gg_audio_render(int16_t *out, int frames) {
    if (!out || frames <= 0) return;
    mix_into(out, frames);
}

int gg_audio_rate(void) { return MIX_RATE; }

// What the mixer is doing, for a test that wants to know that a change of tune
// is a *fade* rather than a cut. Measuring that off the waveform cannot be done
// honestly - a cut between two quiet passages steps no further than the music
// does on its own - so the fact itself is asked for instead.
int gg_audio_playing(void) { return g_tune; }
int gg_audio_fading(void)  { return g_fade; }

int gg_audio_tune_frames(int i) {
    return (i >= 0 && i < TUNES) ? g_music[i].frames : 0;
}

void gg_audio_music_for(const gg_game *g) {
    if (!g_ready) return;

    const int want = gg_audio_tune_for(g);
    if (want == g_tune && g_next_tune < 0) return;
    if (want == g_next_tune) return;      // already on its way

    SDL_LockAudioStream(g_out);
    if (g_tune < 0) {
        // Nothing playing: start, rather than fade in from silence.
        g_tune = want;
        g_music_at = 0;
        g_next_tune = -1;
        g_fade = 0;
    } else {
        g_next_tune = want;
        g_next_at = 0;
        g_fade = FADE_FRAMES;
    }
    SDL_UnlockAudioStream(g_out);
}
