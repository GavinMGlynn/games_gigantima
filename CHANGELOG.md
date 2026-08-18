# Changelog

Notable changes, newest first. Versions follow [semantic
versioning](https://semver.org); anything below 1.0 may still move under you,
and the save and map formats are versioned but not yet frozen.

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
