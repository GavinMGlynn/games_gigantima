# Playing gigantima

Everything you need to run it, walk it, and author more of it. If you only want
one paragraph: download the package for your machine from
[Releases](https://github.com/GavinMGlynn/games_gigantima/releases), unpack it
anywhere, run `gigantima`, and choose **New journey**.

- [Getting it](#getting-it)
- [The first ten minutes](#the-first-ten-minutes)
- [Controls](#controls)
- [Options](#options)
- [The maps](#the-maps)
- [Making a map of your own](#making-a-map-of-your-own)
- [Where your journeys are kept](#where-your-journeys-are-kept)
- [The command line](#the-command-line)
- [Reporting a bug](#reporting-a-bug)

---

## Getting it

A package is a folder you unpack and run. The executables sit at the top and
`assets/` sits beside them, which is where the game looks — so it works from
wherever you put it, with no install step and nothing to set up.

### Linux

```sh
tar -xzf gigantima-*-linux-x86_64.tar.gz
cd gigantima-*-linux-x86_64
./gigantima
```

x86_64, and it needs almost nothing: SDL is linked in statically, so the only
shared libraries are libc, libm and the loader. On a headless box or over SSH
there is no window to open; see `--turns` below.

### Windows

Either unpack the `.zip` and run `gigantima.exe`, or run the `.exe` installer,
which puts it in `Program Files` with a Start-menu entry and an uninstaller.

The build is **not code-signed**, so SmartScreen will show "Windows protected
your PC" the first time. *More info* → *Run anyway*. If you would rather not,
use the zip: nothing in it needs installing.

### macOS

Open the `.dmg` and drag `gigantima.app` (and `gigantima_editor.app`) wherever
you keep applications.

The bundles are **not signed or notarised**, so Gatekeeper will refuse them on
the first run — "cannot be opened because the developer cannot be verified".
Either right-click the app and choose **Open**, which offers to run it anyway,
or clear the quarantine flag yourself:

```sh
xattr -dr com.apple.quarantine /path/to/gigantima.app
```

arm64 only for now; an Intel Mac needs a build from source.

### From source

See [README.md](README.md#building). It needs a C23 compiler, CMake 3.28, Ninja
and two SDL submodules; the art is committed, so you do **not** need the 766 MB
art submodule to build and play.

---

## The first ten minutes

The game opens on a title screen. **New journey** asks for a name — you can type
it or pick the letters off the on-screen alphabet with a pad — and drops you in
the Vale of Gigantima, on the road below the market.

There is a story, and it starts by talking to people:

1. Walk into **Iolo** at his market stall. Walking into somebody is how you talk
   to them, as Ultima VI did.
2. Ask about his **job**. Everybody knows the words `name` and `job` to begin
   with; every other word has to be learned from somebody.
3. He will tell you about the **caravan**, and give you his father's hammer.
   That is the story: a caravan out of the north that never arrived.
4. Ask the rest of the vale about what he told you. **Shamino** watched the
   road, **Nell** has seen a light among the standing stones, **Nystul** knows
   what a light with no fire is not, **Gwenno**'s husband walked north with the
   caravan, **Katrina** will give you something to drink when you are hurt, and
   **Dupre** and **Gwenno** will both come with you if you ask.
5. Read the **journal** (`J`) whenever you are unsure what you were doing.

Then go north. The road out of the vale is at the top of the map and it is not
safe — what you take off the brigands on it is what makes the man at the end of
it beatable. Bring what he took back to Iolo and the story ends.

**There are shops.** Iolo keeps the vale's stall and Sable keeps Wyndle's: ask
a merchant about his JOB and he will name what he has. They buy as well as sell,
at half what they charge.

**Take somebody with you.** Rugar beats a party that walked up alone every time
out of twenty, and loses to one that came prepared fifteen times in twenty. A
creature turns on whoever is hurting it, so while a companion holds its
attention its back is to you — and a back that is turned is the difference.

**Magic is a language, not a list.** Ask **Nystul** about his JOB and then about
the RUNES, and keep asking about the last word he gave you: ten runes, one at a
time, and twelve spells made out of them. `C` opens the book. Nothing is granted
— a spell is castable exactly when you have collected the words it is made of,
can reach its circle, and are carrying its herbs.

---

## Controls

### Keyboard

| | |
| --- | --- |
| Arrows / WASD / keypad | walk — the keypad also gives the four diagonals |
| `T` | talk to whoever you face; in a conversation, ask the word under the cursor |
| `L` | look at what you stand on |
| `O` | open |
| `G` | take what is underfoot |
| `F` | strike what you face, or throw a readied stone |
| `C` | the book of spells |
| `J` | the journal — what has happened so far |
| `I` | what you carry |
| `U` / `R` / `P` | use it, ready it, put it down |
| `Space` / `.` / keypad `5` | wait a turn |
| `F5` | save |
| `F1` | the debug window |
| `F11` / `Alt+Enter` | fullscreen |
| `Esc` | back out of whatever is open, then pause |

**Every one of these can be moved** — Options → Keys. Walking included.

### Gamepad

A pad alone is enough for all of it, naming a new journey included. Hot-plugged;
the stick and the d-pad both walk.

| | |
| --- | --- |
| **A** | talk, and ask |
| **B** | wait, and close whatever is open |
| **X** | look |
| **Y** | open |
| **LB** | the book of spells |
| **RB** | strike |
| **Left trigger** | take what is underfoot |
| **Right trigger** | the journal |
| **Back** | what you carry |
| **Start** | pause, and "that will do" on a menu |

Inside the pack those same four face buttons carry the pack's verbs instead:
**A** uses, **Y** readies, **X** sets down, **B** closes.

### In the world

Walking into things is most of the game. Walk into a townsperson and you talk to
them; walk into a brigand and you strike them; walk into a companion and you
swap places.

---

## Options

Reachable from the title screen or from the pause menu.

| | |
| --- | --- |
| Window size | 1× to 4× |
| Fullscreen | |
| Gamepad rumble | |
| Music, Effects | 0 to 10 |
| **Text size** | normal or large — the whole interface is drawn at twice the size, not scaled |
| **Map colours** | usual, or told apart by shape and shade for the debug overview |
| **Keys** | what every key does, and changing one |

On the **Keys** page: choose a row, press the key you want, and that is the key.
Escape leaves it alone. A key that already had a job loses that job rather than
doing both. "Put them all back" restores what a fresh game has.

---

## The maps

The world is one seamless scale — towns and wilderness are the same grid, with
no load screen and no scale change. What separates one map from another is a
**way out**: a tile that says which map it leads to and where in it you arrive.

Five ship with the game, in `assets/maps/`, all of them as text you can read:

| | |
| --- | --- |
| `vale.map.txt` | The Vale of Gigantima — Britain, its houses, the eight townsfolk, and the roads north and east. Where a new journey begins. |
| `stones.map.txt` | The Standing Stones — the ring at the end of the north road, and what waits in it. |
| `fells.map.txt` | The Fells — rock and wind, a valley the road winds up, and a hole in the ground at the head of it. |
| `deep.map.txt` | The Deep — rooms and corridors under the fells. **It is dark down there**; take two torches, not one. |
| `wyndle.map.txt` | Wyndle — a second town at the east end of the road, with four people who know the country rather than the vale. |

A map you leave is kept as you left it: what you took off its floor stays taken,
who fell on it stays fallen, and who is still standing is where you last saw
them. Up to twelve are held at once, and they are carried in your save.

To start a journey in a particular map:

```sh
./gigantima --map assets/maps/vale.map.txt --new --play
```

A map is looked for in `assets/maps/` first and then beside your saved
journeys — so a map you author yourself is playable by name, with nothing to
install.

---

## Making a map of your own

`gigantima_editor` ships beside the game. Everything in a map is data, so adding
content is never a code change.

```sh
./gigantima_editor                       # a blank map
./gigantima_editor --open assets/maps/vale.map.txt
./gigantima_editor --size 96 96          # a new map of that size
```

| | |
| --- | --- |
| `1`–`8` | the tools: ground, things, litter, people, their day, regions, start, ways out |
| Left button | put down what the tool places |
| Right button | rub out what the tool places — the same tool is the brush and the rubber |
| `[` `]` | change what the brush is |
| Arrows | scroll · `+` `-` zoom · `G` grid |
| `C` | check the map, and list everything wrong with it that would matter to the game |
| `S` / `O` / `N` | save · open · new |
| `Esc` | leave |

A map with no start tile, or a way out that leads nowhere, is refused with a
reason — the editor is where a map should be found to be broken, not the game.

**A map can also be written as text.** `assets/maps/*.map.txt` are the shipped
maps in a form you can open in any editor: a picture of the ground, one
character to a tile, and a list of what stands on it. The game reads either
form, so `--map mymap.map.txt` works, and the editor writes text when the name
ends in `.map.txt`. To convert one:

```sh
./gigantima_editor --open assets/maps/vale.map.txt --export vale.map.txt
```

Saved maps go beside your journeys (see below) as `authored.ggmap` unless you
opened a file, in which case `S` writes back to that file. To play one:

```sh
./gigantima --map authored.ggmap --new --play
```

**The rest of the content is text**, in `assets/`, and you can edit any of it
with a text editor:

| | |
| --- | --- |
| `dialogue.txt` | who lives in the world, what they say, what they teach, what they hand over |
| `bestiary.txt` | what lives in the hills: stats, behaviour, loot, and where it is found |
| `quests.txt` | the story, as a list of stages and the conditions that enter them |
| `spells.txt` | the runes, and the phrases they make |

Each file documents its own format at the top. The game says which file and
which line it did not understand, and refuses to load a broken one rather than
guessing.

---

## Where your journeys are kept

Each named profile keeps a world of its own, and the game saves on the way out.

| | |
| --- | --- |
| Linux | `~/.local/share/gigantima/gigantima/` |
| Windows | `%APPDATA%\gigantima\gigantima\` |
| macOS | `~/Library/Application Support/gigantima/gigantima/` |

Inside: `profiles/<name>/world.ggsave`, `settings.txt` (plain text, safe to edit
by hand), and any maps the editor saved.

---

## The command line

None of it is needed to play; all of it is useful for looking into something.

```
gigantima [--profile NAME] [--seed N] [--play] [--new] [--map FILE.ggmap]
          [--at X,Y] [--time HH:MM] [--scale N] [--fullscreen] [--no-rumble]
          [--debug] [--turns N] [--screen NAME] [--shot FILE.bmp] [--shot-at TURN]
          [--record FILE.ggreplay] [--replay FILE.ggreplay] [--pad-loop]
```

| | |
| --- | --- |
| `--profile NAME` | whose journey to act on |
| `--play` | straight into the world, without stopping at the title |
| `--new` | start that profile over rather than resuming it |
| `--seed N` | the seed for a generated world — the same seed is the same world |
| `--map FILE.ggmap` | begin in that map instead of a generated world |
| `--at X,Y`, `--time HH:MM` | put the Avatar there, set the clock to then |
| `--scale N`, `--fullscreen`, `--no-rumble` | window size, fullscreen, rumble off |
| `--debug` | the debug window: the whole map, the clock, and everyone in it |
| `--turns N` | play N turns headless and quit, saving on the way out |
| `--shot FILE.bmp`, `--shot-at TURN` | write a frame and quit |
| `--screen NAME` | open on a named page, for photographing one |
| `--record FILE` | write down every action of this session |
| `--replay FILE` | play a recording back, and say whether it ended in the same world |
| `--pad-loop` | play a whole journey with a virtual gamepad and no keyboard |

---

## Reporting a bug

Open an issue: <https://github.com/GavinMGlynn/games_gigantima/issues>.

**A bug report here can be a file.** The simulation is integer-only and seeded,
and the world only moves when you act, so a session is completely described by
what it started from and what you did:

```sh
./gigantima --profile Bug --new --play --record bug.ggreplay
# ... play until it goes wrong, then quit
./gigantima --replay bug.ggreplay
```

Attach `bug.ggreplay` to the issue. It is a text file — you can read it, and
trim it by hand to the shortest sequence that still shows the problem. Playing
it back on another machine builds the same world and runs the same actions into
it, and says whether it ended in the same state to the bit.

Failing that, the seed and the turn count are usually enough: `--seed N` builds
the same world every time.
