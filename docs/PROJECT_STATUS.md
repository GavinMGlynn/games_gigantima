# Project status

The single source of truth for **what works**. Updated in the same commit as
the code it describes. If this file and the code disagree, the file is the bug.

**"Working" means 100% of what the part claims.** Anything less is written with
the missing part named *first* — never a bare "the renderer works" with the gap
buried in a later clause. See `CLAUDE.md`.

---

## In one line

An engine, not yet a game: you can walk a generated continent, enter a walled
town, and watch eight townsfolk keep daily schedules under a day/night cycle.
There is **no story, no combat, no magic, no usable inventory, no saving, no
sound, no menus and no editor**.

---

## What runs

### Build and platform

The four target platforms are gated at configure time and all four are in CI:
Linux x86_64 on both the Debian and RHEL families, Windows x64 under MSVC, and
macOS arm64. 32-bit is refused with a message rather than silently produced.
C23 is required and `nullptr` is *probed* rather than inferred from a version
number, so a compiler that gains it later stops needing the shim automatically.

`-Werror` is on in **every** build type, with `-Wall -Wextra -Wshadow
-Wconversion -Wsign-conversion -Wpointer-arith -Wstrict-prototypes
-Wmissing-prototypes`, applied to first-party targets only. Vendored code in
`ext/` builds under its own settings.

*Verification: `cmake --preset` and `ctest --preset` succeed for every preset;
the CI matrix is green on all four platforms.*

### The layer boundary

`src/core/` cannot include from `gfx`, `ui`, `audio`, `debug`, `platform` or
the frontends, and cannot name `SDL_Renderer`, `SDL_Texture`, `SDL_Window`,
`SDL_Surface`, `SDL_Event`, `SDL_Gamepad` or `SDL_AudioStream`. This is
enforced by `cmake/Layering.cmake` at configure time, not by review.

That is what makes the unit tests meaningful: they run with
`SDL_VIDEODRIVER=dummy` and never create a window, so the whole simulation is
proven headless on every run.

*Verification: `gigantima: src/core/ layer boundary clean` on the configure
line; probed with a deliberate violation, which fails with a named message.*

### World

A map is one grid of 4-byte cells — terrain, prop, flags, region. There is one
map class; "town", "dungeon" and "wilderness" are regions of it. Passability
comes from a terrain table the editor will be able to read, plus per-prop
blocking, plus an off-map-is-a-wall rule that makes the border need no ring of
blocking tiles.

Generation is **deterministic in its seed** — the same number always produces
the same continent, town layout, forest and road. It produces a mountain spine,
a desert, a lake with a sand rim and reeds on the shoreline, clumped woodland,
a walled town of ten buildings around a square, and roads connecting the town
to the map edge.

The generator is **not a serious world generator**. It exists so every
subsystem has real data to run against; hand-authored content arrives with the
editor.

*Verification: `generation_is_reproducible_from_its_seed`,
`different_seeds_produce_different_worlds`,
`the_generated_start_tile_is_always_walkable` (60 seeds),
`off_map_tiles_are_never_walkable`, `water_and_mountain_are_impassable`,
`a_prop_blocks_its_tile_but_ground_cover_does_not`.*

### Map file format

Version 1, little-endian, fixed-width. Round-trips byte for byte. A file that
is not a map, is truncated, is a future version, or claims implausible
dimensions is rejected with a named message and without leaking the partial
allocation. Terrain and prop ids are clamped on load, so a corrupt file cannot
index off the end of the tile tables.

**Nothing writes one yet except the tests.** The editor that is the point of
having a format does not exist.

*Verification: `a_saved_map_reloads_byte_for_byte`,
`loading_a_file_that_is_not_a_map_fails_cleanly`.*

### Turns and the clock

The world advances inside `gg_game_act` and nowhere else. One move is one turn
is one game minute, except on rough ground which costs two. A day is 1440
minutes and wraps into a day counter. Daylight is a triangle wave peaking at
noon, squared to flatten midday and steepen dawn and dusk — a linear ramp spent
far too much of the day in a half-light that looked like neither.

**A blocked move costs no turn at all** — deliberately, or a player could
starve by walking into a rock — but it still turns the player to face what they
walked into.

*Verification: `the_clock_wraps_at_midnight_and_advances_the_day`,
`daylight_peaks_at_noon_and_bottoms_at_midnight`,
`walking_into_a_wall_costs_no_turn`,
`a_legal_move_advances_the_world_by_one_turn`.*

### Townsfolk

Eight named townsfolk, each with a four-entry daily schedule and a greeting.
The schedule lookup wraps the day, so somebody who goes to bed at 22:00 is
still in bed at 02:00. Schedule points that land inside a wall are walked
outward at placement time until they are somewhere the NPC can actually stand —
without that, an NPC shoves at a wall all day.

Movement is **greedy, not pathfinding**: one step toward the target, diagonal
first, with a random legal step when boxed in so a knot in a doorway unpicks
itself. Over a full simulated game day no townsperson ends up standing in
terrain, sharing a tile with another, or standing on the player.

Greedy movement is a deliberate approximation, not a finished module: an NPC
whose target is behind a wall will press against that wall. Real pathfinding is
a named plan item.

*Verification: `townsfolk_never_walk_into_terrain` (a full 1440-turn day),
`two_townsfolk_never_share_a_tile`,
`the_player_never_shares_a_tile_with_a_townsperson`,
`a_schedule_before_its_first_entry_uses_the_last`.*

