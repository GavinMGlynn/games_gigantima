# gigantima

An Ultima VI-style seamless-world RPG in C23 and SDL3.

[![ci](https://github.com/GavinMGlynn/games_gigantima/actions/workflows/ci.yml/badge.svg)](https://github.com/GavinMGlynn/games_gigantima/actions/workflows/ci.yml)

> **Status: early.** The engine works end to end — a generated continent with
> autotiled shorelines and terrain, a town of houses you can walk into,
> townsfolk who path around the buildings to keep daily schedules, a world
> clock with lit rooms at night, and a HUD. There is no story, no combat, no
> magic, no inventory interaction, no saving and no sound yet.
> `docs/PROJECT_STATUS.md` is the single source of truth for what works;
> `docs/COMPLETION_PLAN.md` is the road to the rest. Nothing here claims more
> than it has earned.

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

## Playing

| | |
| --- | --- |
| Arrows / WASD / keypad | walk — the keypad also gives the four diagonals |
| `T` | talk to whoever you face |
| `L` | look at what you stand on |
| `O` | open |
| `Space` / `.` | wait a turn |
| `F5` | save |
| `F1` | debug window |
| `F11` / `Alt+Enter` | fullscreen |
| `Esc` | quit |

A gamepad works for all of it, hot-plugged, with the sticks and the d-pad both
driving movement.

`--profile NAME` picks whose game this is. Each profile keeps its own world, and
running again resumes it — the game saves on the way out, so `--new` is what you
pass to start over.

Walking into somebody talks to them, as Ultima VI did — the common case needs
no key at all.

### Command line

```
gigantima [--profile NAME] [--seed N] [--play] [--debug]
          [--scale N] [--fullscreen] [--no-rumble]
          [--new] [--turns N] [--map FILE.ggmap]
          [--at X,Y] [--time HH:MM] [--shot FILE.bmp] [--shot-at TURN]
```

`--seed` makes a run reproducible: the world, the town layout and every random
decision come from that one number. `--shot` runs the world forward with no
window, writes a single frame and exits, which is how CI checks that the game
can still find and draw its own art — it photographs whatever the other flags
select, so `--shot` alone gets the title screen and `--debug --shot` gets the
debug view. `--at X,Y` and `--time HH:MM` put the avatar somewhere specific at
a specific hour, because verifying a change to how water is drawn means getting
to water, and checking that a lit room differs from the street at night means
getting to night without walking there. `--map` plays an authored `.ggmap`,
which is how the shoreline corners were checked against coastlines the
generator would smooth away.

## Layout

```
src/core/       the simulation. No renderer, no window, no input, no audio.
src/gfx/        atlas textures, the world renderer, the bitmap font
src/ui/         title screen, status band, conversation panel
src/platform/   asset and save paths, keyboard and gamepad
src/debug/      the debug window
src/frontend/   main.c per executable
tools/          the art baker and the sheet-scanning tool it was built with
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
