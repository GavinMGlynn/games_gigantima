#!/usr/bin/env python3
"""Bake gigantima's sounds.

The art is vendored from LPC and baked into atlases. The sounds are not
vendored at all: they are *generated* here, and that is a deliberate departure
from what the plan first said.

Two reasons. Nothing exists in the shape LPC has - a single coherent, freely
licensed set in one repository - so vendoring would mean assembling dozens of
files from as many uploaders and tracking a licence and an attribution for
each. And this project's standing preference is no dependency over a small one.
A few hundred lines of arithmetic that writes its own WAVs has no licence
question, no attribution burden, no megabytes in the repository, and is
reproducible by anyone who runs it. What it does have is a ceiling: these are
tones and noise, not a recording of anything. See docs/PROJECT_STATUS.md, which
says so plainly. Replacing any of them is a file swap.

Everything here is integer-friendly, seeded and deterministic: running this
twice writes byte-identical files, so the committed sounds and the script
cannot drift.

    python3 tools/make_sounds.py

Needs nothing but the standard library.
"""
import math
import os
import struct
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "assets", "sounds")

# Effects carry transients - the click of a footfall, the crack of a blow - and
# want the higher rate. The tunes are a drone and an arpeggio with nothing above
# a few kilohertz in them, so half the rate is inaudible and halves 5 MB of
# committed WAV to under 2.
RATE = 22050
MUSIC_RATE = 11025
# Headroom, and it was measured rather than guessed. A blow baked at 20000
# peaks near that on its own; three of them at once plus the music summed past
# full scale and the mixer clipped, which is audible distortion and showed up as
# a peak of exactly 32767 in a headless capture. At 10000 the loudest effect is
# a quarter of full scale, so three together and a tune under them still fit.
PEAK = 10000


# ---------------------------------------------------------------------------
# The smallest synthesiser that will do
# ---------------------------------------------------------------------------
class Rng:
    """A tiny xorshift, so noise is the same every run.

    The same generator shape the simulation uses, for the same reason: a
    committed file that changes when nothing changed is a file nobody trusts.
    """

    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF or 1

    def next(self):
        x = self.s
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.s = x & 0xFFFFFFFF
        return self.s

    def bipolar(self):
        return (self.next() / 0xFFFFFFFF) * 2.0 - 1.0


def env(i, n, attack=0.01, release=0.3):
    """A simple attack/decay shape, in fractions of the whole sound."""
    t = i / n
    a = max(attack, 1e-6)
    r = max(release, 1e-6)
    if t < a:
        return t / a
    if t > 1.0 - r:
        return max(0.0, (1.0 - t) / r)
    return 1.0


def sine(freq, i, rate=RATE):
    return math.sin(2.0 * math.pi * freq * i / rate)


def triangle(freq, i, rate=RATE):
    """Softer than a square and far less shrill than a saw at this scale."""
    p = (freq * i / rate) % 1.0
    return 4.0 * abs(p - 0.5) - 1.0


def write_wav(name, samples, rate=RATE):
    """One channel, 16-bit. Clipped rather than wrapped."""
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name)
    frames = bytearray()
    for s in samples:
        v = int(max(-1.0, min(1.0, s)) * PEAK)
        frames += struct.pack("<h", v)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(frames))
    return path, len(samples)


# ---------------------------------------------------------------------------
# Effects
#
# Each one is a shape rather than a recording: a footstep is filtered noise
# under a fast envelope, a blow is noise plus a low thump, a coin is two high
# tones a beat apart. Short - none of these is longer than a third of a second,
# because a turn-based game plays them one per action.
# ---------------------------------------------------------------------------
def fx_step():
    n = int(RATE * 0.09)
    rng = Rng(0x51EE)
    out, low = [], 0.0
    for i in range(n):
        # A one-pole low pass over noise: a footfall is a dull sound, and raw
        # white noise reads as static.
        low += (rng.bipolar() - low) * 0.12
        out.append(low * env(i, n, 0.02, 0.75) * 0.5)
    return out


def fx_bump():
    n = int(RATE * 0.14)
    rng = Rng(0xB0FF)
    out, low = [], 0.0
    for i in range(n):
        low += (rng.bipolar() - low) * 0.06
        thump = sine(70 - 20 * i / n, i) * 0.6
        out.append((low * 0.5 + thump) * env(i, n, 0.005, 0.7))
    return out


