#!/usr/bin/env python3
"""Bake gigantima's art from the LPC sources in `ext/lpc-revised/`.

The LPC Revised set is thousands of files across a directory tree, laid out for
a human opening them in an editor. The game wants the opposite: a handful of
textures, and a table saying where every sprite sits inside them. This script is
the seam between the two, and it is the *only* place that knows an LPC path.

It writes four textures and one header:

    assets/atlas_tiles.png    32x32 terrain and structure tiles
    assets/atlas_props.png    variable-size scenery, shelf-packed
    assets/atlas_actors.png   64x64 character frames, 8 per direction
    assets/atlas_font.png     the UI font, rasterised from assets/fonts/
    src/gg_atlas.h            GENERATED enums and rect tables for all four

Nothing here runs as part of the build. The outputs are committed, so a clone
builds and plays without the art submodule present - `ext/lpc-revised` is 766
MB and needed only to regenerate. See `ext/README.md`.

    python3 tools/make_atlas.py

Requires Pillow (`python3 -m pip install --user Pillow`). That is a deliberate
exception to the no-dependency rule the rest of the tooling follows: compositing
six characters out of five layers each is 25 megapixels of alpha blending, and
in pure Python that is minutes rather than seconds.
"""
import os
import sys

try:
    from PIL import Image, ImageFont, ImageDraw
except ImportError:
    sys.exit("make_atlas.py needs Pillow:  python3 -m pip install --user Pillow")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LPC = os.path.join(ROOT, "ext", "lpc-revised")
ASSETS = os.path.join(ROOT, "assets")

TILE = 32          # world tile, in texels. LPC Revised is a 32-pixel style.
FRAME = 64         # character frame; the sprite is taller than a tile on purpose
WALK_FRAMES = 8    # columns in an LPC Revised Walk.png
DIRS = 4           # rows: up, left, down, right - in that order


# ---------------------------------------------------------------------------
# Terrain and structure tiles
#
# Every entry is (name, sheet, column, row). The coordinates were not guessed:
# `tools/scan_sheet.py` reports, for each 32x32 cell, whether it is fully opaque
# and how well it butts against a copy of itself, and a base terrain fill has to
# be both. The picks below are the cells that came back flat and seamless.
# ---------------------------------------------------------------------------
TERRAIN = "Terrain/terrain_summer.png"
CLIFF = "Terrain/cliff_summer.png"
SOIL = "Terrain/tilled_soil.png"
GRIT = "Structure/Floor/Gritty Dirt.png"

TILES = [
    # name              sheet      col row
    ("GRASS",           TERRAIN,     4,  1),
    ("GRASS_WORN",      TERRAIN,     5,  2),
    ("DIRT",            GRIT,        1,  0),
    ("EARTH_DARK",      GRIT,        0,  1),
    ("FARMLAND",        SOIL,        1,  1),
    ("ROAD",            TERRAIN,     4,  4),
    ("SAND",            TERRAIN,     4,  5),
    ("DESERT",          TERRAIN,     3,  6),
    ("WATER",           TERRAIN,    12, 16),
    ("WATER_DEEP",      TERRAIN,     1, 24),
    ("MOUNTAIN",        CLIFF,       1,  1),
    ("CLIFF",           CLIFF,       3,  3),
]


# ---------------------------------------------------------------------------
# Props - scenery larger than a tile, drawn over the terrain and sorted by the
# row its base sits on, so the player walks behind a tree's canopy.
#
# (name, sheet, col, row, w_tiles, h_tiles, base_rows)
# `base_rows` is how many tile rows at the bottom of the sprite are the object's
# footprint on the ground; the rest overhangs upward. A tree is 3x4 with one
# base row, so it blocks one tile and its canopy covers three rows above.
# ---------------------------------------------------------------------------
TREES = "Terrain/trees_summer.png"
PLANTS = "Terrain/plants_summer.png"

