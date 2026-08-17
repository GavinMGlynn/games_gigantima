# Project status

The single source of truth for **what works**. Updated in the same commit as
the code it describes. If this file and the code disagree, the file is the bug.

**"Working" means 100% of what the part claims.** Anything less is written with
the missing part named *first* — never a bare "the renderer works" with the gap
buried in a later clause. See `CLAUDE.md`.

---

## In one line

An engine with most of a game in it. From a title screen you start or resume a
named journey; walk a generated continent; go inside a town's houses; pick up
what people have left lying about and eat it, carry it or set it down; ask the
eight townsfolk what they know and collect the words that unlock what the rest
of them know; take two of them along, walking in single file behind you; learn
the runes that make a spell and gather the herbs that pay for it; and find out
what the brigands in the hills make of all that. Hold a torch and it lights the
room, and the world has a voice - footfalls, blows, and a tune that follows the
region and the hour. Every page works from a gamepad alone, naming included.

**There is no story worth the name.** The machine for one works and four small
quests run on it, but they are a demonstration rather than a plot. That is the
one thing still keeping this an engine.

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

### Many maps

A map carries **ways out**: a tile, the name of another map, and where to arrive
in it. Stepping on one crosses over. The vale and the standing stones ship as
two authored maps with a road between them, both written by the editor.

**The simulation names the map; the frontend says where that map lives.** A way
out sets a request — the same arrangement `want_save` already had — and the
frontend resolves `maps/<name>` under the assets, falling back to the
preferences directory so a map you authored yourself is playable without
installing it. `src/core` never learns where content is kept.

What crosses over is **the party and everything about it**: who they are, what
they carry, what they know, how far along every quest is, every flag, the hour,
the day and the RNG. What is replaced is the world — the map, the people who
live in it, and what is lying about. The crossing costs no turn of its own; the
step onto the gate is an ordinary step and costs an ordinary turn.

A failed crossing **changes nothing**. The new map is read before the old one is
let go, so a way out pointing at a map that is not there leaves the world
exactly as it was and still playable.

**A person in the book lives in a named place.** `home Britain`, and their
hours are offsets from that place's centre; a map with a region of that name is
where those offsets mean something and is peopled by them, and a map without one
has never heard of them. That is what keeps Britain off the hillside at the
standing stones — the first crossing brought eight townsfolk along and stood them
there, which is what the screenshot showed — and what keeps it out of the next
town somebody authors. A map that names somebody itself keeps its own version of
them: the vale's Iolo stands at his stall, not at an offset from the square.

*Verification: `walking_between_two_maps_takes_everything_with_you` (the plan's
own, over two maps authored through the editor's own operations, checking the
pack, the readied torch, the companion, the clock, the day, the RNG, the turn
count, the words learned and a flag - and back again),
`a_way_out_that_leads_nowhere_is_refused`,
`the_editor_refuses_a_gate_nobody_can_reach`. Three were checked by breaking the
rule they pin: leaving the party behind, freeing the map on a failed crossing,
and reseeding the RNG on arrival. Plus a `--shot` frame of the walk from the
vale to the stones, taken in CI.*

### A world that remembers

**A map you leave is kept as it was left.** Up to `GG_VISITED_MAX` (12) of them
are held in mind at once, under the leaf name a way out calls them by; walking
back into one takes it out again rather than reading disk, so the return journey
costs no I/O and finds the map unchanged. The oldest is let go when the twelfth
arrives, which costs a re-read and not a crash.

**The ground is the map's; the people are kept beside it.** A `gg_visited` holds
the map - terrain, ways out and everything lying on the floor - and, separately,
whole `gg_actor`s for everybody standing in it. Separately because a map records
who *lives* somewhere and this has to record who is *standing* there, which is a
different list once somebody has been wounded, has wandered off their doorstep,
or has fallen. Keeping actors rather than the map's own placements is also what
carries a creature's bestiary index, its health and its energy, so a brigand you
left with two hit points is still on two hit points when you come back.

Three rules make it a memory rather than a hoard:

- **The fallen are not written down.** They are the only thing a return is meant
  to forget, and an inactive actor still occupies a slot - a world that kept
  them would fill with corpses one crossing at a time.
- **The party is not written down.** They are walking away with you, and leaving
  them in the map's list too puts a second copy of every companion on the floor
  on the way back.
- **The map's own placements are dropped** once the people beside it are
  recorded, for the same reason.

The save carries all of it (**version 9**): the leaf name of the map underfoot,
then each remembered map and its people, through the same `gg_map_write` and
`actor_write` the rest of the file uses. Greetings are pointers into the
dialogue book, so they are rebuilt by name on load for the remembered people as
well as for the ones underfoot.

*Verification: `a_map_you_leave_is_as_you_left_it` - silver left on the floor,
bread taken off it, a named brigand killed, and a resident wounded and moved;
then out to another map, something dropped there too, and back. All four hold,
the survivors are the same number, and none of the dead came back as an empty
slot. The whole world is then saved, loaded into a fresh game, compared field by
field by `games_match` (which now compares the remembered maps and their people
too), and the second map's floor is checked by crossing into it again. Four were
checked by breaking the rule they pin: always re-reading from disk, keeping the
fallen, writing no remembered maps to the save, and dropping the party rule.
Clean under ASan, UBSan and LSan.*

*And on the real game: `--screen return` puts silver on the road, walks the
Avatar out of the vale to the standing stones and back, and photographs the
tile. The frame shows the ingot still lying there and the log says so; the same
run is a CI smoke test.*

### The editor

A second binary on the same core, `gigantima_editor`. Seven tools — ground,
things, litter, people, their day, regions, start — with the left button
painting and the right button rubbing out whatever the current tool places, so
one tool is both brush and rubber and there is no erase mode to be stuck in.

**The document is separate from the window.** `src/editor/gg_edit.c` holds the
map and every operation on it, with no renderer and no event in it;
`src/frontend/editor/main.c` is a window, a mouse and a palette. That split is
what makes the item's own verification possible: a test authors a whole map by
making the same calls a person makes with a mouse, saves it, and plays it — which
is not a thing that can be checked by taking a picture of a window.

Two things it does that a naive editor would not:

- **Rubbing out a building clears its whole footprint.** A prop blocks more
  cells than the one it stands on, so removing only the anchor leaves invisible
  walls where a house used to be — a bug nobody can see. Clicking any wall finds
  the prop whose footprint covers it and removes the lot.
- **It says what is wrong.** A start inside a lake, somebody standing in a wall,
  a schedule point in a mountain — the last of which has an NPC shove at rock
  all day, which is exactly the failure the generator learned to walk its own
  points out of. `C` shows the list.

Placement goes through the same `gg_map_place_prop` the generator uses, so a
building's walls and doorway are laid out identically however it got there, and
painting water sets the water flag from the terrain table rather than by hand.