def fx_blow():
    n = int(RATE * 0.16)
    rng = Rng(0xB10C)
    out, low = [], 0.0
    for i in range(n):
        low += (rng.bipolar() - low) * 0.35
        thump = sine(120 - 60 * i / n, i) * 0.5
        out.append((low * 0.7 + thump) * env(i, n, 0.002, 0.8))
    return out


def fx_hurt():
    n = int(RATE * 0.22)
    out = []
    for i in range(n):
        f = 300 - 160 * i / n
        out.append((triangle(f, i) * 0.5 + sine(f * 2, i) * 0.2) *
                   env(i, n, 0.01, 0.6))
    return out


def fx_die():
    n = int(RATE * 0.5)
    out = []
    for i in range(n):
        f = 220 * math.pow(0.35, i / n)
        out.append(triangle(f, i) * 0.55 * env(i, n, 0.01, 0.5))
    return out


def fx_take():
    n = int(RATE * 0.12)
    out = []
    for i in range(n):
        out.append(sine(660 + 240 * i / n, i) * 0.35 * env(i, n, 0.01, 0.6))
    return out


def fx_drop():
    n = int(RATE * 0.12)
    out = []
    for i in range(n):
        out.append(sine(520 - 200 * i / n, i) * 0.35 * env(i, n, 0.01, 0.6))
    return out


def fx_coin():
    n = int(RATE * 0.18)
    out = []
    for i in range(n):
        # Two tones a fifth apart, the second entering late: the shape every
        # game since 1985 has used for money, and it works.
        a = sine(1046, i) * 0.3 * env(i, n, 0.005, 0.7)
        b = sine(1568, i) * 0.25 * (0.0 if i < n * 0.28 else env(i - int(n * 0.28), int(n * 0.72), 0.01, 0.7))
        out.append(a + b)
    return out


def fx_door():
    n = int(RATE * 0.3)
    rng = Rng(0xD002)
    out, low = [], 0.0
    for i in range(n):
        low += (rng.bipolar() - low) * 0.02
        # A creak: a slow wobble on a low tone, plus the rumble of the boards.
        wob = sine(180 + 40 * sine(7, i), i) * 0.25
        out.append((wob + low * 0.5) * env(i, n, 0.1, 0.4))
    return out


def fx_cast():
    n = int(RATE * 0.4)
    out = []
    for i in range(n):
        t = i / n
        f = 300 + 700 * t * t
        out.append((sine(f, i) * 0.3 + sine(f * 1.5, i) * 0.15) *
                   env(i, n, 0.2, 0.4))
    return out


