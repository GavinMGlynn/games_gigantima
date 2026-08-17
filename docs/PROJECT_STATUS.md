# Project status

The single source of truth for **what works**. Updated in the same commit as
the code it describes. If this file and the code disagree, the file is the bug.

**"Working" means 100% of what the part claims.** Anything less is written with
the missing part named *first* — never a bare "the renderer works" with the gap
buried in a later clause. See `CLAUDE.md`.

---

## In one line

An engine, not yet a game: you can walk a generated continent, enter a town, go
inside its houses, and watch eight townsfolk path around the buildings to keep
daily schedules under a day/night cycle. There is **no story, no combat, no
magic, no usable inventory, no saving, no sound, no menus and no editor**.

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

Movement is **A\*** over the tile grid, budgeted at 400 expanded cells per
resident per turn. Integer step costs (10 orthogonal, 14 diagonal) and an
octile heuristic, so the whole search stays in integers and a seeded world
stays reproducible — including the heap's tie-break, which falls back to the
cell index precisely so two equally good paths resolve the same way on every
machine.

Two properties are worth naming. Diagonals **do not cut corners**: a diagonal
step needs both of its orthogonal neighbours passable, or an actor slips
between two walls that meet at a point and leaves a sealed room. And an
**unreachable target still produces a step**, toward the closest cell the
search reached — that is what makes a resident whose way is blocked edge around
the obstacle rather than stand still.

Greedy stepping is kept as the fallback for the one case A\* cannot help with:
an actor boxed in on every side, where a random legal step is what unpicks a
knot in a doorway.

`gg_path` knows nothing about maps or actors — passability arrives as a
callback — so it is tested against hand-drawn mazes with no world at all, and
a failure points at the search rather than at whatever the generator happened
to produce.

Over a full simulated game day no townsperson ends up standing in terrain,
sharing a tile with another, or standing on the player.

*Verification: the pathfinding tests listed in `COMPLETION_PLAN.md`, plus
`townsfolk_never_walk_into_terrain` (a full 1440-turn day),
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

### Light

Lit **per tile**, not by one quad over the whole view. Tile granularity is not
a compromise: Ultima VI lit by the tile too, and a circle of torchlight
stepping outward a tile at a time is the look.

**Light comes from objects, not from rules.** A prop carries a radius, and a
cell takes the brightest of the sky, the avatar's carried light, and every
emitter within reach. A room is bright because somebody put a lamp in it — and
the lamp is there in the room, visible, in its own pool of light. The town
square has a campfire for the same reason.

An emitter reaches only cells on **its own side of a wall**. Rather than trace
a line, which at this tile size buys little and costs a lot, indoor emitters
light indoor cells and outdoor ones light outdoor cells, with a doorway
counting as both. Without that every house wore a halo at night, because a
room's lamp lit straight through the brickwork.

Emitters are scanned from the map rather than kept in a list. The largest
radius is six, so the box is 13×13 at worst — and a list would have to be
rebuilt every time the map changed, which the editor will do constantly.

Night is blue rather than black, and bottoms out at three fifths rather than
opaque: a pure black wash reads as the renderer having broken, and the player
has to be able to walk home.

Still missing: a torch that **burns down** and is consumed. That is an
inventory behaviour rather than a lighting one, and it is named under the
inventory plan item.

*Verification: `a_room_is_lit_at_midnight_and_the_street_is_not`,
`the_avatar_carries_a_light_that_falls_off`, `noon_lights_the_whole_outdoors`.*

### Shorelines

Water is autotiled. Three edge sets are baked — a grass bank, a beach, and the
shallow-to-deep drop-off — and the renderer picks one of each set's nine pieces
from the four orthogonal neighbours. The set is chosen per cell from the
neighbouring terrain, so one shore of a lake can be beach and the far side
grass; both sets share an identical water centre, so they meet invisibly.

Which piece to draw is **not** stored in the map. It is derived at draw time,
so the editor will be able to paint water as water and have the coast re-shape
itself around an edited cell with no bookkeeping.

The generated water is smoothed first, by two cellular passes with asymmetric
thresholds — four neighbours to stay wet, six to flood. That is not cosmetic:
every single-tile spur the ellipse jitter leaves needs a concave corner piece,
and **the LPC sheets carry none**, so each one would render as a square notch.
Equal thresholds were tried first and oscillate, the same cells flipping every
pass.

**Concave corners are composed at bake time**, because the sheets carry none.
The overlap of two adjoining straight edges is exactly the nub an inner corner
needs, and the pixels come from the matching outer corner, so the art in it is
real corner art rather than a synthesised blob. The blend is by a soft weight,
not a binary mask: two of the three sets have hard-edged banks where a mask
would do, but the shallow-to-deep set is a gradient, and a mask turned its
corners into hard-edged rectangles — visibly worse than the missing corner.

