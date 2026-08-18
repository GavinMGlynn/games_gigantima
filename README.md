# gigantima

An Ultima VI-style seamless-world RPG in C23 and SDL3.

[![ci](https://github.com/GavinMGlynn/games_gigantima/actions/workflows/ci.yml/badge.svg)](https://github.com/GavinMGlynn/games_gigantima/actions/workflows/ci.yml)

> **Status: beta.** A game, with an engine under it. There is a story you can
> finish — a caravan out of the north that never arrived, followed up a road
> with brigands on it to a ring of standing stones and back — and around it a
> seamless single-scale world, a town of houses whose people keep daily
> schedules, keyword conversation, a party, combat, magic you cast by learning
> the words for it, a world with a voice, and an editor to author more of it.
> Everything works from a gamepad alone; every key can be moved; a session can
> be recorded and replayed to the same world to the bit.
>
> What is missing is *more* of it rather than a part that half works: one
> storyline, three kinds of creature, two maps, no levelling, and sounds that
> are synthesised rather than composed. `docs/PROJECT_STATUS.md` is the single
> source of truth for what works, with the gaps named at the bottom. Nothing
> here claims more than it has earned.

## Getting it

**[Download a build](https://github.com/GavinMGlynn/games_gigantima/releases)** —
a tarball for Linux, a zip and an installer for Windows, a disk image for macOS.
Unpack it anywhere and run `gigantima`; there is nothing to install and nothing
to set up. The builds are unsigned, so Windows and macOS will both want telling
that you meant it — [PLAYING.md](PLAYING.md#getting-it) says how.

**[PLAYING.md](PLAYING.md)** is the player's guide: the first ten minutes, every
control on keyboard and pad, the maps, the editor, where your journeys are kept,
the command line, and how to report a bug as a file somebody else can run.

## What "Ultima VI-style" means here

Ultima VI's defining choice was a **single-scale seamless world**. A town is not
a separate map you enter — it is a cluster of walls and doors standing in the
same grid as the wilderness around it, at the same scale, and you walk in. That
one decision shapes the whole engine, so it is the one this project starts from:

- **One map class.** "Town", "dungeon" and "wilderness" are regions of a map,
  not different kinds of thing. There is no scale change and no load screen.
- **Turn-based.** The world advances when you act and at no other time. Stand
  still and the town stands still with you.
- **NPCs with days.** Every townsperson has a schedule and is somewhere for a
  reason at every hour. This is why Ultima VI's towns felt inhabited, so it is
  in the engine rather than bolted on later.
- **A clock that matters.** One game minute per turn, 1440 turns to the day,
  and the light level follows it.

The presentation is *not* turn-based: walk cycles and the camera interpolate at
60 Hz while the simulation waits for your next move.

## Building

Needs a C23 compiler — **GCC 14+, Clang 19+, AppleClang 16+, or MSVC 19.39+** —
plus CMake 3.28 and Ninja.

```sh
git clone https://github.com/GavinMGlynn/games_gigantima.git
cd games_gigantima
git submodule update --init --depth 1 ext/sdl ext/sdl_image

cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release

./build/linux-release/gigantima
```

Presets exist for `linux`, `macos` and `windows`, each in `-debug` and
`-release`. The debug presets build with address and UB sanitizers.

**Only two submodules are needed to build.** The art submodule is 766 MB and is
required only to regenerate the atlas, which is committed — see `ext/README.md`.

On Debian/Ubuntu the SDL build needs the usual X11/Wayland/audio development
packages; the exact list is in `.github/workflows/ci.yml`, which is kept honest
by being the thing CI actually runs.

### Supported platforms

64-bit only, enforced at configure time:

| Platform | Compiler | CI |
| --- | --- | --- |
| Linux x86_64, Debian family | GCC 14 | yes |
| Linux x86_64, RHEL family | GCC 14 | yes, `rockylinux:10` container |
| Windows x64 | MSVC | yes |
| macOS arm64 | AppleClang | yes |

### Making a package

```sh
cd build && cpack
```

A tarball on Linux, a zip and an installer on Windows, and a disk image holding
two `.app` bundles on macOS. Each is a folder you unpack and run: the
executables at the top and `assets/` beside them, which is where the game looks
for its art — so a package works from wherever it is put, with no install step
and nothing set in the environment.

## Playing

| | |
| --- | --- |
| Arrows / WASD / keypad | walk — the keypad also gives the four diagonals |
| `T` | talk to whoever you face, and ask about the word under the cursor |
| `L` | look at what you stand on |
| `O` | open |
| `C` | open the book of spells |
| `J` | the journal - what has happened so far |
| `F` | strike what thou facest, or throw a readied stone |
| `G` | take what is underfoot |
| `I` | what thou carriest |
| `U` / `R` / `P` | use it, ready it, put it down |
| `Space` / `.` | wait a turn |
| `F5` | save |
| `F1` | debug window |
| `F11` / `Alt+Enter` | fullscreen |
| `Esc` | back out of whatever is open, then pause |

**Every one of these can be moved** — Options → Keys — and the text can be drawn
at twice the size.

A gamepad works for all of it, hot-plugged, with the sticks and the d-pad both
driving movement: **A** talks, **B** waits, **X** looks, **Y** opens, **Back**
opens the pack, **LB** the spell book, **RB** strikes, the **left trigger**
takes what is underfoot, the **right trigger** opens the journal, and **Start**
pauses. Inside the pack those same four face buttons carry the pack's verbs
instead — **A** uses, **Y** readies, **X** sets down, **B** closes.

The game opens on a title screen. Continue picks up the most recent journey,
New journey asks for a name, and Journeys lists everything saved with its day,
hour and place. Options sets the window scale, fullscreen, rumble, the two volumes, the text
size, the map palette, and what every key does.

**A pad alone is enough for all of it**, naming included — the naming page
carries its own alphabet, so nothing here needs a keyboard.

Each profile keeps a world of its own, and the game saves on the way out, so
Continue is always where you left off. From the command line, `--profile NAME`
says whose game to act on and `--play` goes straight into it without stopping at
the title; `--new` starts that profile over rather than resuming.

Walking into somebody talks to them, as Ultima VI did — the common case needs
no key at all.

There are brigands in the hills — and outlaws, and a slinger who fights from
three tiles away — and they are people — this art set has no
monsters in it at all, which suits a caravan that never arrived better than a
wolf would. Walking into one strikes it, the same gesture that talks to a
townsperson; a readied stone reaches five tiles and lands where it was thrown.
A quick foe acts twice where a slow one acts once, and what falls leaves what it
was carrying.

Some of them will come with you. Asking is a topic like any other — Dupre and
Gwenno each have a reason and a word for it — and up to four walk behind you in
single file, stepping where you stepped so the line files through a doorway one
at a time. Walk into one and you swap places rather than talk. The same word
that asked them along sends them home.

Magic is a **language**. A spell is a phrase — `IN LOR`, `VAS MANI` — and you can
cast it when you know the runes it is made of, which are words in the same
vocabulary the conversations use: ask Nystul about the RUNES. Reagents are
ordinary things you carry, and they grow where they would grow — ginseng under
trees, ash at the foot of the cliffs. It is all in `assets/spells.txt`, and what lives in the hills is all in
`assets/bestiary.txt` — stats, behaviour and loot alike.

Conversation is a **vocabulary, not a dialogue tree**. Everyone starts knowing
two words, NAME and JOB; a topic can be asked when you know one of its words,
and answers hand over words you did not have. So a rumour heard from the
merchant is what makes the gatekeeper worth asking, and there is no flag system
anywhere making that happen. It is all in `assets/dialogue.txt`, which holds one block per person — their
sprite, their day and everything they say. Adding a townsperson is an edit to a
text file, and `gg_game.c` does not know anybody's name.

People leave things about their houses, and apples fall under trees. Everything
you carry has a weight, and thirty stone is all anyone can manage — a bar of
silver is four of them, so a hoard is a decision rather than a total. Ready a
torch and it lights the room; put it away and the night closes back in.

Sound is generated rather than recorded: `tools/make_sounds.py` writes every
effect and every tune out of arithmetic, so there is no third-party audio to
license or attribute. The five tunes are *written down* — a chord progression, a
bass, an inner voice and a melody in phrases, on a plucked string and a pad in a
room — and each loops seamlessly rather than fading out. Music follows the
region and the hour and crossfades when either changes; `--music FILE.wav`
records the lot if you would rather hear it than play to it.

### The editor

`gigantima_editor` is a second binary on the same core. Eight tools down the
left — ground, things, litter, people, their day, regions, start, ways out —
picked with `1`..`8`, the brush changed with `[` and `]` or the wheel, left
button to paint and right to rub out. `Z` and `Y` take a mistake back and put it
again, thirty deep, with a drag counting as one step; `F` fills the run of
ground under the cursor. `C` lists what is wrong with the map: a start inside a
lake, somebody standing in a wall, a schedule point in a mountain.

`O` opens any map on the machine — it lists them, so nothing has to be typed
that is not already known — `Shift+S` saves under another name, `E` names
whatever the current tool is about, and `L` says where the next way out leads.

```
gigantima_editor [--open FILE.ggmap] [--size W H] [--tool N] [--shot FILE.bmp]
                 [--ask open|save|name|link] [--export FILE.map.txt]
```

A map it writes is played with `gigantima --map FILE.ggmap`, and nothing about
the game had to change for that to be true. Maps link to each other with ways
out — a tile, a map, and where to arrive — so `assets/maps/vale.map.txt` and
`assets/maps/stones.map.txt` are one world with a road between them. Everything
about the party crosses with you; a map you leave, though, forgets what you did
there.

### Command line

```
gigantima [--profile NAME] [--seed N] [--play] [--debug]
          [--scale N] [--fullscreen] [--no-rumble]
          [--new] [--turns N] [--listen MS] [--map FILE.ggmap] [--screen NAME]
          [--at X,Y] [--time HH:MM] [--shot FILE.bmp] [--shot-at TURN]
```

`--seed` makes a run reproducible: the world, the town layout and every random
decision come from that one number. `--shot` runs the world forward with no
window, writes a single frame and exits, which is how CI checks that the game
can still find and draw its own art — it photographs whatever the other flags
select, so `--shot` alone gets the title screen and `--debug --shot` gets the
debug view; `--screen NAME` opens a named page — `title`, `profiles`, `name`,
`options`, `play`, `pause`, `pack`, `talk`, `party`, `fight`, `spells` or `journal` — so each one can be photographed in turn.
`--at X,Y` and `--time HH:MM` put the avatar somewhere specific at a specific
hour, because verifying a change to how water is drawn means getting to water,
and checking that a lit room differs from the street at night means getting to
night without walking there. `--map` plays an authored `.ggmap`,
which is how the shoreline corners were checked against coastlines the
generator would smooth away.

## Layout

```
src/core/       the simulation. No renderer, no window, no input, no audio.
src/gfx/        atlas textures, the world renderer, the bitmap font
src/ui/         menus and screens, status band, conversation panel
src/platform/   asset and save paths, keyboard and gamepad
src/debug/      the debug window
src/editor/     the map being edited, and everything that can be done to it
src/frontend/   main.c per executable - the game and the editor
tools/          the art baker, the sound baker, and the sheet scanner
assets/         the atlases and sounds, and the content: dialogue, spells,
                bestiary, quests
ext/            pinned submodules — see ext/README.md
docs/           the completion plan and the project status
```

`src/core/` not knowing how anything is drawn is a rule, not a habit: it is
checked at configure time by `cmake/Layering.cmake`, which fails the build if
core acquires an include or an SDL type from any other layer. That is what
keeps the simulation headless, the unit tests windowless, and a re-bake of the
art unable to change the behaviour of a single turn.

## The art

Baked from **[LPC Revised](https://github.com/ElizaWy/LPC)** by
`tools/make_atlas.py`, which is the only thing in the repository that knows an
LPC path — the game knows rectangles. Characters are composited from five
independent layers each, so the cast is a list of choices rather than a list of
images.

Terrain tiles were picked by measurement rather than by eye:
`tools/scan_sheet.py` reports which cells of a sheet are fully opaque *and*
butt cleanly against a copy of themselves, and only those can serve as ground.
The LPC terrain sheets are laid out as 3×3 blob rings, so a plausible-looking
pick lands on a tile with a transparent hole or an edge baked into it — which
is invisible in a screenshot of one tile and glaring in a field of them.

Art is **OGA-BY 3.0 / CC-BY 3.0** and the font is **SIL OFL 1.1**; both require
attribution, which is generated into `assets/CREDITS.md`. See
`assets/LICENSES.md`.

## Licence

**GPL-3.0** — see `LICENSE`. That covers the code. The art and font keep their
own licences, recorded in `assets/LICENSES.md`.