def fx_learn():
    n = int(RATE * 0.35)
    out = []
    # A rising third: the sound of having understood something.
    for i in range(n):
        step = 0 if i < n // 2 else 1
        f = [523, 659][step]
        seg = i if step == 0 else i - n // 2
        out.append(sine(f, i) * 0.3 * env(seg, n // 2, 0.02, 0.5))
    return out


def fx_level():
    """Rising, and it arrives somewhere - a fanfare of three notes.

    Distinct from `learn`, which is two notes and a step: understanding a word
    is a small thing that happens often, and being better than you were is not.
    """
    n = int(RATE * 0.55)
    out = []
    notes = [523, 659, 784]                   # C, E, G - the chord, spelled out
    seg = n // len(notes)
    for i in range(n):
        which = min(i // seg, len(notes) - 1)
        within = i - which * seg
        # The last note rings on rather than being cut to length, so it lands
        # rather than stopping.
        hold = seg if which < len(notes) - 1 else n - which * seg
        out.append((sine(notes[which], i) * 0.28 +
                    sine(notes[which] * 2, i) * 0.08) *
                   env(within, hold, 0.02, 0.45))
    return out


EFFECTS = {
    "step": fx_step,
    "bump": fx_bump,
    "blow": fx_blow,
    "hurt": fx_hurt,
    "die": fx_die,
    "take": fx_take,
    "drop": fx_drop,
    "coin": fx_coin,
    "door": fx_door,
    "cast": fx_cast,
    "learn": fx_learn,
    "level": fx_level,
}


# ---------------------------------------------------------------------------
# Music
#
# Five loops, one per mood, where a mood is a region and a part of the day. They
# are **written down** rather than wandered: each has a chord progression, a
# bass, an inner voice and a melody with phrases that repeat, because a random
# walk over a mode is recognisably a random walk after about eight seconds and
# there is nothing to remember about it afterwards.
#
# The synthesis is three voices and a room:
#
#   pluck   Karplus-Strong - a burst of noise in a delay line, fed back through
#           an average of itself. Two lines of arithmetic and it is a plucked
#           string, which is the sound a game set among standing stones and
#           market stalls wants for its melody.
#   pad     detuned triangles under a slow vibrato, for the harmony
#   bass    a sine with a little of its own second harmonic, for the floor
#
# and then a Schroeder reverb - four combs into two allpasses - because dry
# tones sound like a test signal however well composed they are, and a room is
# the difference between "notes" and "music".
#
# **The loop is seamless by construction.** The music is written as a whole
# number of bars ending on the tonic, and the reverb is run over two copies of
# it with the second copy kept: the tail of the last bar is therefore already
# present under the first, exactly as it will be when the loop comes round. The
# quarter-second fades at the ends were what hid a hard edge before; nothing
# needs hiding now, and they are gone.
#
# A mode rather than a key signature, because a mode carries a mood on its own
# and needs no modulation to do it.
# ---------------------------------------------------------------------------
# Semitones above the root.
IONIAN     = [0, 2, 4, 5, 7, 9, 11]     # bright: a town in the morning
AEOLIAN    = [0, 2, 3, 5, 7, 8, 10]     # sober: the wilderness
DORIAN     = [0, 2, 3, 5, 7, 9, 10]     # wistful: dusk
PHRYGIAN   = [0, 1, 3, 5, 7, 8, 10]     # uneasy: night, and underground

# A rest, in a melody. `None` would do but reads badly beside a degree of 0,
# which is the tonic and the most common note there is.
REST = "rest"


def degree_hz(root, mode, degree):
    """Scale degree to hertz, where 7 is the octave above 0 and -1 the seventh
    below. Written to take any integer so a melody can lean either way out of
    the octave it is mostly in without a special case."""
    octave = degree // len(mode)
    return root * math.pow(2.0, (mode[degree % len(mode)] + 12 * octave) / 12.0)


def add(out, at, samples, gain=1.0):
    """Mixes `samples` into `out` at `at`, wrapping past the end.

    Wrapping rather than clipping: a note struck in the last bar rings over the
    loop point, which is where it will be heard when the loop comes round.
    """
    n = len(out)
    if n == 0:
        return
    for i, s in enumerate(samples):
        out[(at + i) % n] += s * gain


def pluck(freq, n, rate, seed, damp=0.5, bright=1.0):
    """Karplus-Strong. `damp` is how fast the string dies; `bright` how much of
    the initial burst is noise rather than a sawtooth edge."""
    if freq <= 0 or n <= 0:
        return []
    period = max(2, int(rate / freq))
    rng = Rng(seed)
    line = []
    for i in range(period):
        edge = 2.0 * (i / period) - 1.0            # a plectrum, not a hiss
        line.append(rng.bipolar() * bright + edge * (1.0 - bright))

    out = []
    at = 0
    # Feedback under one keeps the string finite; the average of two
    # neighbouring samples is the lowpass that makes it decay from the top down,
    # which is what a real string does and what makes this sound like one.
    feedback = 1.0 - damp * 0.02
    for _ in range(n):
        v = line[at]
        out.append(v)
        line[at] = (v + line[(at + 1) % period]) * 0.5 * feedback
        at = (at + 1) % period
    return out


def pad(freq, n, rate, gain=1.0):
    """Two triangles a hair apart under a slow vibrato: the beat between them is
    what stops a held chord sounding like a test tone."""
    out = []
    for i in range(n):
        vib = 1.0 + 0.003 * math.sin(2.0 * math.pi * 4.5 * i / rate)
        a = triangle(freq * vib, i, rate)
        b = triangle(freq * 1.004, i, rate)
        out.append((a * 0.5 + b * 0.5) * env(i, n, 0.25, 0.35) * gain)
    return out


def bass(freq, n, rate, gain=1.0):
    out = []
    for i in range(n):
        v = sine(freq, i, rate) + 0.25 * sine(freq * 2.0, i, rate)
        out.append(v * env(i, n, 0.02, 0.45) * gain)
    return out


def lowpass(samples, rate, cutoff):
    """One pole. Enough to take the edge off a pluck without a filter design."""
    if not samples:
        return samples
    a = 1.0 - math.exp(-2.0 * math.pi * cutoff / rate)
    y = 0.0
    out = []
    for s in samples:
        y += a * (s - y)
        out.append(y)
    return out


def reverb(samples, rate, room=0.82, wet=0.32):
    """Schroeder: four parallel combs into two allpasses.

    Delays are prime-ish and unrelated so the echoes never line up into a
    flutter, which is the one thing a cheap reverb does that a listener hears
    as a fault rather than as a room.
    """
    n = len(samples)
    if n == 0:
        return samples
    combs = [(int(rate * 0.0297), room), (int(rate * 0.0371), room * 0.98),
             (int(rate * 0.0411), room * 0.96), (int(rate * 0.0437), room * 0.94)]
    tail = [0.0] * n
    for delay, gain in combs:
        if delay <= 0 or delay >= n:
            continue
        buf = [0.0] * delay
        at = 0
        for i in range(n):
            old = buf[at]
            tail[i] += old
            buf[at] = samples[i] + old * gain
            at = (at + 1) % delay
    tail = [v * 0.25 for v in tail]

    for delay, gain in ((int(rate * 0.005), 0.7), (int(rate * 0.0017), 0.7)):
        if delay <= 0 or delay >= n:
            continue
        buf = [0.0] * delay
        at = 0
        for i in range(n):
            old = buf[at]
            v = -gain * tail[i] + old
            buf[at] = tail[i] + gain * v
            tail[i] = v
            at = (at + 1) % delay

    return [samples[i] * (1.0 - wet) + tail[i] * wet for i in range(n)]


def render(score):
    """Renders one written-down piece, at whatever level it comes out at.

    A score is bars of chords, a bass figure, a repeating inner voice and a
    melody in phrases - everything a listener could hum back, none of it rolled.
    """
    rate = MUSIC_RATE
    tempo = score["tempo"]
    beats = score["beats"]                     # beats in a bar
    bars = len(score["chords"])
    beat_n = int(rate * 60.0 / tempo)
    n = beat_n * beats * bars
    out = [0.0] * n
    root = score["root"]
    mode = score["mode"]

    # The harmony. One pad note per chord tone, held for the whole bar, so the
    # chord changes on the barline and nowhere else.
    for b, chord in enumerate(score["chords"]):
        at = b * beats * beat_n
        length = beats * beat_n
        for k, degree in enumerate(chord):
            f = degree_hz(root, mode, degree)
            add(out, at, pad(f, length + beat_n // 2, rate),
                score["pad_gain"] / (1.0 + 0.4 * k))

    # The floor. The bass plays the root of the bar on the figure's beats -
    # written per tune, because a walking bass and a held drone are different
    # pieces of music even over the same chords.
    for b, chord in enumerate(score["chords"]):
        for beat, drop in score["bass"]:
            at = (b * beats + beat) * beat_n
            f = degree_hz(root, mode, chord[0] - 7 + drop)
            add(out, at, bass(f, int(beat_n * 1.6), rate), score["bass_gain"])

    # An inner voice, plucked, that repeats every bar: the figure a player
    # stops noticing and would miss at once if it went.
    seed = score["seed"]
    for b, chord in enumerate(score["chords"]):
        for beat, step in score["inner"]:
            at = int((b * beats + beat) * beat_n)
            f = degree_hz(root, mode, chord[0] + step)
            seed = (seed * 1664525 + 1013904223) & 0xFFFFFFFF
            v = pluck(f, int(beat_n * 1.4), rate, seed,
                      damp=score["damp"], bright=0.75)
            add(out, at, lowpass(v, rate, score["tone"]), score["inner_gain"])

    # And the tune. Written as phrases of (degree, beats) laid end to end, so
    # what repeats is a phrase rather than a note.
    at = int(score["melody_at"] * beat_n)
    for phrase in score["melody"]:
        for degree, length in phrase:
            span = int(length * beat_n)
            if degree is not REST:
                f = degree_hz(root, mode, degree)
                seed = (seed * 1664525 + 1013904223) & 0xFFFFFFFF
                v = pluck(f, min(span + beat_n, n), rate, seed,
                          damp=score["damp"] * 0.6, bright=0.6)
                add(out, at, lowpass(v, rate, score["tone"] * 1.4),
                    score["melody_gain"])
            at += span

    # The room, run over two copies with the second kept, so the tail of the
    # last bar is under the first bar exactly as the loop will play it.
    wet = reverb(out + out, rate, score["room"], score["wet"])[n:]

    return wet


def compose(score):
    """The same, normalised to a fixed headroom rather than to whatever this
    piece happened to peak at - so five tunes are five tunes at one volume and a
    crossfade between two of them does not step."""
    wet = render(score)
    loudest = max(abs(v) for v in wet) or 1.0
    return [v / loudest * 0.72 for v in wet]


# ---------------------------------------------------------------------------
# The five pieces
#
# Degrees are scale degrees, not semitones: 0 is the tonic, 7 the octave above,
# -1 the seventh below. So the same phrase written once reads the same in every
# mode and carries that mode's mood without being rewritten.
# ---------------------------------------------------------------------------

# A market at ten in the morning. Ionian, a dance in six, and the melody is two
# phrases that answer each other and a third that goes somewhere before the
# first comes back.
TOWN_DAY = {
    "seed": 0xC0FFE, "root": 261.63, "mode": IONIAN,
    "tempo": 108, "beats": 6,
    "chords": [[0, 2, 4], [3, 5, 7], [4, 6, 8], [0, 2, 4],
               [5, 7, 9], [3, 5, 7], [4, 6, 8], [0, 2, 4]],
    "bass":  [(0, 0), (2, 7), (4, 0)],
    "inner": [(1, 4), (3, 2), (5, 4)],
    "melody_at": 0,
    "melody": [
        [(4, 1), (2, 0.5), (4, 0.5), (5, 1), (4, 1), (2, 1), (0, 1)],
        [(2, 1), (4, 0.5), (5, 0.5), (7, 1), (5, 1), (4, 2)],
        [(7, 1), (6, 0.5), (7, 0.5), (9, 1), (7, 1), (5, 1), (4, 1)],
        [(5, 1), (4, 0.5), (2, 0.5), (4, 1), (2, 1), (0, 2)],
        [(4, 1), (2, 0.5), (4, 0.5), (5, 1), (7, 1), (5, 1), (4, 1)],
        [(2, 1), (0, 0.5), (2, 0.5), (4, 1), (2, 1), (0, 2)],
        [(0, 1), (2, 1), (4, 1), (5, 1), (7, 2)],
        [(4, 1), (2, 1), (0, 4)],
    ],
    "pad_gain": 0.13, "bass_gain": 0.34, "inner_gain": 0.18,
    "melody_gain": 0.80, "damp": 0.9, "tone": 2600,
    "room": 0.74, "wet": 0.24,
}

# The same town with the shutters up. Dorian, half the speed, the melody in
# long notes and the inner voice down to two plucks a bar.
TOWN_NIGHT = {
    "seed": 0xD00D1, "root": 220.00, "mode": DORIAN,
    "tempo": 66, "beats": 4,
    "chords": [[0, 2, 4], [5, 7, 9], [3, 5, 7], [0, 2, 4],
               [6, 8, 10], [5, 7, 9], [4, 6, 8], [0, 2, 4]],
    "bass":  [(0, 0), (2, 7)],
    "inner": [(1, 4), (3, 2)],
    "melody_at": 2,
    "melody": [
        [(0, 2), (2, 1), (4, 3), (2, 2)],
        [(4, 2), (5, 1), (4, 1), (2, 2), (0, 2)],
        [(4, 2), (7, 2), (5, 2), (4, 2)],
        [(2, 3), (0, 1), (REST, 2), (-1, 2)],
        [(0, 2), (2, 1), (4, 3), (5, 2)],
        [(7, 3), (5, 1), (4, 2), (2, 2)],
        [(4, 2), (2, 2), (0, 4)],
        [(REST, 4), (0, 4)],
    ],
    "pad_gain": 0.16, "bass_gain": 0.30, "inner_gain": 0.14,
    "melody_gain": 0.72, "damp": 0.7, "tone": 1900,
    "room": 0.80, "wet": 0.32,
}

# The road. Aeolian, open fifths under it and a melody that keeps moving,
# because this is what plays for hours while somebody walks north.
WILD_DAY = {
    "seed": 0xA11CE, "root": 196.00, "mode": AEOLIAN,
    "tempo": 88, "beats": 4,
    "chords": [[0, 4, 7], [5, 9, 12], [3, 7, 10], [4, 8, 11],
               [0, 4, 7], [6, 10, 13], [4, 8, 11], [0, 4, 7]],
    "bass":  [(0, 0), (2, 0), (3, 7)],
    "inner": [(0, 7), (2, 4), (3, 7)],
    "melody_at": 1,
    "melody": [
        [(0, 1), (2, 1), (4, 2), (2, 1), (0, 1), (-3, 2)],
        [(0, 1), (4, 1), (7, 2), (5, 1), (4, 1), (2, 2)],
        [(4, 1), (5, 1), (7, 2), (9, 1), (7, 1), (5, 2)],
        [(4, 2), (2, 1), (0, 1), (2, 4)],
        [(7, 1), (5, 1), (4, 2), (5, 1), (7, 1), (9, 2)],
        [(7, 1), (4, 1), (5, 2), (4, 1), (2, 1), (0, 2)],
        [(-3, 2), (0, 2), (2, 2), (4, 2)],
        [(2, 2), (0, 6)],
    ],
    "pad_gain": 0.11, "bass_gain": 0.35, "inner_gain": 0.16,
    "melody_gain": 0.76, "damp": 1.0, "tone": 2200,
    "room": 0.84, "wet": 0.34,
}

# The same road with something on it. Phrygian - the flat second is the whole
# mood - slow, sparse, and the melody circles the tonic instead of leaving it.
WILD_NIGHT = {
    "seed": 0xB0B01, "root": 174.61, "mode": PHRYGIAN,
    "tempo": 56, "beats": 4,
    "chords": [[0, 2, 4], [1, 3, 5], [0, 2, 4], [6, 8, 10],
               [0, 2, 4], [1, 3, 5], [4, 6, 8], [0, 2, 4]],
    "bass":  [(0, 0), (2, 7)],
    "inner": [(2, 4)],
    "melody_at": 3,
    "melody": [
        [(0, 3), (1, 1), (0, 4)],
        [(REST, 2), (4, 2), (1, 2), (0, 2)],
        [(1, 2), (0, 2), (-2, 4)],
        [(REST, 4), (0, 4)],
        [(4, 3), (5, 1), (4, 2), (1, 2)],
        [(0, 4), (REST, 2), (1, 2)],
        [(0, 2), (-2, 2), (-4, 4)],
        [(REST, 4), (0, 4)],
    ],
    "pad_gain": 0.18, "bass_gain": 0.32, "inner_gain": 0.11,
    "melody_gain": 0.66, "damp": 0.5, "tone": 1500,
    "room": 0.86, "wet": 0.40,
}

# Under the fells. Almost nothing: a low drone, a bass that lands twice a bar,
# and six notes in thirty seconds - the point is the room, not the tune.
DUNGEON = {
    "seed": 0xE1234, "root": 146.83, "mode": PHRYGIAN,
    "tempo": 48, "beats": 4,
    "chords": [[0, 2, 4], [0, 2, 4], [1, 3, 5], [1, 3, 5],
               [0, 2, 4], [6, 8, 10], [0, 2, 4], [0, 2, 4]],
    "bass":  [(0, 0)],
    "inner": [(3, 7)],
    "melody_at": 5,
    "melody": [
        [(0, 4), (REST, 4)],
        [(1, 4), (0, 4)],
        [(REST, 8)],
        [(-2, 4), (0, 4)],
        [(REST, 4), (4, 4)],
        [(1, 4), (0, 4)],
        [(REST, 8)],
        [(0, 8)],
    ],
    "pad_gain": 0.22, "bass_gain": 0.36, "inner_gain": 0.10,
    "melody_gain": 0.60, "damp": 0.4, "tone": 1100,
    "room": 0.90, "wet": 0.46,
}

TUNES = {
    "wild_day":   WILD_DAY,
    "wild_night": WILD_NIGHT,
    "town_day":   TOWN_DAY,
    "town_night": TOWN_NIGHT,
    "dungeon":    DUNGEON,
}


def main():
    os.makedirs(OUT, exist_ok=True)
    print("gigantima: baking sounds")

    total = 0
    for name, make in EFFECTS.items():
        path, n = write_wav(f"fx_{name}.wav", make())
        total += n * 2
        print(f"  fx_{name:<6} {n / RATE:5.2f}s")

    for name, score in TUNES.items():
        path, n = write_wav(f"mus_{name}.wav", compose(score), rate=MUSIC_RATE)
        total += n * 2
        print(f"  mus_{name:<11} {n / MUSIC_RATE:5.1f}s")

    print(f"  wrote {len(EFFECTS)} effects and {len(TUNES)} tunes, "
          f"{total / 1024 / 1024:.1f} MB, into assets/sounds/")


if __name__ == "__main__":
    main()