**Land meeting land** is a second, transparent pass over the base tile: grass
bleeds a verge onto dirt, road, sand, desert, farmland and mountain. Rock is
included because the alternative is a hard staircase where the mountains meet
the grass, which reads as a tiling artifact rather than as a cliff.
`GG_TILE_CLIFF` is deliberately excluded — it stands in for masonry as well as
rock, and a grass fringe up every building is worse than a hard edge on a few
loose cliffs.

The overlay returns a **set** of pieces rather than one. A single piece cannot
say "grass on both sides", which is what a one-tile road and a lone patch both
are; choosing one left roads grassy down the west side and hard-edged down the
east. Overlays are transparent, so one straight piece per grassy side
composites into the right shape, and the concave pieces cover only what a
straight piece cannot reach.

**Every terrain pair is covered.** The sheet has an overlay ring for grass and
for nothing else, so the rest are synthesised at bake time by filling the grass
ring's shape with each terrain's own fill tile — the ring's edge is a jagged
organic fringe, and at this tile size it reads as a natural boundary rather
than as grass blades, so sand and dirt wear it convincingly.

A **rank** decides which way round each boundary is drawn: softer, more
vegetated ground bleeds onto barer ground and never the reverse, so a boundary
is drawn once and from one side. Equal ranks do not transition at all, which
keeps grass and worn grass from fringing each other. Water is excluded because
its edge sets already carry a bank, and `GG_TILE_CLIFF` because it stands in
for masonry — a grass fringe up every building would be worse than a hard edge
on a few loose cliffs.

*Verification: `a_square_lake_selects_all_nine_edge_pieces` (all nine pieces of
a square lake, each exactly once), `water_at_the_map_edge_draws_no_shoreline_against_nothing`,
`deep_water_is_never_adjacent_to_land` (25 seeds — the deep set has no
land-boundary art, so this must never happen), `the_coastline_has_no_isolated_puddles`
(25 seeds).*

### Buildings

Exteriors only. The three pre-assembled houses from LPC's
`Structure/Structures/` — brick, gabled brick and panelled — with roofs,
chimneys, windows, doorframes and porches, drawn as props.

A prop's **footprint is smaller than its sprite**, which is what makes this
work: a house is 8×7 tiles but stands on 6×3, so the roof overhangs the rows
behind it and the player walks there. The anchor is the footprint's bottom
centre, derived in the baker from the footprint rather than declared, so the
two cannot disagree. `gg_map_place_prop` is the single implementation of
"put a building here", shared by the generator and, later, the editor.

The terrain table no longer carries masonry: `GG_TILE_MOUNTAIN` used to stand
in for a wall.

**You can go inside.** The footprint is the *whole* building, roof depth
included, because that depth is the room: a house drawn in three-quarter view
shows its front wall along the bottom and its roof over the space behind, and
that space is the interior. The perimeter becomes brick wall, the inside a
wood floor flagged `GG_CELL_INDOORS`, and the door a hole in the front wall.
Rooms are furnished with barrels, crates and tables, kept clear of the door
column.

The cutaway is the whole mechanism: **the room is walkable at all times, and
only the roof is hidden** when the player is inside it. Nothing about collision
depends on where the player or the camera is, so walking out of a house can
never put them in a wall. Skipping the sprite is all the renderer does — the
floor and walls beneath are ordinary terrain, already drawn.

One bug worth recording: the panelled house's door was in a *corner* column of
its footprint, so stepping through it landed in the side wall rather than the
room. It looked perfectly correct from outside and was only found by walking
through it in a screenshot. The baker now refuses a door outside
`1 .. foot_w-2` for any building.

*Verification: `a_placed_building_blocks_its_whole_footprint`,
`a_buildings_doorway_is_walkable` (all three kinds),
`you_can_walk_behind_a_building`,
`a_building_that_does_not_fit_changes_nothing`,
`the_generated_town_has_buildings_with_reachable_doors` (20 seeds).*

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
| Indoor lighting | the light quad covers the whole view; `GG_CELL_INDOORS` is set but unused |
| Party | the avatar is alone |

---

## Known approximations

Deliberate, documented, with the cost to close:

- **Roads stop at water** rather than bridging it. Paving the water was what
  put a brown causeway across the middle of the lake; a bridge prop is the
  proper fix.
- **The map's `region` byte is written and never read** beyond the region
  bounding boxes. It is there for the editor.