PROPS = [
    ("TREE_OAK",     TREES,   4,  0, 3, 4, 1),
    ("TREE_ELM",     TREES,   7,  0, 3, 4, 1),
    ("TREE_TALL",    TREES,  10,  0, 3, 4, 1),
    ("TREE_PINE",    TREES,   4, 12, 3, 4, 1),
    ("TREE_FIR",     TREES,   7, 12, 3, 4, 1),
    ("TREE_BARE",    TREES,   0,  8, 3, 5, 1),
    ("STUMP",        TREES,   1, 14, 1, 1, 1),
    ("BUSH_ROUND",   PLANTS,  0,  0, 1, 2, 1),
    ("BUSH_LEAFY",   PLANTS,  2,  0, 1, 2, 1),
    ("BUSH_CONIFER", PLANTS,  0,  2, 1, 2, 1),
    ("REEDS",        PLANTS, 11,  0, 1, 2, 1),
    ("FERN",         PLANTS,  3,  2, 1, 2, 1),
    ("LILYPAD",      PLANTS, 12,  2, 1, 1, 1),
    ("CATTAILS",     PLANTS, 14,  2, 1, 2, 1),
]


# ---------------------------------------------------------------------------
# Characters
#
# An LPC Revised character is a stack of independent sheets that share one
# geometry, so a cast is a list of layer choices rather than a list of images.
# Order matters and is bottom-to-top: body, then head, then what it wears, then
# hair over the top of the head.
#
# Every layer is <category>/<variant>/<colour>/Walk.png, except the few that
# have no colour axis. A missing layer is skipped rather than fatal, so a cast
# entry that names a garment this body shape does not have still produces a
# usable sprite instead of failing the whole bake.
# ---------------------------------------------------------------------------
BODY_M = "Body/Body 02 - Masculine, Thin"
BODY_F = "Body/Body 01 - Feminine, Thin"
HEAD_M = "Head/Head 02 - Masculine"
HEAD_F = "Head/Head 01 - Feminine"
HEAD_E = "Head/Head 03 - Elderly"


def cast(body, head, tone, hair, hair_col, torso, torso_col,
         legs, legs_col, feet, feet_col, sex="Masculine, Thin"):
    """One character as an ordered list of (directory, colour) layer picks."""
    return [
        (f"{body}", tone),
        (f"{head}", tone),
        (f"Clothing/{sex}/Legs/{legs}", legs_col),
        (f"Clothing/{sex}/Feet/{feet}", feet_col),
        (f"Clothing/{sex}/Torso/{torso}", torso_col),
        (f"Hair/{hair}", hair_col),
    ]


ACTORS = [
    # The Avatar - deliberately plain, so the player reads as themselves.
    ("AVATAR", cast(BODY_M, HEAD_M, "Tan", "Medium 01 - Page", "Brown",
                    "Shirt 07 - Buttoned Longsleeve Shirt", "Azure",
                    "Pants 03 - Pants", "Brown", "Shoes 02 - Boots", "Brown")),
    ("GUARD", cast(BODY_M, HEAD_M, "Bronze", "Short 01 - Buzzcut", "Charcoal",
                   "Shirt 01 - Longsleeve Shirt", "Charcoal",
                   "Pants 03 - Pants", "Charcoal", "Shoes 02 - Boots", "Black")),
    ("MERCHANT", cast(BODY_M, HEAD_M, "Peach", "Medium 07 - Bob, Side Part", "Ash Brown",
                      "Shirt 09 - Polo", "Amber",
                      "Pants 04 - Cuffed Pants", "Brown", "Shoes 01 - Shoes", "Brown")),
    ("HEALER", cast(BODY_F, HEAD_F, "Ivory", "Medium 02 - Curly", "Blonde",
                    "Shirt 03 - Scoop Longsleeve Shirt", "Emerald",
                    "Pants 02 - Leggings", "Navy", "Shoes 01 - Shoes", "Leather",
                    sex="Feminine, Thin")),
    ("MAGE", cast(BODY_F, HEAD_F, "Coffee", "Medium 09 - Twists", "Black",
                  "Shirt 02 - V-neck Longsleeve Shirt", "Amethyst",
                  "Pants 01 - Hose", "Charcoal", "Shoes 01 - Shoes", "Black",
                  sex="Feminine, Thin")),
    ("ELDER", cast(BODY_M, HEAD_E, "Porcelain", "Medium 04 - Bangs & Bun", "White",
                   "Shirt 01 - Longsleeve Shirt", "Plum",
                   "Pants 03 - Pants", "Charcoal", "Shoes 01 - Shoes", "Leather")),
]


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def sheet(rel):
    """Open an LPC sheet, with an error that says what to do if it is missing."""
    path = os.path.join(LPC, rel)
    if not os.path.exists(path):
        sys.exit(f"missing art: {path}\n"
                 f"  the LPC submodule is not checked out. Run:\n"
                 f"    git submodule update --init ext/lpc-revised")
    return Image.open(path).convert("RGBA")


