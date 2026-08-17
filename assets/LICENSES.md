# Asset licences

This repository's code is GPL-3.0 (see `../LICENSE`). The art and the font are
**not** ours and keep their own terms, which are recorded here.

Code and art are separate works. Licensing the code GPL-3.0 does not, and
cannot, relicense somebody else's art — so this file, and the generated
`CREDITS.md` beside it, are what make shipping the art lawful.

## Art — LPC Revised

| | |
| --- | --- |
| Source | <https://github.com/ElizaWy/LPC> |
| Vendored at | `ext/lpc-revised`, commit `f07f7f5` |
| Licence | **OGA-BY 3.0** or **CC-BY 3.0**, per asset |
| Attribution | **Required.** See `CREDITS.md` |

Every tile, prop and character sprite in `atlas_tiles.png`, `atlas_props.png`
and `atlas_actors.png` is derived from this set by `../tools/make_atlas.py`.

`CREDITS.md` is **generated**, not written: the baker collects the
`Credits.txt` file that ships in each source directory it drew from, and
reproduces it in full. Regenerate it in the same commit as any change to the
manifest in `make_atlas.py`, or the attribution stops describing the art.

OGA-BY 3.0 is CC-BY 3.0 with the anti-DRM clause removed, which is what makes
these assets usable on storefronts that apply technical protection measures.

## Font — VT323

| | |
| --- | --- |
| Designer | Peter Hull |
| Source | <https://github.com/google/fonts/tree/main/ofl/vt323> |
| File | `fonts/VT323-Regular.ttf` |
| Licence | **SIL Open Font License 1.1** — full text in `fonts/OFL.txt` |

`atlas_font.png` is a bitmap rasterisation of this font, produced by
`make_atlas.py`. The OFL permits embedding and redistribution; it forbids
selling the font on its own and requires derivatives that use the Reserved
Font Name to be renamed — neither applies here, since the font ships unmodified
alongside a rasterisation of it.

The TTF is vendored rather than fetched so the bake is reproducible, and is
excluded from `cmake --install` because the game does not read it at runtime —
the baked `atlas_font.png` is what ships.

## Generated files

These are committed so that a clone builds and plays without the 766 MB art
submodule. All are outputs of `tools/make_atlas.py` and none should be edited
by hand:

- `atlas_tiles.png`, `atlas_props.png`, `atlas_actors.png`, `atlas_font.png`
- `CREDITS.md`
- `../src/core/gg_ids.h`, `../src/core/gg_ids.c`
- `../src/gfx/gg_atlas.h`
