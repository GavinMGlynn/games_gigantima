// gg_audio.h - what the world sounds like.
//
// One device stream, mixed here rather than by a library. SDL3 plays WAVs on
// its own, and the sounds are generated rather than vendored, so SDL_mixer buys
// nothing but a dependency - and this project's standing preference is no
// dependency over a small one. What is mixed is a music loop under up to a
// handful of one-shot effects, and that is the whole of it.
//
// The simulation knows nothing about any of this. It emits events; the frontend
// drains them and hands them here.
#ifndef GG_AUDIO_H
#define GG_AUDIO_H

#include "core/gg_common.h"
#include "core/gg_game.h"

// How many one-shot effects may sound at once. A turn produces one or two; the
// bound is what stops a long fight from stacking a hundred blows into a roar.
#define GG_VOICES 8

// Opens the device and loads every sound. Returns false if audio is not
// available at all, which is not fatal: a silent game is a playable one, so the
// caller logs and carries on.
bool gg_audio_init(const char *sounds_dir);

// The clips without a device, for rendering the mix rather than playing it.
// False if not one sound could be read. See gg_audio_render.
bool gg_audio_load(const char *sounds_dir);
void gg_audio_quit(void);

// Whether there is any sound to be had. False after a failed init.
bool gg_audio_ready(void);

// Volumes, 0 to 10, as the options page keeps them. Applied to the mix, so a
// zero is genuinely silent rather than quiet.
void gg_audio_volumes(int music, int effects);

// Plays one. Ignored when audio never opened.
void gg_audio_play(gg_event e);

// Chooses the music the world calls for - where the Avatar is, and what hour
// it is - and crossfades if that has changed. Safe to call every frame; it
// only does anything when the answer differs from what is playing.
void gg_audio_music_for(const gg_game *g);

// Which tune the world calls for, as an index into the loaded music. Exposed
// because it is pure arithmetic over the game state, and far easier to check as
// a number than by listening.
int gg_audio_tune_for(const gg_game *g);

// The names of the tunes, in the order gg_audio_tune_for returns.
const char *gg_audio_tune_name(int i);
int gg_audio_tune_count(void);

// Starts, or crossfades to, one tune by index - the half of gg_audio_music_for
// that moves the mixer, without the half that decides. A capture uses it to
// walk through every tune in one recording.
void gg_audio_music_play(int tune);

// Silence, and forget where every tune had got to - so the next one starts at
// its beginning rather than wherever it was last left.
void gg_audio_music_stop(void);

// Renders exactly what the device would be handed, without a device, and says
// at what rate. This is how the music is checked: a crossfade that clicks or
// drops to silence cannot be seen in a screenshot and should not have to be
// caught by ear.
void gg_audio_render(int16_t *out, int frames);
int  gg_audio_rate(void);

// Which tune is playing, or -1, and how many frames of crossfade are left.
// A change of tune is a fade of half a second; that it is a fade at all cannot
// be measured off the waveform honestly, so it is asked for.
int gg_audio_playing(void);
int gg_audio_fading(void);

// How long one tune is, in frames. A loop's length is the one number needed to
// render exactly one round of it and look at where it joins.
int gg_audio_tune_frames(int i);

#endif // GG_AUDIO_H