def find_layer(directory, colour):
    """Resolve a layer to its Walk.png, tolerating the sheets that have no
    colour axis. Returns None if the variant does not exist for this body."""
    base = os.path.join(LPC, "Characters", directory)
    for candidate in ([os.path.join(base, colour, "Walk.png")] if colour else []) + \
                     [os.path.join(base, "Walk.png")]:
        if os.path.exists(candidate):
            return candidate
    return None


class Shelf:
    """A trivial shelf packer: place left to right, drop to a new row when full.

    Good enough by a wide margin - the props are a few dozen sprites of similar
    height, which is exactly the case shelf packing handles well, and the atlas
    is built once and committed.
    """

    def __init__(self, width):
        self.width = width
        self.x = self.y = self.row_h = 0
        self.placed = []

    def add(self, name, img):
        if self.x + img.width > self.width:
            self.x = 0
            self.y += self.row_h
            self.row_h = 0
        rect = (self.x, self.y, img.width, img.height)
        self.placed.append((name, img, rect))
        self.x += img.width
        self.row_h = max(self.row_h, img.height)
        return rect

    def render(self):
        h = self.y + self.row_h
        out = Image.new("RGBA", (self.width, max(h, 1)), (0, 0, 0, 0))
        for _name, img, (x, y, _w, _h) in self.placed:
            out.paste(img, (x, y))
        return out