### Rendering

Three passes: terrain, then props and actors together in one list sorted by the
row they stand on, then a light quad tinted by the world clock. Sorting props
and people together in world-pixel space is what lets the player walk behind a
tree canopy and in front of its trunk with no per-object special cases.

**The camera is in pixels, not tiles.** This was a real bug: a tile-quantised
camera left the avatar interpolating smoothly while the entire world teleported
32 px whenever it crossed a boundary, which reads as the game stuttering. It
passed every test and was invisible in a screenshot — it took frames pulled
from a screen recording to see. The fix draws terrain at a sub-tile offset with
one extra row and column to cover the sliver exposed at each edge.

The walk-cycle phase now carries **across** tile boundaries and settles to the
standing pose only after a few idle ticks, rather than restarting at every
tile, which had the avatar playing the first half of the cycle over and over.

*Verification: `the_camera_moves_in_sub_tile_steps_while_walking`,
`the_camera_clamps_to_the_map_edges`, `a_finished_step_lands_exactly_on_the_tile`,
and the CI smoke test, which renders a frame headless and fails if the game
cannot find its own art.*

### Input

Keyboard and gamepad, read every frame and merged, so either can be used at any
moment. Arrows, WASD and the numeric keypad walk — the keypad because it is the
only layout that reaches all four diagonals as single keys. Gamepads hot-plug.
Rumble is capability-checked and disableable.

Held movement repeats after a delay. **The repeat rate is tied to the step
animation** (`GG_REPEAT_RATE == GG_STEP_TICKS`): at the original 5 ticks against
a slide of 8, the next move landed mid-slide and re-anchored the interpolation
on the tile just left, so the sprite teleported backwards on every step of a
held walk.

### Art pipeline

`tools/make_atlas.py` bakes four PNG atlases, two generated headers and one
generated source file out of the LPC Revised submodule. Characters are
composited from five independent layers each — body, head, legs, feet, torso,
hair — so the cast is a list of choices rather than a list of images.

Terrain picks were **measured, not eyeballed**. `tools/scan_sheet.py` reports
which 32×32 cells are fully opaque *and* butt cleanly against a copy of
themselves; the LPC sheets are 3×3 blob rings, so a plausible-looking pick
lands on a tile with a hole or an edge baked in, which is invisible in one tile
and glaring in a field.

Attribution is generated, not transcribed: `assets/CREDITS.md` reproduces the
`Credits.txt` of every source directory the bake drew from, so it cannot drift
from the art it describes. That is a licence condition of OGA-BY, not
documentation.

The outputs are committed, so a clone builds and plays without the 766 MB art
submodule. **CI does not verify that the committed atlas matches what the baker
would produce** — that would need the art submodule in CI. Named in the plan.

### Debug window

A separate window (`--debug`, or F1), showing a pixel-per-tile overview of the
whole map with the actors, the player and the camera footprint on it, plus the
clock, the light level, the player's position and terrain, and the townsfolk
roster with each one's distance from where their schedule says they should be.
A roster that is permanently amber is the tell that a schedule point landed
somewhere unreachable.

The overview is a streaming texture rather than one draw call per tile: 30,000
`SDL_RenderPoint` calls a frame would halve the frame rate of the window meant
to be diagnosing it.

### Sanitizers

The debug presets build with `-fsanitize=address,undefined`. Both the test
suite and a headless render run clean under ASan, UBSan and LSan, with
suppressions covering only third-party process-lifetime allocations
(`.lsan-suppressions.txt`); nothing in `src/` is suppressed.

*Verification: the `sanitizers` CI job runs both.*

---

## What does not exist

Named plainly, because a reader should not have to infer absence:

| | |
| --- | --- |
| Story, quests, journal | nothing at all |
| Combat | nothing at all |
| Magic | nothing at all |
| Inventory | a table of six counters shown in the HUD; nothing can be used, picked up or dropped |
| Conversation | a greeting and a panel. No keywords, no topics, no branching |
| Saving and profiles | nothing. `--profile` only names the avatar |
| Sound | nothing. `ext/sdl_mixer` is pinned but not linked |
| Title/menus/setup pages | a title screen with a prompt. No menus, no options, no profile pages |
| Level editor | nothing. The map format exists for it |
| Buildings | rectangles of wall with a door. No roofs, windows, interiors or furniture |
| Shorelines | flat tiles butted together; no autotiling, so water meets land with a hard edge |
| Indoor lighting | the light quad covers the whole view; `GG_CELL_INDOORS` is set but unused |
| Pathfinding | greedy stepping only |
| Party | the avatar is alone |

---

## Known approximations

Deliberate, documented, with the cost to close:

- **Greedy NPC movement.** Cheap and adequate inside a town; fails when a
  target is behind a wall. Closing it is A* over the tile grid with a
  per-turn budget.
- **Whole-view light quad.** Ignores `GG_CELL_INDOORS` and light sources.
  Closing it needs a light model — emitters, radius, and a per-tile blend.
- **Buildings as flat rectangles.** The LPC Structure sheets carry roofs,
  windows and doorframes in 3×3 arrangements this project has not decoded yet.
- **`GG_TILE_MOUNTAIN` doubles as masonry.** The pale cobble reads as stone
  wall against the dark interior, which is why it was chosen over the striated
  cliff face — but it is a terrain tile standing in for a structure tile.
- **The map's `region` byte is written and never read** beyond the region
  bounding boxes. It is there for the editor.