**Map format version 3 carries people**, so a map records who lives in it and
what their day is. A map that names anybody is the whole of who lives there —
the generator's eight townsfolk stay out of an authored map rather than turning
up alongside. Moving those eight *out of C* is the next item; the format that
will hold them is here.

*Verification: `a_map_authored_in_the_editor_can_be_played` (the plan's own),
`the_editor_rubs_out_what_it_draws`, `the_editor_says_what_is_wrong_with_a_map`,
`a_failed_load_leaves_what_was_open_alone`. Plus `--shot` frames of the editor
with a map open and of the game playing that same map, the editor's now taken in
CI.*

### Map file format

Version 3, little-endian, fixed-width. Round-trips byte for byte — the cells,
the things lying on the ground, and the people with their days. A file that is not a map, is truncated, is
a different version, or claims implausible dimensions is rejected with a named
message and without leaking the partial allocation. Terrain and prop ids are
clamped on load, and an item id past the table or a pile lying off the map is
refused outright, so a corrupt file cannot index off the end of anything.

Version 2 added the ground items, version 3 the people, and no version reads
another. Nothing outside a test ever wrote an older one, and a reader that
guesses at a missing section is worse than one that says no.

The editor writes them, which is what the format was for.

*Verification: `a_saved_map_reloads_byte_for_byte`,
`loading_a_file_that_is_not_a_map_fails_cleanly`.*

### Saving, and profiles

A **profile** is a player: a name, and a directory of their own under the
preferences path. Several people share a machine, and each finds their own
world where they left it.

A save carries the map, every actor with its schedule, stats, side and kit, the
clock, the RNG state, what the Avatar is carrying, the words they have learned
and the footprints their party is following. Carrying the **RNG** is what makes a resumed
game not merely look the same but *continue* the same — the world goes on
making the decisions it would have.

Two things it deliberately does not carry. A greeting is a pointer into a
static table, so each actor stores the index it was built from and the pointer
is rebuilt on load; a pointer in a file is a pointer into the wrong process.
And the loader builds into a local, moving it into place only once every part
has read cleanly, so a truncated file cannot leave a half-loaded world behind.

**Profile names are an allow-list, not a deny-list.** A name becomes a
directory name, and the ways a string can escape a directory are too numerous
and too platform-specific to enumerate — `..`, separators of either slash,
drive letters, trailing dots, reserved device names. Permitting letters,
digits, space, `_`, `-` and `'` closes all of them at once.

Resuming is the default: a player who names their profile and runs the game
expects to be where they left off, so starting over is what `--new` is for. The
game saves on the way out — but **never on a `--shot` run**, because a
screenshot must not overwrite somebody's game. That exclusion left no way to
exercise the save-on-exit path at all, which is what `--turns N` is for: it
plays headlessly and leaves by the ordinary exit.

A small header file sits beside each save so the profile picker can draw a row
— name, day, time, turns, level, where they were — without loading a world.

*Verification: `a_saved_game_resumes_exactly_where_it_was_left` (and that it
stays identical for 60 turns after), `a_resumed_game_can_still_be_talked_to`,
`profiles_do_not_see_each_others_saves`, `a_profile_name_cannot_steer_a_path`,
`a_save_that_is_not_ours_is_refused`,
`the_profile_list_reports_what_was_saved`; plus a CI smoke test that plays,
quits and resumes through the real binary and checks it came back at the right
turn.*

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

**Who lives in the town is a file.** `assets/dialogue.txt` holds one block per
person — their sprite, their day and everything they say — and `gg_game.c`
contains no character names at all. Adding a townsperson is an edit to one file
rather than an edit to a file and a table in C that had to agree with it by
hand, which is what the two used to be.

A person with an `art` line is somebody the world places; one without is a voice
that exists to be talked to in an authored map without the generator putting a
copy of them in every town. Somebody with a sprite and no day is **refused** —
they would stand on the square from dawn to dawn, which is the whole reason a
schedule exists.

The schedule's offsets are **from the town centre**, not tiles: the generator
puts the town somewhere different every seed, so an absolute position would land
these people in a field. A map authored in the editor records absolute positions
instead, and a map that names anybody is the whole of who lives there — the
book's residents stay out of it rather than turning up alongside.

Greetings are rebound after a load **by name, not by the index the save
records**: the book is a text file somebody may have edited between saves, and
an index into it is a promise it never made. Somebody no longer in the book
comes back mute, which is a world where they moved away.

Eight townsfolk in the shipped book, each with a four-entry daily schedule and a
greeting.
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

*Verification, pathfinding: `a_path_goes_around_a_wall`,
`a_path_solves_a_serpentine_maze`, `a_path_never_cuts_a_diagonal_corner`,
`an_unreachable_target_still_moves_toward_it`,
`a_completely_boxed_in_actor_reports_no_step`, `the_search_is_reproducible`,
`a_resident_crosses_the_town_to_a_fixed_target`,
`a_resident_walks_round_a_building_rather_than_into_it`.*

*Verification, townsfolk: `who_lives_in_the_town_comes_out_of_the_book`,
`somebody_who_lives_here_needs_a_day`,
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
`the_avatar_carries_a_light_that_falls_off`,
`a_lamp_indoors_does_not_light_the_street`, `noon_lights_the_whole_outdoors`;
and `--shot` frames of the same town at noon, dusk and midnight, of the campfire
on the square, and of a lit room with a dark street round it.*

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

*Verification, shorelines: `a_square_lake_selects_all_nine_edge_pieces` (all
nine pieces of a square lake, each exactly once),
`water_at_the_map_edge_draws_no_shoreline_against_nothing`,
`deep_water_is_never_adjacent_to_land` (25 seeds — the deep set has no
land-boundary art, so this must never happen),
`the_coastline_has_no_isolated_puddles` (25 seeds); and a `--shot` frame of the
lake.*

*Verification, concave corners: `a_one_tile_island_selects_all_four_concave_corners`,
`an_orthogonal_edge_beats_a_concave_corner`; and an authored `.ggmap` scene of
promontories, notches and islands rendered with `--map`.*

*Verification, land-to-land: `a_one_tile_road_takes_a_verge_on_both_sides`,
`a_lone_patch_takes_a_verge_on_all_four_sides`,
`a_diagonal_only_neighbour_uses_the_concave_piece`,
`a_straight_verge_suppresses_its_own_corner`,
`a_patch_touching_no_grass_needs_no_overlay`, `the_map_edge_grows_no_verge`.*

*Verification, non-grass boundaries: `a_boundary_is_drawn_from_the_softer_side_only`,
`equal_ranks_do_not_transition`, `water_and_masonry_take_no_overlay`; and an
authored scene banding desert / sand / road / dirt / farmland / grass with no
straight-line edge anywhere in it.*

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
`a_buildings_walls_block_but_its_room_does_not`,
`a_building_can_be_walked_into_and_out_of`,
`a_buildings_doorway_is_walkable` (all three kinds, and that each door opens
into the room rather than along a wall), `you_can_walk_behind_a_building`,
`a_building_that_does_not_fit_changes_nothing`,
`the_generated_town_has_buildings_with_reachable_doors` (20 seeds, from the
street and from inside); and `--shot` frames of the street, the doorway and the
furnished room.*

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

The pad feeds **two streams from the same buttons** — world actions and menu
commands — and the frontend drains whichever suits the screen showing. They are
kept apart because A means "talk" in the world and "choose" in a menu, and one
press must not do both. Two rules make that safe:

- `gg_input_take` and `gg_input_nav` share the held-direction timer, so calling
  both in a tick would eat every other step. Exactly one is called per tick.
- `gg_input_forget` runs on every screen change, so the A that chose "Resume"
  does not also talk to whoever is standing next to you.

Start is the exception, kept in a flag of its own (`gg_input_take_pause`)
because it is the one button that means something on every screen — pause in the
world, "that will do" in a menu — and so has to be readable from either drain.

*Verification: `the_pad_feeds_the_world_and_the_menus_separately`.*

### Sound

**The sounds are generated, not vendored, and that is a departure from what the
plan said.** The plan called for a freely-licensed set vendored with its
attribution, as the art was. Nothing exists in the shape LPC has — one coherent
set in one repository — so vendoring would have meant assembling dozens of files
from as many uploaders and carrying a licence and an attribution for each. This
project's standing preference is no dependency over a small one, and
`tools/make_sounds.py` is a few hundred lines of arithmetic that writes its own
WAVs: no licence question, no attribution burden, 2.6 MB in the repository, and
reproducible by anyone who runs it. It is deterministic, so the committed files
and the script cannot drift.

**The ceiling is real and worth stating: these are tones and noise, not
recordings.** A footfall is filtered noise under a fast envelope; the tunes are
a drone and an arpeggio wandering a mode. They establish the mechanism and they
are meant to be replaced by something composed. Replacing any of them is a file
swap — nothing in the code knows how they were made.

**SDL_mixer has been removed.** It was pinned in `ext/` against this work and
turned out not to be needed: SDL3 loads and converts WAVs on its own, so
`src/audio/gg_audio.c` opens one device stream and mixes a music loop under up
to eight one-shot voices itself. About a hundred lines against a dependency on
four platforms — which is what "prefer no dependency to a small one" looks like
when it is acted on rather than quoted.

**The simulation does not know audio exists.** It emits events — a footfall, a
blow, a word learned — into a small bounded queue, and the frontend drains them
and hands them over. That is the arrangement the bump rumble already had,
generalised, so adding a sound is a case in one switch rather than a hook in the
middle of a rule. Overflow drops the newest: a lost footstep is nothing, and a
queue that grows is a leak.

Music is chosen by **where you are and what hour it is** — town or wilderness,
day or night, and one mood for underground where there is no hour — and changes
are crossfaded over half a second rather than cut. The day boundary is the same
one the HUD uses for its words, so what a player reads and what they hear agree.

The baked level was **measured, not guessed**. At the first level a blow peaked
near full scale on its own; three at once plus the music clipped, which showed
up as a peak of exactly 32767 in a headless capture. Baking at a quarter of full
scale leaves room for three effects and a tune together, and no volume setting
clips now.

*Verification: `the_tune_follows_where_you_are_and_what_hour_it_is` (four
combinations all different, the day boundary to the minute, and every answer a
tune that exists), `the_world_says_what_it_did_and_forgets_it` (including that
the queue is bounded and drains once), `every_event_has_a_sound_baked_for_it`,
and `the_options_page_cycles_its_values`, whose check that the sound rows were
dead is now the inverse. Plus the plan's own, in CI: four headless captures
through SDL's disk audio driver at four volume settings — everything off is
silent, music alone and effects alone are each audible, and both together are
louder than either.*

### The bestiary

What lives in the world is a **file**. `assets/bestiary.txt` gives each creature
its sprite, health, level, speed, damage, guard, reach, loot table and how many
of it a map holds — and nothing in C knows what a brigand is. `gg_spawn_foe`
takes a row index and copies it; the generator loops the bestiary placing each
creature's `haunts`; the switch that used to hold two creatures' numbers is
gone.

Two of the fields are **behaviour** rather than statistics, and both are visible
in play:

- `notice` — how near you must come before it cares. Its own, not one number
  shared by everything: a watchful thing stirs at eleven tiles while a dozy one
  beside it does not.
- `flees` — the health below which it would rather be elsewhere. A creature that
  fights to its last point whatever happens is a number that walks at you. A
  cornered one still fights, because that is the other half of fleeing.

`reach` above one is something that fights **at a distance with nothing in its
hands** — the vale's slinger hits from three tiles away, which is a line in a
file rather than a special case in the code.

**Loot is a table**, several lines deep, each with its own chance, rolled
through the game's RNG so what falls is part of the seeded world. A brigand
carries a purse always and a loaf sometimes.

Damage is deliberately **not defaulted**. A creature that can hurt nobody is
scenery, and the loader refuses one — but a default of 1 made that check
unreachable, which the malformed-bestiary test caught.

**Where it is found is a line in the same file.** `haunts 4` is how many of it
a world holds, scattered where there is room; `haunts 1 stones.ggmap 24 22` puts
one of it in a named map on a named tile, the first time that map is walked
into. That is how the storyline's villain stands in the middle of the ring
without a line of C knowing where he is.

*Verification: `a_creature_can_be_added_in_a_file_alone` (the plan's own — every
number read back out of the world, plus the generator placing it without being
told what it is), `a_creature_flees_when_it_is_hurt_enough`,
`a_creature_notices_at_the_distance_its_file_says`,
`a_creature_with_reach_strikes_from_where_it_stands`,
`a_bestiary_that_does_not_parse_loads_nothing` (ten malformed files and a
missing one), `the_vale_is_stocked_with_creatures_that_work`. Three were checked
by breaking the rule they pin — fixing notice at the global constant, never
fleeing, and ignoring a creature's own reach — and the first of those was not
caught until a test was added for it, which is why that one exists.*

### The story

A quest is a list of stages. **Stage k is entered when stage k's condition
holds**, and entering it writes its journal line and may raise a flag. Only the
next stage is ever tested, so a quest cannot skip ahead however the world
changes, and the whole of a quest's state is one number: how far along it is.

**The conditions are things the world already knows how to answer** — a word
you have learned, something you are carrying, a flag another stage raised, how
many have fallen to the party, how many walk with you, and where you are
standing (`when at stones.ggmap 24 22 6`, which is a map and optionally a spot
in it). Nothing here reached into
the simulation to invent new state, because a condition that needs its own
bookkeeping is a condition that can go out of step with the world. The one
number the story did need is a tally of the slain.

**Flags are how quests interlock without knowing about each other.** The
caravan's last stage raises `caravan_understood`; the brigand quest's last stage
waits on that flag and says "It fits: the road was stopped, and these are who
stopped it." Neither quest mentions the other.

The **journal is built, not stored**: the state is the stage number, and every
line is read back out of the book. That means editing a journal line changes
what a saved game says, which is right — the text is content and the progress is
state.

Quests are saved **by name, not by index**, because the book is a text file
somebody may have edited between saves and an index into it is a promise it
never made. A quest that has gone from the book takes its progress with it
rather than handing it to whatever moved into its slot.

A first stage with no condition is **refused**: it would begin the moment a game
did, which is a quest nobody was given. A bare `when` is allowed on any later
stage and means "at once", which is how a stage that only writes a line is
written.

A stage may be marked **`ends`**, and entering it is the end of the story: the
world stops taking orders and the closing panel goes up. What it says is that
stage's own journal line, read back out of the book rather than stored, so the
last words of the game are a line in a text file like every other line it says.
The save carries one bit — whether the story is over — and everything else about
the ending is derived.

*Verification: `a_two_stage_quest_is_completed_and_remembered` (the plan's own,
including that one loaf does not satisfy a condition wanting two, and that a
finished quest cannot run past its end), `a_quest_cannot_skip_a_stage`,
`one_quest_can_open_another`, `killing_things_moves_a_quest_on`,
`a_quest_file_that_does_not_parse_loads_nothing` (nine malformed books and a
missing one), and `the_vale_has_a_story_that_can_be_reached` — which refuses a
quest waiting on a word nobody teaches, a flag nothing raises, a map that does
not ship, or a spot outside it. Plus a `--shot` frame of the journal, taken in
CI.*

### The main storyline

**A caravan out of the north never arrived.** The vale tells you why one person
at a time — Iolo has empty shelves and names the caravan, Shamino watched the
road, Nell has seen a cold light among the standing stones, Nystul knows what a
light with no fire is not, and Gwenno's husband walked north with it. Two of
them will come with you and two of them arm you. The road north has brigands on
it. At the end of it, in the middle of a ring of standing stones, Rugar is
waiting with the caravan's silver on him. Bring it back to the market it was
bound for and hand it to Iolo, and the story ends.

Every part of that is a text file: `quests.txt` has the arc and the closing
words, `dialogue.txt` has what everybody says and what they hand over,
`bestiary.txt` has who is on the road and who is at the end of it. Three
mechanisms were added to make it sayable in those files, and nothing else in the
game knows the story exists.

**Where a creature is found is in the bestiary.** `haunts 4` is how many of it a
world holds, put down where there is room; `haunts 1 stones.ggmap 24 22` is one
of it, in that map, on that tile, the first time the map is walked into. Stocking
happens once per map per world — a map walked back into is remembered, not
re-read — so the villain is never there twice.

**A topic can hand something over, and take something back.** `gives HAMMER 1`
arms whoever asks, but only when the pack holds none already, so a topic asked
twice is not a purse that never empties. `wants SILVER 3` is the other way: the
topic is only offered while the pack holds them, and asking gives them away.
`raises FLAG` is how a conversation reaches the story — the quests watch for the
flag and nothing in the conversation knows which quest cares.

**The road arms you, and that is the balance.** The Avatar leaves the vale with
no weapon; a hammer comes from Iolo, a shield from the gate, a phial from
Katrina, and better of each off the brigands on the way. Rugar's numbers are set
so that this matters: a party that came prepared wins 17 fights in 20, and one
that walked up alone and empty-handed wins none of 20. Both figures are measured
by the test on every run and logged whether it passes or not, because they are
the numbers that say whether the story is beatable and whether beating it takes
anything.

*Verification: `the_whole_story_can_be_played_from_start_to_finish` — a real
playthrough on the shipped content, asking the town for its words, crossing to
the stones, meeting Rugar, killing him with ordinary blows, taking the silver,
walking home and handing it over, then checking the world takes no more orders
and comes back finished from a save file. Five were checked by breaking the rule
they pin: a stage that ends the story not ending it, a topic that wants
something not taking it, a place condition that ignores which map you are in,
one that ignores how near you are, and a topic that gives handing over a fresh
hammer every time it is asked. Plus
`a_party_that_walked_the_road_can_beat_the_man_at_the_end_of_it`, forty fights
on the shipped numbers.*

*And on the real game: `--screen ending` plays the whole story through in one
run — every step of it through the ordinary turn, so a story a player could not
finish is one it cannot finish either — and photographs the closing panel. It is
a CI smoke test, and it checks the log says Rugar fell and the story ended.*

### Magic

Ultima's magic is a **language**, and this is built as one. A spell is a phrase
— `IN LOR`, `VAS MANI` — and it can be cast when the runes it is made of are
known. So the runes live in **exactly the same vocabulary the conversation
system uses**, and are learned the same way: Nystul teaching MANI is the same
mechanism as Iolo teaching CARAVAN, and magic needed no second one. There is no
spellbook to be given and no list to add to — the words *are* the book, which is
why the game stores a spell cursor and nothing else.

**Reagents are ordinary items**, spent out of the ordinary pack. A spell you
cannot afford is one whose herbs you have not gone and found, and they grow
where they would grow: ginseng and nightshade under trees, blood moss on bare
earth, sulphurous ash at the foot of the cliffs. Every reagent appears on every
seed — measured over twelve of them, not hoped for.

Three effects, and each has to be **visible in the world** or it is a control
that does nothing: a light of your own for a number of turns, health back, and
damage at a distance. The light is a second source rather than a replacement, so
letting a spell lapse cannot put out the torch in your other hand.

**Nothing is spent on a spell that does not go off.** Unknown words, too low a
circle, missing reagents, nothing in range — each is refused before the price is
paid, and each says *which* of those it was, because "thou hast not the
reagents" sends a player to count their pack by hand.

`assets/spells.txt` holds all of it, in the dialogue file's format because the
same kind of person reads both. Every complaint from the parser names the file
and the line, and a book that does not parse loads nothing at all.

The spell panel shows the phrase, the name, the circle and the price, greying
what cannot be cast and colouring a reagent you lack — so a player can see *why*
a spell is out of reach without leaving the book.

*Verification: `a_spell_from_a_file_costs_reagents_and_does_what_it_says` (the
plan's own — the effect and the price both observed, and every refusal checked
to spend nothing), `a_phrase_needs_every_rune_in_it`,
`a_spell_of_light_lasts_its_turns_and_then_goes_out` (including that it does not
put out a held torch),
`a_bolt_needs_something_to_aim_at_and_spends_nothing_without_one`,
`a_spell_file_that_does_not_parse_loads_nothing` (ten malformed books and a
missing one), `every_spell_in_the_vale_can_be_learned_from_somebody` — which
refuses a spell needing a rune nobody teaches, and a rune no spell uses —
and `a_spell_of_light_survives_a_save`. Plus a `--shot` frame of the book, taken
in CI.*

### Fighting

Turn-based like everything else: it happens inside `gg_game_act` and nowhere
else. What it adds to a turn is an **order**. Everyone carries an energy budget
that fills by their speed each turn and empties by 100 an action, so an outlaw
at speed 150 acts three times in two turns where a brigand at 100 acts once a
turn — and among those acting, the quickest goes first, ties broken by actor
index so the order is total. That is initiative, and it shows in the log as an
outlaw swinging twice between two of your own turns.

**Every roll goes through the game's own RNG**, which is saved. A fight is part
of the reproducible world rather than something on top of it: the same seed
fights the same battle, which is the whole of this item's verification.

Melee is **walking into somebody**, the same gesture that talks to a townsperson
and swaps with a companion — which of the three it is depends only on whose side
they are on. Ranged is a **readied stone**, thrown up to five tiles along a clear
line, which leaves itself on the ground where it lands. `F` strikes without
stepping, for the throw and for anything you would rather not walk into.

There are **three sides, not two**: yours, the hostiles', and everybody else. A
townsperson cannot be struck at all — a stray blow that starts a riot is a thing
people stop swinging to avoid, and there is no crime system for it to mean
anything to yet.

**The dead leave what they carried**, through the same `gg_ground_drop` a
dropped item uses, so loot is picked up with the same key and saved by the same
code. The Avatar is the exception to dying: they are *not* removed, because the
camera, the HUD and `gg_player` all read through that actor and a player index
pointing at a dead slot is a crash rather than an ending. The game ends instead,
and a save made after that comes back ended — the mode is derived from the
Avatar's health on load rather than assumed to be play.

**Hostiles only notice within eight tiles.** Without that a brigand hunts from
anywhere on the map, and a player who stands still for a few hundred turns is
killed by somebody who set out from the far side of the continent. That is not
menace, it is bookkeeping — and it was caught by the save test diverging, not by
anybody watching.

**This art set contains no monsters.** It is people, head to foot, so what there
is to fight is brigands and outlaws, composited from the same layers the
townsfolk are. A caravan that never arrived is better served by that than by a
wolf. There is no sword that stands on its own either — the sword exists only as
a layer on a swinging character — so the arms are a smith's hammer, a throwing
stone, and a shield cut out of the one frame that shows one whole.

*Verification: `a_scripted_encounter_resolves_the_same_way_every_time` (the
plan's own — five runs of one seed, and a different seed producing a different
fight, so the check cannot pass on a simulation that ignores its dice),
`a_blow_lands_or_misses_by_the_dice_and_never_for_nothing`,
`armour_turns_blows_aside_and_a_weapon_drives_them_home`,
`a_thrown_stone_reaches_across_the_room_and_lands_there`, `a_wall_stops_a_stone`,
`the_quick_strike_before_the_slow_and_more_often`,
`what_falls_leaves_what_it_carried`, `a_townsperson_is_never_caught_in_a_fight`,
`the_avatar_dying_ends_the_game_rather_than_the_world`. Four were checked by
breaking the rule they pin — making every speed equal, clearing line of sight,
dropping no loot, and putting townsfolk on the other side. Plus a `--shot` frame
of an exchange of blows, now taken in CI.*

### The party

Up to four walk with the Avatar. **Recruiting is content, not code**: a topic in
`assets/dialogue.txt` carries `joins`, so who can be asked along — and what they
have to be asked — is written in the book with everything else they say. The
same word sends them home again, which saves every companion needing a parting
topic of their own.

They follow by **walking where the Avatar walked**, not toward where the Avatar
is. Each holds a slot, and slot N walks to the Nth footprint back. That is the
whole formation, and it is what makes the line file through a one-tile doorway
instead of four people all trying to stand in it. The trail is saved, or a
resumed party bunches up on the first step.

**A companion in the way steps aside.** Walking into one swaps the two of you
rather than starting a conversation — without that, a party that has just
followed you through a door can wall you into it. That swap lays a footprint the
same way an ordinary step does, and getting *that* wrong is the one bug this
item produced: pushing the tile the Avatar moved *into* rather than the one they
left sent the companion chasing a square it can never stand on, so it shuffled
sideways on every swap. The test caught it.

**Stats moved onto the actor** — health, level and the party slot — because a
companion needs the same ones the Avatar has, and a second place to keep them is
the split that bites the first time something hits the party. Experience stays
on the game: it is the party's, not one person's.

*Verification: `a_companion_follows_through_a_door_without_blocking` (the plan's
own — in through a real generated doorway, then out and back four times, failing
if the Avatar is ever blocked or shares a tile),
`a_companion_in_the_way_swaps_places` (and that a townsperson who is *not* in
the party is still talked to), `the_line_closes_when_somebody_leaves_it`,
`a_companion_is_recruited_by_a_topic_in_the_book`,
`a_party_survives_a_save_in_order`. Three were checked by breaking the rule they
pin — removing the swap, not closing the gap, and letting a companion keep their
daily schedule — and removing the swap fails with "the avatar could not move,
blocked by its own party", which is the plan's verification stated as a failure.
Plus a `--shot` frame of the line in single file with the party in the HUD.*

### Conversation

Ultima's model, which is a **vocabulary rather than a dialogue tree**. Everyone
starts knowing two words — NAME and JOB. A topic can be asked when you know one
of its words. An answer may hand over a word you did not have. That is the whole
mechanism; there are no flags, no branches and no script.

The gating falls out of it, including the interesting case. Iolo teaches
CARAVAN, and it is *Shamino and Nell* who have something to say about it — so a
rumour crosses a town without anything in the code knowing that a rumour exists.
A per-speaker flag system could not do that without being told to.

**It is all in `assets/dialogue.txt`**, a line-based text format, because the
people who will write these files are writing prose and a binary format would
need a tool before a single word could be put in anybody's mouth. Indentation is
decoration. Every complaint from the parser names the file and the line.

A book that does not parse **loads nothing at all** — a half-loaded book puts
half a conversation in somebody's mouth, which is worse than a silent town. A
person with no entry still greets, so a town half-written reads as one whose
people are quiet rather than as a broken one.

Conversation **costs no time**, which is Ultima's own rule and the reason you
can afford to ask everything. What is saved is the list of words learned — that
is the entire story state. The conversation itself is not saved: it holds a
pointer into the loaded book, and nobody wants to resume mid-sentence.

The panel lists the words this person will answer to, out of the words you know,
rather than asking you to type. That is what makes it work on a pad, and it is
also more discoverable than Ultima's blank prompt ever was.

**Three things a topic can do besides speak.** `teach WORD` hands over a word,
which is the whole of the gating. `gives ITEM N` hands over a thing, and only
when the pack holds none of that kind already, so a topic asked twice is not a
purse that never empties. `wants ITEM N` is the other direction: the topic is
only offered while the pack holds them, and asking hands them over — which is
how something is *given* to somebody rather than merely carried past them. And
`raises FLAG` is how a conversation reaches the story; the quests watch for the
flag and nothing here knows which quest cares.

*Verification: `a_topic_unlocks_only_after_the_word_is_learned` (the plan's
own), `a_word_learned_from_one_person_opens_another`,
`what_was_learned_survives_a_save`,
`a_dialogue_file_that_does_not_parse_loads_nothing` (six malformed books and a
missing one), `synonyms_ask_the_same_topic_and_show_one_label`,
`the_vale_has_a_book_and_every_word_in_it_is_reachable` — which checks that no
topic in the shipped book answers to a word nothing ever teaches, because that
is a question no player could put. Three of these were checked by breaking the
rule they pin: offering every topic regardless, teaching nothing, and keeping a
half-parsed book. Plus `--shot` frames of the panel before and after a word is
learned, with the new word visibly appearing in the list.*

### Things, and carrying them

An **item** is a kind generated from the art by the same pass that bakes the
props — `GG_ITEM[]` in `gg_ids.c` carries its name, weight, what using it does,
and where it can be held — so adding one is a line in `tools/make_atlas.py`, not
a code change. Six exist: bread, apples, a phial of red liquor, torches, gold
coins and bars of silver.

**Things lying on the ground live in the map**, not in the game, as a list of
(x, y, kind, count). That is what makes picking one up take it *out of the
world* rather than out of a parallel bookkeeping, and it is why the editor will
be able to place treasure. Map format version 2 carries them.

**The pack is slots**, each a kind and a count, rather than a counter per kind.
That is the shape a container model grows out of without the save file changing
meaning. What is held is a **pack index** per slot, not an item id, so holding
one of three torches is unambiguous — and emptying a slot repairs the indices,
because the last slot moves down into the gap and a held torch would otherwise
silently become a held loaf.

**Weight is in hundredths of a stone.** That unit is chosen so a coin weighs 1
and stays an integer: a purse is light, a bar of silver is four stone, and
thirty stone is all anyone can carry. Picking up is deliberately partial — a
player standing on a hoard gets an armful rather than a refusal.

The verbs are **get, drop, use, ready** — Ultima's own, less the ones WASD has
claimed, so dropping is `P` and equipping is `R`. `G` takes, `I` opens the pack,
and inside it the same four face buttons a pad uses in the world carry the
pack's verbs instead, so neither device is the poor relation. **The pack cursor
lives in the simulation**, not the UI, so a replay file will drive all of it
through the same door as a keypress.

Two things the world taught, both found by probing a generated town rather than
by reasoning:

- The first scattering rule put **one item in a 192×160 map**. Six houses, a
  one-in-three chance each. It is now one or two per house, plus windfalls under
  trees and lost change on roads.
- A tile can hold **more than one kind**, and the lookup only ever finds the
  first — so taking one and stopping stranded the rest for good. Taking now
  clears the tile.

**A torch is the only thing that can be held**, and it is the one piece of
equipment that does something: the avatar's light radius comes from it, so
holding one lights a room and stowing it puts the light out. That is the same
rule every other emitter follows — light comes from an object — applied to the
object the player carries. Empty hands still reach one tile, so a player who
drops their last torch is in the dark but never blind. There are no weapons or
armour, because there is no combat for them to affect and a control that
modifies nothing is worse than one that is absent.

*Verification:
`an_item_taken_off_the_ground_is_in_the_pack_and_gone_from_the_map` (the plan's
own, including the save round trip), `what_is_set_down_is_where_it_was_set_down`,
`taking_from_a_tile_clears_everything_on_it`,
`a_second_pile_on_one_tile_joins_the_first`,
`a_pack_will_not_hold_more_than_it_can_carry`,
`eating_costs_the_food_and_mends_the_eater`,
`what_is_held_stays_held_through_the_pack_shifting`,
`only_things_meant_to_be_held_can_be_held`,
`every_item_is_one_the_rules_can_handle`, and
`the_avatar_carries_a_light_that_falls_off` (rewritten: empty hands reach one
tile, a held torch reaches the torch's radius, and stowing it puts the light
out). Three of these were checked by breaking the rule they pin — dropping the
held-index repair, the map removal, and the weight check — and confirming each
failed. Plus `--shot` frames of an apple lying where it fell, the pack panel,
and the same night scene with a torch held and stowed.*

### Screens

Six: title, journeys, naming, options, world, paused. The pages own their own
state and menus; **the frontend owns the transitions**, which is where menu code
usually tangles.

- **Title.** Continue is first and pre-selected when there is something to
  continue, and disabled with a reason when there is not.
- **Journeys.** Every profile with its day, clock, place and turn count.
  Forgetting one takes two steps: choosing "Forget a journey" arms it and says
  so, and only then does choosing a journey delete it.
- **Naming.** Typed on a keyboard, or picked off an on-screen alphabet with a
  pad. The alphabet holds exactly the 66 characters `gg_profile_name_ok`
  accepts — which lays out as a full 11×6 rectangle, so navigating it needs no
  special cases and has no dead cells — with "Rub out" and "Begin" as two wide
  keys below. The cursor starts *off* the grid, so the keyboard flow is
  unchanged until somebody presses a direction.
- **Options.** Window scale, fullscreen and rumble cycle in place rather than
  opening sub-menus. Music and effects are shown but disabled, with "no sound
  yet" as the reason — a control that silently does nothing is worse than one
  that says why. Settings are applied on the way out and written to
  `settings.cfg`, which keeps keys it does not recognise.
- **Paused.** Resume, save, options, leave for the title, quit. The last two
  save first.

Options is the only page with two ways in, and it remembers which: backing out
of it from a paused game returns to the game. It used to drop to the title,
abandoning unsaved turns.

Everything is reachable and leavable with directions and one button, so a pad
alone can start a new journey, name it, play it, pause it and quit — no keyboard
at any step.

*Verification: `every_screen_is_reachable_by_directions_alone`,
`every_screen_can_be_left`, `the_options_page_returns_where_it_came_from`,
`a_journey_can_be_named_with_directions_alone`,
`the_title_screen_offers_continue_only_when_there_is_one`,
`naming_a_journey_refuses_a_bad_or_taken_name`,
`forgetting_a_journey_takes_two_steps`, `the_options_page_cycles_its_values`,
`a_value_can_be_nudged_both_ways_without_leaving_the_page`,
`the_menu_cursor_skips_disabled_rows_and_wraps`,
`a_menu_with_nothing_choosable_chooses_nothing`, `settings_round_trip_and_clamp`;
plus `--screen NAME --shot` in CI, which opens and photographs every one of them
at both text sizes — the unit tests exercise the logic without a renderer, so a
page that crashes or draws nothing would otherwise pass.*

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

Object picks are measured too, and the bar was raised after it let something
through. The baker used to require only that a prop's anchor cell was "not
entirely transparent" — and `TORCH_WALL` passed that with **nine texels**, the
tip of a flame whose shaft and bracket sit on the row below. It baked, shipped
and was invisible. The check is now a coverage one: the anchor cell must be at
least 5% drawn. Every real prop clears 10% (cattails are the sparsest), so the
threshold accuses nothing genuine while a speck cannot pass.

The two generated tables are also checked against each other at run time —
`a_props_atlas_rect_matches_the_size_it_declares` — because a prop's geometry
lands in `gg_ids.c` and its atlas rectangle in `gg_atlas.h`, and a stale pair on
disk would draw a sprite at the wrong size with nothing complaining.

Attribution is generated, not transcribed: `assets/CREDITS.md` reproduces the
`Credits.txt` of every source directory the bake drew from, so it cannot drift
from the art it describes. That is a licence condition of OGA-BY, not
documentation.

**And the whole of it is checked, rather than claimed.** The outputs are
committed so a clone builds without the 766 MB art submodule, which means
"these files are what the baker produces from the pinned art" is a promise
nobody can see. `tools/check_atlas.py` re-bakes and diffs it: seven pictures
compared **by pixel** — a PNG carries its encoder's choices, and a different
Pillow writes different bytes for the same image — and four generated files
compared byte for byte, including the credits. `.github/workflows/atlas.yml`
runs it weekly, on demand, and on any change to the baker, the font it
rasterises or the submodule pins; not on every push, because 766 MB is not a
per-push cost. On failure the job uploads what it baked, so the difference can
be looked at rather than inferred from a pixel count.

*Verification: the check reports all eleven files reproducible from the pinned
art. It was checked by breaking what it pins: one pixel of `atlas_items.png`
changed (`1 of 32768 pixels differ`) and one string in `gg_ids.c`, both caught,
exit code 1.*

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

### The content files, and line endings

Every content file here is the same line-based shape, and every one of them is
a file a player may open in an editor. **A line ending is `\n` or `\r\n`**, and
the parsers now strip the carriage return before looking at a line.

Left on, it does not merely add a stray character: a *blank* line becomes a line
holding `"\r"`, which is not blank, and is read as a keyword nobody has ever
heard of — so the whole file is refused, on the first blank line, before it has
read anything. The shipped bestiary, dialogue, quests and spells all did exactly
that on Windows and nowhere else, because a checkout there converts them and a
checkout here does not; the tests only started loading the shipped content
recently, so that is when it surfaced. It is a rule about the formats rather
than about CI: the same thing happens to a player who edits `dialogue.txt` in
Notepad.

*Verification: `a_content_file_written_on_windows_still_loads` writes all four
formats twice, once with each kind of line ending — blank lines included,
because those are the ones that matter — and requires the same result from
both. Checked by removing the fix from one parser and watching it fail; and the
whole shipped content was converted to CRLF and loaded, which is 8 people, 4
creatures, 4 quests and a world of 17 actors.*

### Getting at the game

Three things a player might need in order to play at all, each set from the
options page and each kept in `settings.txt`.

**Every key is rebindable, walking included.** The input layer used to hold a
switch of scancodes; it now holds a table copied from the settings — one key and
one alternate per action, so the arrows *and* WASD both survive, and the eight
directions are rebindable like every other verb because they are actions like
every other verb. Bindings are stored as SDL **scancodes** — positions on the
keyboard rather than the letters printed on them — so a binding made on one
layout is the same physical key on another, and written to the file by name
(`key.TALK = T`) because a settings file is a thing a player may open. The keys
page shows six at a time over four pages, offers "put them all back", and takes
the next key pressed; a key that already had a job loses it rather than doing
both.

**Text at twice the size**, everywhere: the band, the menus, the screens and
every panel over the world. Whole-number scaling only — the glyphs were
rasterised without antialiasing on purpose, and half a texel is a blurred one.
This was not a matter of multiplying a number: **every layout that was written
around the baked font size had to be written around the font instead**, and the
screenshots at twice the size are what found each one. The status band drops to
three rows and gives up log lines rather than the health gauge; a menu row puts
its detail beside its label instead of under it; the naming screen gives up the
wordmark to keep its alphabet; the pack and the spell book scroll to their
cursor instead of drawing through the bottom of the panel; speech wraps and the
journal clips with an ellipsis; and the long key hints are replaced by short
ones when they no longer fit.

**A debug overview that does not depend on hue.** The ordinary palette is green
against brown against blue, which is the one distinction the commonest colour
blindness cannot make. The plain palette separates by **lightness** on one grey
ramp — roads lightest, grass, scrub, canopy, masonry darkest — with a single
blue for water, and marks the Avatar with a white cross in a black box rather
than with the one red dot among yellow ones. Off by default: the ordinary
palette reads better for everyone it works for.

*Verification: `a_key_can_be_moved_and_the_world_hears_the_new_one` rebinds from
the page a player would use, then feeds real SDL key events through the input
layer and requires the new key to work and the old one not to — plus that one
key cannot hold two jobs, that Escape leaves a binding alone, and that "put them
all back" does. `what_makes_the_game_reachable_survives_being_put_down` sets the
text size and the palette from the options page by row *name*, writes them out,
reads them back, and checks every one of the forty bindings survived the trip.*

*And every screen and panel photographed at both sizes — thirteen of them at the
baked size and eleven at twice it, in CI, because "larger text" that runs off
the bottom of its own panel is a setting that makes the game less playable.*

### The controller

**Every verb is reachable with a pad.** A: talk, and ask a word in a
conversation. B: wait, and close whatever is open. X: look. Y: open. LB: cast.
RB: strike. Back: the pack. Start: pause, and "that will do" on a menu. The
left stick or the d-pad walks, and holding a direction repeats at the same rate
the step animation runs at.

**The pack gives three buttons its own meanings while it is open** — A uses, Y
readies, X sets down, B closes — because the four face buttons are spoken for
in the world and a pad player still has to be able to eat, equip and drop. That
is the same trick a console game plays, and it is in the simulation rather than
in the input layer, so the keyboard's U, R and P go on working unchanged.

**The triggers carry the two that were left over.** Left picks a thing up off
the ground; right opens the journal. Neither had any button at all, which meant
a pad could not pick up the caravan's silver and therefore could not finish the
story. A trigger is an axis rather than a button, so it is turned into an edge
here, with hysteresis: it fires past two thirds and cannot fire again until it
has fallen back to a third, or a finger resting on the edge fires it on every
jitter.

*Verification: `every_verb_can_be_reached_with_a_pad` feeds real SDL gamepad
events through the input layer and requires each verb to come out, checks that
a held trigger does not repeat and that a released one fires again, that a
stick is not mistaken for a button, and drives the pack's three aliases through
`gg_game_act`.*

*And the whole loop, on the real binary with **no keyboard at all**: `--pad-loop`
attaches a virtual gamepad through SDL and plays the game with it — the title,
a new journey, a name typed on the on-screen alphabet, a walk about the world,
every verb the pad carries, then pause, save and leave for the title. The
presses are virtual; everything after them is the ordinary path, from
`SDL_EVENT_GAMEPAD_BUTTON_DOWN` through `gg_input_event` to the screens. It
checks the journey it saved is really on disk, and it is a CI smoke test. Two
things it found: a d-pad on a virtual joystick is a hat rather than buttons, and
a button set and cleared within one frame is a button SDL never had time to
see.*

### Packaging

**A package is a folder somebody unpacks and runs.** The executables at the top
and `assets/` beside them, which is one of the two places
`src/platform/gg_paths.c` looks — so a package works from wherever it is put,
with no install step and nothing set in the environment. On macOS the same
layout lives inside a bundle: SDL hands a bundled app its *resourcePath* as the
base path, so the art goes in `Contents/Resources/assets/` and the probe finds
it unchanged.

The install rules before this put the game in `bin/` and the art in
`share/gigantima/assets/`. That is the right shape for a distribution package
and the wrong one for this game, which looks beside its own executable: an
installed copy found no art at all and started only because nobody had ever run
one.

`cpack` produces a tarball on Linux, a zip and — where NSIS is installed — an
installer on Windows, and a disk image of two `.app` bundles on macOS. Names
carry the version and the platform, because four artifacts called
`gigantima-0.1.0` in a downloads folder are four files nobody can tell apart.
Binaries are stripped: the debug info was two thirds of the download, and a bug
report here is a seed, a turn count and a replay file rather than a stack trace.

**What is finished and what is not.** The Linux tarball is done and checked. The
Windows installer and the macOS bundles are configured, and their jobs written,
and **neither has ever been built** — there is no runner for either here. Until
`.github/workflows/package.yml` has run, that half of the item is a plan.

*Verification: the Linux package built with `cpack`, unpacked into a directory
with no source tree in it, and played from `/` with `GIGANTIMA_ASSETS` unset —
40 turns of a generated world, and the whole storyline through `--screen
ending`, and the editor opening a shipped map. 5.1 MB packed, 9.1 MB unpacked,
and four shared libraries: libc, libm, the loader and the vDSO. The same
unpack-and-play check is what the packaging job runs on each platform, which is
what makes "runs on a clean machine" a thing CI says rather than a hope.*

### Replay

A session is fully described by what it started from and which actions it was
given: the simulation is integer-only and seeded, and the world advances only
inside `gg_game_act`. So it can be written down and played back.

`--record FILE` writes every action as it is given to the world; `--replay FILE`
builds the same starting world, applies them, and compares the state hash with
the one the recording ended on. **The file is text**, in the same
`keyword rest-of-line` shape as every other content file here — so a bug report
can be a file that somebody reads, diffs, and trims by hand to the shortest
sequence that still breaks.

    # gigantima replay
    map vale.ggmap
    profile Avatar
    act N
    act TALK
    travel stones.ggmap 24 44
    hash 7EA93FDACE5B4D09

**A crossing is the one thing that is not an action.** The simulation names the
map it wants and the frontend says where that map lives, so the recording notes
the crossing that was made and the driver performs it.

**The frontend has one door into the world.** Every action it gives the game
goes through a single `act()` wrapper that records as it applies, so a recording
is complete by construction rather than by somebody remembering to add a line
beside each call. That rule is why the scripted player used for the staged
screens walks its pack cursor with direction keys and asks a topic with the talk
key instead of setting `ask_cursor` and calling `gg_conversation_ask`: those
would move the world without an action, and a session that did it could not be
replayed. The whole storyline is recordable because of it.

**`gg_state_hash` covers the simulation and not the presentation.** The clock,
the RNG, the story, every actor, everything carried, everything on the floor and
every map held in mind — but not `step`, `anim`, `idle` or `from_x`/`from_y`,
which are how far through a slide the *drawing* is. Those are advanced by
`gg_game_animate` on a frame rather than by `gg_game_act` on a turn, and a hash
that noticed them reported a divergence on the very first replay run. The
turn-based simulation and the 60 Hz presentation are separate here as
everywhere else.

*Verification: `a_recorded_session_replays_to_the_same_world` — 400 actions
drawn from a seeded script rather than written by hand, recorded, read back
(every action compared with what was played, because the names are the file
format), and replayed to the same hash; plus that one more turn hashes
differently, because a hash that never changes would pass everything.
`the_state_hash_notices_every_part_of_the_world` pokes twenty-one parts of the
world one at a time and requires the hash to move for each, then puts them all
back and requires it to return — and requires that animating the world does not
move it at all.*

*And on the real game: the whole storyline recorded through `--screen ending`
(528 actions, two crossings, 273 turns) and replayed to an identical hash, as a
CI smoke test. Checked by breaking what it pins — one action removed from the
recording, and a recording's seed changed — both of which come back as
`DIVERGED`, and an action name that does not exist, which is refused with the
file and line.*

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
| The story beyond the first | one storyline, playable start to finish, and three small quests beside it. What is missing is a second one |
| Levelling | experience is counted and saved and spends on nothing; the Avatar ends the story at level one |
| Combat depth | blows, initiative, reach, loot and fleeing exist; there are no skills, no criticals and three kinds of foe |
| Magic beyond three effects | light, heal and harm. No summoning, no travel, no enchantment, no mana - reagents are the whole cost |
| Arms | a hammer, a throwing stone and a shield. No swords: this art set has none that stand on their own |
| Trade | nothing. Items carry a value in copper, and no one buys or sells |
| Trade in conversation | a topic can give a thing, take a thing and raise a flag, which is enough for a story. Nobody haggles, and no price is ever paid |
| Composed audio | the sounds are synthesised tones and noise, and the tunes a drone and an arpeggio. The mechanism is finished; the music is placeholder |
| Editor conveniences | no undo, no fill, no copy, no file dialog - it saves beside the profiles under one name |
| Party roles | up to four follow, and have health and a level, but nothing distinguishes one companion from another - no classes, no skills, no orders to give |
| Screens not yet needed | no character sheet or map page — there is nothing yet for them to show |

---

## Known approximations

Deliberate, documented, with the cost to close:

- **Roads stop at water** rather than bridging it. Paving the water was what
  put a brown causeway across the middle of the lake; a bridge prop is the
  proper fix.
- **The map's `region` byte is written and never read** beyond the region
  bounding boxes. It is there for the editor.
