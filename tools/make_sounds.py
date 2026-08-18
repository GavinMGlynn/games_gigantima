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
# One loop per mood, and a mood is a region and a part of the day. These are
# thin - a drone, a slow arpeggio over it, and nothing else - and they are meant
# to be replaced by something composed. What they establish is the *mechanism*:
# the right loop for where you are and what hour it is, crossfaded when that
# changes.
#
# A mode rather than a key signature, because a mode carries a mood on its own
# and needs no chord changes to do it.
# ---------------------------------------------------------------------------
# Semitones above the root.
IONIAN     = [0, 2, 4, 5, 7, 9, 11]     # bright: a town in the morning
AEOLIAN    = [0, 2, 3, 5, 7, 8, 10]     # sober: the wilderness
DORIAN     = [0, 2, 3, 5, 7, 9, 10]     # wistful: dusk
PHRYGIAN   = [0, 1, 3, 5, 7, 8, 10]     # uneasy: night, and underground


def note_hz(root, semitones):
    return root * math.pow(2.0, semitones / 12.0)


def music(seed, root, mode, seconds, tempo, drone=True, bright=0.5):
    """A drone and a slow arpeggio wandering the mode.

    Deterministic in `seed`, so the committed file is the same every bake.
    """
    rate = MUSIC_RATE
    n = int(rate * seconds)
    out = [0.0] * n
    rng = Rng(seed)

    if drone:
        for i in range(n):
            # Root and fifth, detuned a hair against each other so the pad
            # breathes instead of sitting perfectly still.
            a = sine(root / 2, i, rate) * 0.10
            b = sine(root / 2 * 1.5 * 1.001, i, rate) * 0.07
            out[i] += a + b

    step_len = int(rate * 60.0 / tempo)
    if step_len <= 0:
        step_len = rate // 2

    at = 0
    degree = 0
    while at < n:
        # Wander by a step or a third, staying inside the mode - which keeps it
        # tonal without needing anything as grand as a chord progression.
        move = [-2, -1, 1, 1, 2, 2][rng.next() % 6]
        degree = max(0, min(len(mode) * 2 - 1, degree + move))
        octave = degree // len(mode)
        semis = mode[degree % len(mode)] + 12 * octave
        f = note_hz(root, semis)

        length = min(step_len, n - at)
        for i in range(length):
            voice = (triangle(f, i, rate) * bright +
                     sine(f, i, rate) * (1.0 - bright))
            out[at + i] += voice * 0.16 * env(i, length, 0.12, 0.5)
        at += step_len

    # A short fade at both ends so the loop point is inaudible.
    edge = int(rate * 0.25)
    for i in range(min(edge, n)):
        k = i / edge
        out[i] *= k
        out[n - 1 - i] *= k
    return out


TUNES = {
    # name                seed   root  mode      secs tempo bright
    "wild_day":     (0xA11CE, 196.00, AEOLIAN,   24, 46, 0.35),
    "wild_night":   (0xB0B01, 174.61, PHRYGIAN,  24, 38, 0.25),
    "town_day":     (0xC0FFE, 261.63, IONIAN,    24, 62, 0.55),
    "town_night":   (0xD00D1, 220.00, DORIAN,    24, 44, 0.35),
    "dungeon":      (0xE1234, 146.83, PHRYGIAN,  24, 32, 0.20),
}


def main():
    os.makedirs(OUT, exist_ok=True)
    print("gigantima: baking sounds")

    total = 0
    for name, make in EFFECTS.items():
        path, n = write_wav(f"fx_{name}.wav", make())
        total += n * 2
        print(f"  fx_{name:<6} {n / RATE:5.2f}s")

    for name, (seed, root, mode, secs, tempo, bright) in TUNES.items():
        path, n = write_wav(f"mus_{name}.wav",
                            music(seed, root, mode, secs, tempo, bright=bright),
                            rate=MUSIC_RATE)
        total += n * 2
        print(f"  mus_{name:<11} {n / MUSIC_RATE:5.1f}s")

    print(f"  wrote {len(EFFECTS)} effects and {len(TUNES)} tunes, "
          f"{total / 1024 / 1024:.1f} MB, into assets/sounds/")


if __name__ == "__main__":
    main()
