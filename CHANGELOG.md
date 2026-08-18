# Changelog

Notable changes, newest first. Versions follow [semantic
versioning](https://semver.org); anything below 1.0 may still move under you,
and the save and map formats are versioned but not yet frozen.

## Unreleased

Work since the beta, on the list in `docs/COMPLETION_PLAN.md` under *Phase 5 —
More of it*: ten items of sixteen. The gaps named under 0.1.0-beta.1 below are
what that release had; this is what has closed since.

### More of it

- **Five maps, not two.** The fells the north road runs up into, **the deep**
  under them — dark enough that a torch is the difference between seeing and
  not — and **Wyndle**, a second town down the east road with four people of its
  own. All five ship as text you can read and edit.
- **A second storyline.** *Under the Hills* — Wyndle's mill has no corn because
  nothing comes down out of the fells. Playable by somebody who never went to
  the vale, and it notices the first story and is noticed by it.
- **Nine kinds of creature**, each a different question rather than a different
  number: one that will not run, one that is quick and thin, one in armour, one
  that throws from five tiles, one that takes your gold and leaves.
- **Levelling.** Killing things and working the story out both teach the party.
  Everybody walking with you rises, and the higher circles of the spell book
  open — a level check that nothing could pass before.
- **Trade.** Iolo keeps the vale's stall and Sable keeps Wyndle's; both buy and
  sell, at prices out of the item table rather than written down twice.
- **An arming sword**, and a weapon that turns blows as well as dealing them —
  which is the whole difference between a blade and a hammer.
- **A fight with more than one answer.** A telling blow lands whatever the
  armour; striking somebody who is not looking at you is easier and hurts more;
  and a creature turns on whoever is hurting it, so bringing company is the
  skill rather than a stat.
- **Magic beyond light, heal and harm**: a word that puts somebody to sleep, a
  ward that turns blows for a while, and one that sets you down on the far side
  of whatever is in the way. Nystul teaches the three new runes.
- **Companions who are somebody.** Who a person is in a fight is a line in the
  book beside what they say, so the vale's two are different answers rather than
  different numbers — one stands in front of things, the other throws from three
  paces and dies if anything reaches her. And they take orders: follow, hold
  this ground, or keep out of it, asked for in the conversation like any other
  word.
- **A world that is not the same twice.** What is *in* a map is rolled from the
  seed, so every journey through the vale has its own trouble in its own
  places — and `--seed N` still reproduces one exactly.

### Still open

A character sheet, music that is not placeholder, an editor with undo and fill,
bridges, and the region byte.

### Note

The save format has moved (version 13) and there is no migration, so a journey
saved by 0.1.0-beta.1 will not resume on a build from `main`.

## 0.1.0-beta.1

The first public build. Everything on the completion plan is done — 42 items of
42 — which means the parts that exist are finished rather than sketched, not
that there is a great deal of game yet. See *Known gaps* below, and
`docs/PROJECT_STATUS.md` for what each part does and how it was verified.

### The game

- **A story you can finish.** A caravan out of the north never arrived. The vale
  tells you why one person at a time, arms whoever asks, and sends you up a road
  with brigands on it to a ring of standing stones. Bring the silver home and
  the game says so and ends.
- **A seamless single-scale world.** Towns, houses and wilderness on one grid,
  no load screens, no scale change. Roofs lift when you step indoors.
- **Turn-based simulation, 60 Hz presentation.** The world moves only when you
  act; the camera, the walk cycle and the day/night fade do not wait for it.
- **Keyword conversation**, Ultima VI's: you know some words, you say one to
  somebody, and what they say hands you words you did not have. That single rule
  is the whole of the gating, including across speakers.
- **Party, combat, magic, inventory.** Up to four walk behind you in single
  file; blows, initiative, reach, fleeing and loot; spells you can cast when you
  know the runes they are made of; a weight limit that bites.
- **Two maps that remember.** A map you leave is kept as you left it — what you
  took off its floor stays taken, and who fell on it stays fallen.
- **Sound**: footfalls, blows, and a tune that follows the region and the hour.

### Playing it

- Packages for **Linux (tarball), Windows (zip and installer) and macOS (disk
  image of two app bundles)**, each holding the game, the editor, the art and
  the licence, and each working from wherever it is unpacked.
- **Everything works from a gamepad alone**, naming a new journey included.
- **Every key can be moved**, walking included, from a page in the game.
- **Text at twice the size**, as a different layout rather than a scaled one.
- **A debug overview that does not depend on hue**, for red-green colour
  blindness.
- Named profiles that save on the way out and resume by default.

### Making more of it

- **All content is text or data**: `dialogue.txt`, `bestiary.txt`, `quests.txt`,
  `spells.txt`, and maps written by the editor that ships beside the game.
  Adding a person, a creature, a quest or a level is not a code change.
- **`gigantima_editor`**: paint terrain and props, leave things lying about,
  place people and draw their days, drag out regions, set the start, link maps
  with ways out — and check a map for everything wrong with it that would matter
  to the game.

### Looking into it

- **`--record` and `--replay`.** A session is written down as a text file of the
  actions that made it, and playing it back has to end in the same world to the
  bit. A bug report can be a file somebody else can run.
- **`--seed N` reproduces a world exactly.** The simulation is integer-only and
  seeded; there is no floating point and no wall-clock time in it.

### Known gaps

Named rather than hidden — the full list is at the bottom of
`docs/PROJECT_STATUS.md`.

- One storyline and three small quests. What is missing is *more* of it.
- Three kinds of creature, and a fourth that ends the story.
- Two maps.
- No levelling: experience is counted and spends on nothing.
- No trade: things carry a value and nobody buys or sells.
- The sounds are synthesised tones and noise, and the music a drone and an
  arpeggio. The mechanism is finished; the music is placeholder.
- The editor has no undo, no fill and no file dialog.
- Windows builds are unsigned and macOS bundles unnotarised, so both will warn
  on first run — see [PLAYING.md](PLAYING.md#getting-it).