# ---------------------------------------------------------------------------
# Builders - each returns (image, [(NAME, x, y, w, h, extra...)])
# ---------------------------------------------------------------------------
def build_tiles():
    cols = 8
    rows = (len(TILES) + cols - 1) // cols
    out = Image.new("RGBA", (cols * TILE, rows * TILE), (0, 0, 0, 0))
    entries = []
    cache = {}
    for i, (name, rel, c, r) in enumerate(TILES):
        if rel not in cache:
            cache[rel] = sheet(rel)
        src = cache[rel]
        tile = src.crop((c * TILE, r * TILE, (c + 1) * TILE, (r + 1) * TILE))
        if not tile.getchannel("A").getextrema()[1]:
            sys.exit(f"tile {name} at {rel} ({c},{r}) is entirely transparent - "
                     f"the pick is wrong")
        x, y = (i % cols) * TILE, (i // cols) * TILE
        out.paste(tile, (x, y))
        entries.append((name, x, y, TILE, TILE))
    return out, entries


def build_props():
    packer = Shelf(512)
    entries = []
    cache = {}
    for name, rel, c, r, wt, ht, base in PROPS:
        if rel not in cache:
            cache[rel] = sheet(rel)
        src = cache[rel]
        img = src.crop((c * TILE, r * TILE, (c + wt) * TILE, (r + ht) * TILE))
        if not img.getchannel("A").getextrema()[1]:
            sys.exit(f"prop {name} at {rel} ({c},{r}) is entirely transparent - "
                     f"the pick is wrong")
        x, y, w, h = packer.add(name, img)
        entries.append((name, x, y, w, h, wt, ht, base))
    return packer.render(), entries


def build_actors():
    sheet_w = FRAME * WALK_FRAMES
    out = Image.new("RGBA", (sheet_w, FRAME * DIRS * len(ACTORS)), (0, 0, 0, 0))
    entries = []
    for i, (name, layers) in enumerate(ACTORS):
        y = i * FRAME * DIRS
        composed = Image.new("RGBA", (sheet_w, FRAME * DIRS), (0, 0, 0, 0))
        used = 0
        for layer in layers:
            if layer is None:
                continue
            directory, colour = layer
            path = find_layer(directory, colour)
            if path is None:
                print(f"  note: {name}: no Walk.png for {directory}"
                      f"{'/' + colour if colour else ''} - layer skipped")
                continue
            img = Image.open(path).convert("RGBA")
            if img.size != composed.size:
                print(f"  note: {name}: {directory} is {img.size}, "
                      f"expected {composed.size} - layer skipped")
                continue
            composed = Image.alpha_composite(composed, img)
            used += 1
        if used == 0:
            sys.exit(f"actor {name} composited to nothing - every layer missing")
        out.paste(composed, (0, y))
        entries.append((name, 0, y, FRAME, FRAME))
    return out, entries


def build_font():
    """Rasterise the UI font into a fixed-cell glyph sheet.

    Baked rather than loaded at runtime so the game needs no TTF library and no
    font installed on the player's machine. Antialiasing is off (`fontmode=1`):
    the art is pixel art, and a grey fringe on the text is the one thing that
    makes a pixel game look wrong.
    """
    size = 16
    path = os.path.join(ASSETS, "fonts", "VT323-Regular.ttf")
    if not os.path.exists(path):
        sys.exit(f"missing font: {path}")
    font = ImageFont.truetype(path, size)

    first, last = 32, 126
    n = last - first + 1
    # Measure the widest and tallest glyph so every cell is the same size; the
    # game indexes the sheet arithmetically rather than carrying per-glyph rects.
    probe = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
    cw = ch = 0
    top = 0
    for code in range(first, last + 1):
        box = probe.textbbox((0, 0), chr(code), font=font)
        cw = max(cw, box[2])
        ch = max(ch, box[3])
        top = min(top, box[1])
    cw = max(cw, 1) + 1
    ch = max(ch, 1) + 1

    cols = 16
    rows = (n + cols - 1) // cols
    out = Image.new("RGBA", (cols * cw, rows * ch), (0, 0, 0, 0))
    d = ImageDraw.Draw(out)
    d.fontmode = "1"
    for i in range(n):
        gx, gy = (i % cols) * cw, (i // cols) * ch
        d.text((gx, gy), chr(first + i), font=font, fill=(255, 255, 255, 255))

    # The advance for a proportional font varies per glyph, so record it; the
    # cell is uniform but the text layout is not monospaced.
    adv = [int(probe.textlength(chr(c), font=font)) for c in range(first, last + 1)]
    return out, (first, last, cw, ch, cols, adv)


# ---------------------------------------------------------------------------
# Header emission
# ---------------------------------------------------------------------------
def banner(filename, what):
    return f"""\
// {filename} - GENERATED by tools/make_atlas.py, do not edit.
//
// {what}
//
// Art: LPC Revised (github.com/ElizaWy/LPC), OGA-BY 3.0 / CC-BY 3.0.
// Font: VT323 by Peter Hull, SIL Open Font License 1.1.
// Per-asset attribution is in assets/CREDITS.md, which this script also writes.
"""


def emit_ids(tiles, props, actors, path):
    """The content vocabulary: what a tile, prop or actor *is*.

    Split out from the rectangles deliberately. `src/core/` reasons about
    terrain and props all the time and must never need to know where a sprite
    sits in a texture - that keeps the simulation headless and testable, and
    stops a re-bake of the art from being a change to the simulation.
    """
    o = [banner("gg_ids.h", "The content vocabulary shared by the simulation "
                            "and the renderer."),
         "#ifndef GG_IDS_H", "#define GG_IDS_H", "",
         "#include <stdint.h>", ""]

    o.append("typedef enum {")
    for name, *_ in tiles:
        o.append(f"    GG_TILE_{name},")
    o.append("    GG_TILE_COUNT")
    o.append("} gg_tile_id;")
    o.append("")

    o.append("typedef enum {")
    for name, *_ in props:
        o.append(f"    GG_PROP_{name},")
    o.append("    GG_PROP_COUNT")
    o.append("} gg_prop_id;")
    o.append("")

    o.append("typedef enum {")
    for name, *_ in actors:
        o.append(f"    GG_ACTOR_{name},")
    o.append("    GG_ACTOR_COUNT")
    o.append("} gg_actor_id;")
    o.append("")
    o.append("// Row order within an actor's block, matching LPC's Walk.png.")
    o.append("typedef enum { GG_FACE_UP, GG_FACE_LEFT, GG_FACE_DOWN, "
             "GG_FACE_RIGHT } gg_facing;")
    o.append("")
    o.append(f"#define GG_ACTOR_FRAMES  {WALK_FRAMES}")
    o.append(f"#define GG_ACTOR_DIRS    {DIRS}")
    o.append("")
    o.append("// A prop's footprint in tiles, which the simulation needs for")
    o.append("// collision. The sprite may stand taller and overhang above it.")
    o.append("typedef struct { uint8_t tiles_w, tiles_h, foot_h; } gg_prop_size;")
    o.append("extern const gg_prop_size GG_PROP_SIZE[GG_PROP_COUNT];")
    o.append("")
    o.append("#endif // GG_IDS_H")

    with open(path, "w") as f:
        f.write("\n".join(o) + "\n")


def emit_sizes(props, path):
    """The one generated .c file - a table the simulation links against."""
    o = [banner("gg_ids.c", "Prop footprints, for the simulation's collision."),
         '#include "core/gg_ids.h"', "",
         "const gg_prop_size GG_PROP_SIZE[GG_PROP_COUNT] = {"]
    for name, x, y, w, h, wt, ht, base in props:
        o.append(f"    [GG_PROP_{name}] = {{ {wt}, {ht}, {base} }},")
    o.append("};")
    with open(path, "w") as f:
        f.write("\n".join(o) + "\n")


def emit_atlas(tiles, props, actors, font_meta, path):
    first, last, cw, ch, fcols, adv = font_meta
    o = [banner("gg_atlas.h", "Where every sprite sits inside the four atlas "
                              "textures in assets/."),
         "#ifndef GG_ATLAS_H", "#define GG_ATLAS_H", "",
         "#include <stdint.h>", '#include "core/gg_ids.h"', "",
         "typedef struct { int16_t x, y, w, h; } gg_rect;", ""]

    o.append("static const gg_rect GG_TILE_RECT[GG_TILE_COUNT] = {")
    for name, x, y, w, h in tiles:
        o.append(f"    [GG_TILE_{name}] = {{ {x:4d}, {y:4d}, {w:3d}, {h:3d} }},")
    o.append("};")
    o.append("")

    o.append("static const gg_rect GG_PROP_RECT[GG_PROP_COUNT] = {")
    for name, x, y, w, h, wt, ht, base in props:
        o.append(f"    [GG_PROP_{name}] = {{ {x:4d}, {y:4d}, {w:3d}, {h:3d} }},")
    o.append("};")
    o.append("")

    o.append(f"#define GG_ACTOR_FRAME   {FRAME}")
    o.append("// Top-left of each actor's block; add facing*FRAME to y, "
             "frame*FRAME to x.")
    o.append("static const gg_rect GG_ACTOR_RECT[GG_ACTOR_COUNT] = {")
    for name, x, y, w, h in actors:
        o.append(f"    [GG_ACTOR_{name}] = {{ {x:4d}, {y:4d}, {w:3d}, {h:3d} }},")
    o.append("};")
    o.append("")

    o.append("// --- font: fixed cells, proportional advances ---")
    o.append(f"#define GG_FONT_FIRST   {first}")
    o.append(f"#define GG_FONT_LAST    {last}")
    o.append(f"#define GG_FONT_CELL_W  {cw}")
    o.append(f"#define GG_FONT_CELL_H  {ch}")
    o.append(f"#define GG_FONT_COLS    {fcols}")
    o.append("static const uint8_t GG_FONT_ADV[GG_FONT_LAST - GG_FONT_FIRST + 1] = {")
    for i in range(0, len(adv), 16):
        o.append("    " + " ".join(f"{a:2d}," for a in adv[i:i + 16]))
    o.append("};")
    o.append("")
    o.append("#endif // GG_ATLAS_H")

    with open(path, "w") as f:
        f.write("\n".join(o) + "\n")


def emit_credits(path):
    """Collect the LPC per-directory Credits.txt files we actually drew from.

    Attribution is a licence condition of OGA-BY and CC-BY, so this is not
    documentation - it is the thing that makes shipping the art lawful. Gathered
    from the source tree rather than transcribed, so it cannot drift from the
    art it describes.
    """
    used = sorted({os.path.dirname(rel) for _n, rel, *_ in TILES} |
                  {os.path.dirname(rel) for _n, rel, *_ in PROPS} |
                  {"Characters"})
    parts = [
        "# Art credits\n",
        "GENERATED by `tools/make_atlas.py`. Do not edit by hand.\n",
        "Gigantima's art is baked from the **LPC Revised** collection",
        "(<https://github.com/ElizaWy/LPC>), used under **OGA-BY 3.0** and",
        "**CC-BY 3.0**. Both require attribution, so the per-directory credit",
        "files from the source set are reproduced below in full, for every",
        "directory this game draws from.\n",
        "The UI font is **VT323** by Peter Hull, under the SIL Open Font",
        "License 1.1 - see `assets/fonts/OFL.txt`.\n",
    ]
    for d in used:
        c = os.path.join(LPC, d, "Credits.txt")
        if not os.path.exists(c):
            continue
        parts.append(f"\n## {d}\n")
        parts.append("```")
        parts.append(open(c, encoding="utf-8", errors="replace").read().rstrip())
        parts.append("```")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(parts) + "\n")


def main():
    os.makedirs(ASSETS, exist_ok=True)
    print("gigantima: baking art from ext/lpc-revised")

    tiles_img, tiles = build_tiles()
    tiles_img.save(os.path.join(ASSETS, "atlas_tiles.png"))
    print(f"  tiles   {tiles_img.size[0]:4d}x{tiles_img.size[1]:<4d} {len(tiles):3d} entries")

    props_img, props = build_props()
    props_img.save(os.path.join(ASSETS, "atlas_props.png"))
    print(f"  props   {props_img.size[0]:4d}x{props_img.size[1]:<4d} {len(props):3d} entries")

    actors_img, actors = build_actors()
    actors_img.save(os.path.join(ASSETS, "atlas_actors.png"))
    print(f"  actors  {actors_img.size[0]:4d}x{actors_img.size[1]:<4d} {len(actors):3d} entries")

    font_img, font_meta = build_font()
    font_img.save(os.path.join(ASSETS, "atlas_font.png"))
    print(f"  font    {font_img.size[0]:4d}x{font_img.size[1]:<4d} "
          f"cell {font_meta[2]}x{font_meta[3]}")

    src = os.path.join(ROOT, "src")
    emit_ids(tiles, props, actors, os.path.join(src, "core", "gg_ids.h"))
    emit_sizes(props, os.path.join(src, "core", "gg_ids.c"))
    emit_atlas(tiles, props, actors, font_meta, os.path.join(src, "gfx", "gg_atlas.h"))
    emit_credits(os.path.join(ASSETS, "CREDITS.md"))
    print("  wrote src/core/gg_ids.{h,c}, src/gfx/gg_atlas.h, assets/CREDITS.md")


if __name__ == "__main__":
    main()
