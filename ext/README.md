# Third-party dependencies

Everything here is a pinned git submodule. Nothing in this directory is our
code, nothing here is modified in place, and nothing here is redistributed by
this repository — a clone gets URLs and commit SHAs, not sources.

## What is here

| Submodule | Upstream | Pinned at | Role | Licence |
| --- | --- | --- | --- | --- |
| `sdl` | libsdl-org/SDL | `release-3.4.14` | **Linked.** Window, renderer, input, audio, filesystem, timing | Zlib |
| `sdl_image` | libsdl-org/SDL_image | `release-3.4.4` | **Linked.** Decodes the PNG atlases in `assets/` | Zlib |
| `lpc-revised` | ElizaWy/LPC | `f07f7f5` | **Art source, build-time only.** Never linked, never shipped | OGA-BY 3.0 / CC-BY 3.0 |

All four are pinned to a release tag where one exists. `lpc-revised` has no
tags, so it is pinned to the commit SHA above — the head of `main` as of
2023-12-01, which is the last time that project published art.

## What the build actually needs

**`ext/sdl` and `ext/sdl_image`, and nothing else.**

```sh
git submodule update --init --depth 1 ext/sdl ext/sdl_image
cmake --preset linux-release && cmake --build --preset linux-release
ctest --preset linux-release
```

`ext/sdl_mixer` used to be declared here against the audio work. It has been
removed, because the audio work did not need it: SDL3 loads and converts WAVs on
its own, and the sounds are generated rather than vendored, so the mixing is a
hundred lines in `src/audio/gg_audio.c` instead of a dependency on four
platforms. This project prefers no dependency to a small one, and that is what
that preference looks like when it is acted on.

**Do not init `ext/lpc-revised` casually: it is 766 MB.** It is needed only to
*regenerate* the art, and the generated output — `assets/atlas_*.png`,
`src/core/gg_ids.{h,c}`, `src/gfx/gg_atlas.h`, `assets/CREDITS.md` — is
committed. So a clone builds and plays without it.

CI does exactly this — see `CI_SUBMODULES` in `.github/workflows/ci.yml` —
which is what keeps the claim honest rather than aspirational. `CMakeLists.txt`
fails with an actionable message if `ext/sdl` is missing, and
`tools/make_atlas.py` fails with the `git submodule` command to run if
`ext/lpc-revised` is.

## The art boundary

`ext/lpc-revised` is a different kind of dependency from the other three, and
the difference is a licensing boundary rather than a convenience:

- The LPC art is **OGA-BY 3.0 / CC-BY 3.0**. Both require attribution, so
  `tools/make_atlas.py` gathers the per-directory `Credits.txt` files from
  every directory it draws from and writes `assets/CREDITS.md`. That file is
  generated from the source tree rather than transcribed, so it cannot drift
  from the art it describes.
- Attribution is a **licence condition, not documentation**. If the atlas is
  regenerated from different source directories, `assets/CREDITS.md` must be
  regenerated in the same commit.
- The art's licence is not this repository's licence. `LICENSE` is GPL-3.0 and
  covers the code; the art keeps its own terms. See `assets/LICENSES.md`.

## Why release tags rather than branch tips

A branch tip is not a version: two clones a week apart build different
software, and a bug report that names one is unreproducible against the other.
Every submodule that can be pinned to a tag is. `lpc-revised` cannot be, so its
SHA is written down here as well as in the index, because a SHA with no prose
beside it tells the next reader nothing about how it was chosen.

## Updating one

```sh
cd ext/sdl && git fetch --tags && git checkout release-3.4.15 && cd ../..
git add ext/sdl && git commit
```

Update the table above in the same commit. A pin that has moved without the
table moving is worse than no table.
