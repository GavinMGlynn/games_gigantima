#!/usr/bin/env python3
"""Bake gigantima's art from the LPC sources in `ext/lpc-revised/`.

The LPC Revised set is thousands of files across a directory tree, laid out for
a human opening them in an editor. The game wants the opposite: a handful of
textures, and a table saying where every sprite sits inside them. This script is
the seam between the two, and it is the *only* place that knows an LPC path.

It writes six textures and three generated sources:

    assets/atlas_tiles.png     32x32 terrain and structure tiles
    assets/atlas_edges.png     3x3 blob rings for shorelines
    assets/atlas_overlays.png  the rings again, for land meeting land
    assets/atlas_props.png     variable-size scenery, shelf-packed
    assets/atlas_items.png     the things that can be picked up
    assets/atlas_actors.png    64x64 character frames, 8 per direction
    assets/atlas_font.png      the UI font, rasterised from assets/fonts/

    src/core/gg_ids.h    GENERATED vocabulary: what a tile, prop or item IS
    src/core/gg_ids.c    GENERATED tables the simulation links against
    src/gfx/gg_atlas.h   GENERATED rectangles: where each sprite SITS

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
WOOD = "Structure/Floor/Wood Floor A.png"
BRICK = "Structure/Walls/Brick Wall A.png"

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
    # Interiors. Both tile cleanly left-to-right; their vertical seam is a
    # plank joint and a brick course, which is what those surfaces look like.
    ("FLOOR_WOOD",      WOOD,        1,  1),
    ("WALL_BRICK",      BRICK,       1,  1),
]


# ---------------------------------------------------------------------------
# Edge sets - the 3x3 blob rings that make a shoreline look drawn rather than
# stamped.
#
# An LPC Revised terrain block is a field of one terrain with a pool of another
# cut into it: the eight outer cells carry the boundary at each orientation and
# the centre is the pool's interior. So a block is exactly a 3x3 autotile, and
# the index order below is the order the renderer computes from a neighbour
# mask:
#
#     0 NW   1 N   2 NE
#     3 W    4 C   5 E
#     6 SW   7 S   8 SE
#
# `col`/`row` is the block's top-left cell. Verified by eye against a magnified
# render of each candidate block before being written down - the sheet also
# contains hole variants whose centre is transparent, which look identical in a
# thumbnail and are not what is wanted here.
#
# **These sheets carry no concave corners.** A water cell with land on a single
# diagonal has no piece to draw and falls back to the interior fill, leaving a
# square notch. Generated lakes are convex so it rarely shows; hand-authored
# coastlines will need the missing pieces drawn. Recorded in the plan.
# Both sets share an identical water centre - (42,133,152) in each - so a lake
# can change its bank part way round without the water itself changing colour.
EDGES = [
    # name              sheet      col row
    ("WATER_GRASS",     TERRAIN,     0, 10),
    ("WATER_SAND",      TERRAIN,     0, 20),
    # Shallow water outside, deep water inside, with a soft gradient between -
    # so the drop-off reads as depth rather than as two flat blues meeting.
    ("WATER_DEEP",      TERRAIN,     0, 23),
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
HOUSES = "Structure/Structures"
FURN = "Objects/Furniture"
SMALL = "Objects/Small Items"
WALLITEM = "Objects/Wall Items"

NO_DOOR = 255

# Must match GG_LIGHT_MAX_RADIUS: the renderer scans a box this big around
# each cell looking for emitters, so anything brighter would be clipped.
GG_LIGHT_MAX_RADIUS = 6


def prop(name, sheet, col, row, w, h, foot=None, door=NO_DOOR, hollow=False,
         light=0):
    """One prop.

    `foot` is the footprint within the sprite as (x, y, w, h) in tiles - the
    part that stands on the ground and blocks. It defaults to a single tile at
    the sprite's bottom centre, which is right for a tree: three tiles wide and
    four tall, but standing on one, with the canopy overhanging the rows above.

    A building is the case that needs the general form. Its roof overhangs
    upward over tiles that are *behind* it, and the player should be able to
    walk there - so the footprint is the wall body, not the whole sprite.

    `door` is a column offset within the footprint, on its bottom row. That
    cell is left walkable and flagged as a door.

    `hollow` marks a building: its footprint is walls around a walkable
    interior rather than a solid block, so the player can go in. The roof is
    hidden while they are inside - see gg_render.c.

    `light` is how far this thing lights the world, in tiles. Zero for most
    things. This is what makes light come from objects rather than from rules:
    a room is bright because somebody put a lamp in it, not because it is a
    room.
    """
    if foot is None:
        foot = ((w - 1) // 2, h - 1, 1, 1)
    return (name, sheet, col, row, w, h, foot, door, hollow, light)


# ---------------------------------------------------------------------------
# Items - the things that can be picked up
#
# An item is a prop you can carry, so it is declared the same way: a cell of a
# sheet, measured rather than eyeballed. What makes it an item is the rest of
# the row - what it weighs, what using it does, and whether it can be worn.
#
# Weight is in HUNDREDTHS OF A STONE, which is the unit that makes a coin come
# out at 1 and stay an integer. A stone of gold is a hundred coins, which is
# roughly true and, more to the point, means a purse is light and a bar of
# silver is not.
# ---------------------------------------------------------------------------
FOOD = f"{SMALL}/Food"
ORES = f"{SMALL}/Ores & Ingots"
TOOLS = f"{SMALL}/Tools, Smithing.png"
SHIELD_PROP = "Characters/Props/Shield 01 - Heater Shield"


# What using an item does. Mirrored into gg_ids.h.
USE_NONE, USE_EAT, USE_DRINK = 0, 1, 2
# Where an item can be worn or held. GG_SLOT_NONE means it cannot be.
SLOT_NONE, SLOT_LIGHT, SLOT_WEAPON, SLOT_ARMOUR = 0, 1, 2, 3


def item(name, sheet, col, row, w, h, one, many, short, weight,
         stack=True, use=USE_NONE, heal=0, slot=SLOT_NONE, light=0, value=0,
         damage=0, guard=0, reach=0, layers=None, frame=None):
    """One kind of carryable thing.

    `one`/`many` are how it reads in a sentence - "thou takest a loaf of
    bread", "thou takest 3 loaves of bread" - and `short` is the bare noun the
    pack lists. Three strings rather than one plus an "s", because "loaves"
    exists and a game that says "3 breads" has stopped being written.

    `stack` is whether several of them share one slot in the pack. A torch does
    not stack once it is lit, but nothing here burns down yet, so they all do
    except where it would be odd.

    `use` is what happens when the player uses it, `heal` how much health that
    restores. `slot` and `light` are for things that can be held: a lit torch
    is the only equipment that exists, and it is the one that does something -
    see docs/PROJECT_STATUS.md on why there is no sword yet.

    `value` is worth in copper, for when there is trade. Recorded now because
    it is a property of the thing, not of the shop.

    `damage` is what it adds to a blow, `guard` what it turns aside, and
    `reach` how many tiles away it can be used - zero meaning arm's length.
    A thing with reach is thrown, and leaves itself on the ground where it
    lands, which is why a stone is worth picking up again.

    `layers`/`frame` cut the picture out of a character prop instead of a tile
    sheet: several PNGs composited, then the one 64px frame named, then cropped
    to what is actually drawn and centred in a tile. The shield exists only as
    something a person is holding, so this is the only way to get a picture of
    one on its own.
    """
    return (name, sheet, col, row, w, h, one, many, short, weight,
            1 if stack else 0, use, heal, slot, light, value,
            damage, guard, reach, layers, frame)


ITEMS = [
    item("BREAD",  f"{FOOD}/Bread A.png",  2, 0, 1, 1,
         "a loaf of bread", "loaves of bread", "bread",
         weight=40, use=USE_EAT, heal=4, value=5),
    item("APPLE",  f"{FOOD}/Fruit A.png",  3, 0, 1, 1,
         "an apple", "apples", "apples",
         weight=15, use=USE_EAT, heal=2, value=2),
    item("POTION", f"{SMALL}/Kitchen Clutter A.png", 2, 4, 1, 1,
         "a phial of red liquor", "phials of red liquor", "phials",
         weight=60, use=USE_DRINK, heal=15, value=40),
    item("TORCH",  f"{WALLITEM}/Lighting, Wall.png", 2, 0, 1, 2,
         "a torch", "torches", "torches",
         weight=90, slot=SLOT_LIGHT, light=4, value=8),
    item("GOLD",   f"{ORES}/Ore, Gold.png", 1, 2, 1, 1,
         "a gold coin", "gold coins", "gold",
         weight=1, value=10),
    item("SILVER", f"{ORES}/Alloys.png",    2, 0, 1, 1,
         "a bar of silver", "bars of silver", "silver",
         weight=400, value=200),

    # Arms. This art set has no swords that stand on their own - the sword is
    # only ever a layer on a swinging character - so what the vale fights with
    # is what the vale has: a smith's hammer, a stone, and a shield cut out of
    # the one frame that shows it whole.
    item("HAMMER", TOOLS, 8, 0, 1, 1,
         "a smith's hammer", "smith's hammers", "hammer",
         weight=350, stack=False, slot=SLOT_WEAPON, damage=5, value=60),
    item("STONE",  f"{ORES}/Ore, Stone.png", 1, 2, 1, 1,
         "a throwing stone", "throwing stones", "stones",
         weight=30, slot=SLOT_WEAPON, damage=2, reach=5, value=1),
    # Reagents. Nothing is done with them but spent - their whole purpose is
    # to be the price of a spell, so they need no use, no reach and no slot.
    item("GINSENG",    f"{FOOD}/Herbs & Spices.png",  0, 5, 1, 1,
         "a ginseng root", "ginseng roots", "ginseng", weight=5, value=6),
    item("NIGHTSHADE", f"{FOOD}/Herbs & Spices.png", 12, 7, 1, 1,
         "a sprig of nightshade", "sprigs of nightshade", "nightshade",
         weight=3, value=20),
    item("BLOODMOSS",  f"{FOOD}/Herbs & Spices.png",  9, 0, 1, 1,
         "a tuft of blood moss", "tufts of blood moss", "blood moss",
         weight=3, value=10),
    item("ASH",        f"{ORES}/Ore, Coal.png",       0, 2, 1, 1,
         "a pinch of sulphurous ash", "pinches of sulphurous ash", "ash",
         weight=2, value=4),

    item("SHIELD", "", 0, 2, 1, 1,
         "a wooden shield", "wooden shields", "shield",
         weight=500, stack=False, slot=SLOT_ARMOUR, guard=3, value=80,
         layers=[f"{SHIELD_PROP}/Wood/Oak/Combat 1h - Idle.png",
                 f"{SHIELD_PROP}/Paint/Combat 1h - Idle.png"],
         frame=(0, 2)),
]


PROPS = [
    prop("TREE_OAK",     TREES,   4,  0, 3, 4),
    prop("TREE_ELM",     TREES,   7,  0, 3, 4),
    prop("TREE_TALL",    TREES,  10,  0, 3, 4),
    prop("TREE_PINE",    TREES,   4, 12, 3, 4),
    prop("TREE_FIR",     TREES,   7, 12, 3, 4),
    prop("TREE_BARE",    TREES,   0,  8, 3, 5),
    prop("STUMP",        TREES,   1, 14, 1, 1),
    prop("BUSH_ROUND",   PLANTS,  0,  0, 1, 2),
    prop("BUSH_LEAFY",   PLANTS,  2,  0, 1, 2),
    prop("BUSH_CONIFER", PLANTS,  0,  2, 1, 2),
    prop("REEDS",        PLANTS, 11,  0, 1, 2),
    prop("FERN",         PLANTS,  3,  2, 1, 2),
    prop("LILYPAD",      PLANTS, 12,  2, 1, 1),
    prop("CATTAILS",     PLANTS, 14,  2, 1, 2),

    # Buildings. Each is its own file rather than a cell of a sheet, so the
    # column and row are zero and the whole image is the sprite.
    #
    # The footprint is the *whole* building, roof depth included, because that
    # depth is the room: a house drawn in three-quarter view shows its front
    # wall along the bottom and its roof over the space behind, and that space
    # is where the interior goes. The footprint's perimeter becomes walls and
    # its inside becomes floor.
    #
    # Read off a magnified render with a tile grid over it, not guessed: for
    # Brick House A the brick wall runs cols 1-6 of 8, the slate roof covers
    # the rows above it, and the door is the dark opening in column 2.
    # Furnishings. Small enough for a three-tile room, and each stands on one
    # tile. Chest.png is deliberately absent: it is a palette PNG with no
    # transparency, so it would draw as a solid square.
    prop("BARREL",       f"{FURN}/Barrel.png",     0, 0, 1, 2),
    prop("CRATE",        f"{FURN}/Crate.png",      0, 0, 1, 1),
    prop("TABLE",        f"{FURN}/End Table.png",  0, 0, 1, 1),

    # Things that give light. The radius is the whole of it - see gg_render.c
    # for how an emitter reaches only cells on its own side of a wall.
    prop("LAMP",         f"{SMALL}/Lighting, Table.png", 0, 0, 1, 2, light=4),
    # Two tiles tall, like the lamp: the torch's shaft and bracket are on the
    # lower row and only the tip of its flame licks up into the upper one.
    # Taking the upper row alone - which this did - bakes nine texels of flame
    # tip and nothing else, and the check below now refuses that.
    prop("TORCH_WALL",   f"{WALLITEM}/Lighting, Wall.png", 2, 0, 1, 2, light=4),
    prop("CAMPFIRE",     f"{SMALL}/Fire, Camp.png", 1, 1, 1, 1, light=5),

    prop("HOUSE_BRICK_A",  f"{HOUSES}/Brick House A.png",   0, 0, 8, 7,
         foot=(1, 0, 6, 6), door=1, hollow=True),
    prop("HOUSE_BRICK_B",  f"{HOUSES}/Brick House B.png",   0, 0, 6, 6,
         foot=(1, 0, 4, 6), door=2, hollow=True),
    prop("HOUSE_PANELED",  f"{HOUSES}/Paneled House A.png", 0, 0, 5, 5,
         foot=(0, 0, 5, 5), door=3, hollow=True),
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

    # What there is to fight. This set has no monsters at all - it is people,
    # head to foot - so the vale's trouble is people, which suits a caravan
    # that did not arrive better than a wolf would.
    ("BRIGAND", cast(BODY_M, HEAD_M, "Tawny", "Short 04 - Cowlick", "Black",
                     "Shirt 01 - Longsleeve Shirt", "Garnet",
                     "Pants 03 - Pants", "Black", "Shoes 02 - Boots", "Black")),
    ("OUTLAW", cast(BODY_F, HEAD_F, "Bronze", "Medium 06 - Dreadlocks", "Charcoal",
                    "Shirt 01 - Longsleeve Shirt", "Shadow",
                    "Pants 03 - Pants", "Black", "Shoes 02 - Boots", "Leather",
                    sex="Feminine, Thin")),
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
    """Resolve a layer to its Walk.png.

    Some sheets have no colour axis at all, and for those the bare Walk.png is
    the right answer. A colour that was *asked for* and does not exist is not:
    that used to fall through to the uncoloured base, and two brigands were
    baked and committed in white shirts because "Rust" and "Slate" are not
    colours this set has. A misspelling has to be an error, not a costume.

    Returns None if the layer does not exist for this body at all, which is a
    different thing and stays a note.
    """
    base = os.path.join(LPC, "Characters", directory)
    if not os.path.isdir(base):
        return None

    if colour:
        tinted = os.path.join(base, colour, "Walk.png")
        if os.path.exists(tinted):
            return tinted
        # Only forgive the miss when this sheet has no colours to choose from.
        has_colours = any(os.path.isdir(os.path.join(base, d))
                          for d in os.listdir(base))
        if has_colours:
            choices = sorted(d for d in os.listdir(base)
                             if os.path.isdir(os.path.join(base, d)))
            sys.exit(f"no colour '{colour}' for {directory}. It has: "
                     + ", ".join(choices))

    plain = os.path.join(base, "Walk.png")
    return plain if os.path.exists(plain) else None


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
    images = {}          # named fills, for the synthesised overlay rings
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
        images[name] = tile
    return out, entries, images


def _mean_rgb(img):
    px = list(img.convert("RGB").getdata())
    n = len(px)
    return tuple(sum(p[i] for p in px) / n for i in range(3))


def _bank_weight(piece, interior_mean, threshold=55.0):
    """How strongly each pixel of a ring piece reads as boundary, 0.0 to 1.0.

    A weight rather than a yes/no, because the three sets differ in kind. The
    grass and sand banks are hard-edged art, so their weights are already
    almost all 0 or 1 and a blend against them stays crisp. The
    shallow-on-deep set is a soft radial gradient, and a binary mask turned its
    composed corners into hard-edged rectangles of the wrong colour - visibly
    worse than the missing corner it replaced.

    Distance from the interior tile's mean colour, rather than a hue test: one
    of the three sets is water on water, so anything looking for "blue" would
    find boundary everywhere in it.
    """
    rgb = piece.convert("RGB").load()
    alpha = piece.getchannel("A").load()
    w = [0.0] * (TILE * TILE)
    ir, ig, ib = interior_mean
    for y in range(TILE):
        for x in range(TILE):
            if alpha[x, y] == 0:
                continue
            r, g, b = rgb[x, y]
            d = ((r - ir) ** 2 + (g - ig) ** 2 + (b - ib) ** 2) ** 0.5
            w[y * TILE + x] = min(1.0, d / threshold)
    return w


def _inner_corner(interior, outer, w_a, w_b):
    """Build a concave corner: interior everywhere, boundary only where the two
    adjoining straight edges both had it.

    The LPC sheets carry no inner corners, so this is where they come from. The
    overlap of the N band and the W band is exactly the top-left nub an inner
    NW corner needs, and taking the pixels from the *outer* NW corner means the
    art in that nub is already drawn for that orientation - it is a crop of
    real corner art, not a synthesised blob.

    The two weights are combined with min(), which is the soft reading of
    "both": a pixel is corner only as far as it is boundary in both directions.
    """
    out = interior.copy()
    src = outer.convert("RGBA").load()
    dst = out.load()
    for y in range(TILE):
        for x in range(TILE):
            t = min(w_a[y * TILE + x], w_b[y * TILE + x])
            if t <= 0.0:
                continue
            sr, sg, sb, sa = src[x, y]
            dr, dg, db, da = dst[x, y]
            # Alpha is blended too, not carried over from the base. The edge
            # sets composite onto an opaque interior where it makes no
            # difference, but an overlay ring composites onto nothing - keeping
            # the base's alpha there produced four entirely empty corners.
            dst[x, y] = (round(dr + (sr - dr) * t),
                         round(dg + (sg - dg) * t),
                         round(db + (sb - db) * t),
                         round(da + (sa - da) * t))
    return out


def build_edges():
    """One block per row: the nine ring cells, then the four concave corners.

    Laid out 13 wide so a set's cells stay contiguous and the renderer can
    index them arithmetically.
    """
    cells_per_set = 13
    out = Image.new("RGBA", (cells_per_set * TILE, TILE * len(EDGES)), (0, 0, 0, 0))
    entries = []
    cache = {}
    for i, (name, rel, c, r) in enumerate(EDGES):
        if rel not in cache:
            cache[rel] = sheet(rel)
        src = cache[rel]

        piece = [src.crop(((c + k % 3) * TILE, (r + k // 3) * TILE,
                          (c + k % 3 + 1) * TILE, (r + k // 3 + 1) * TILE))
                 for k in range(9)]

        # The centre must be the pool's interior, not a hole: the sheet carries
        # hole variants of every block, they look the same in a thumbnail, and
        # picking one leaves the water see-through in play.
        interior = piece[4]
        if interior.getchannel("A").getextrema()[0] != 255:
            sys.exit(f"edge set {name} at {rel} ({c},{r}) has a transparent "
                     f"centre - that is the hole variant, not the fill")

        mean = _mean_rgb(interior)
        m_n = _bank_weight(piece[1], mean)
        m_s = _bank_weight(piece[7], mean)
        m_w = _bank_weight(piece[3], mean)
        m_e = _bank_weight(piece[5], mean)

        inner = [
            _inner_corner(interior, piece[0], m_n, m_w),   # 9  inner NW
            _inner_corner(interior, piece[2], m_n, m_e),   # 10 inner NE
            _inner_corner(interior, piece[6], m_s, m_w),   # 11 inner SW
            _inner_corner(interior, piece[8], m_s, m_e),   # 12 inner SE
        ]
        for k, img in enumerate(inner):
            covered = sum(1 for v in _bank_weight(img, mean) if v > 0.5)
            if covered == 0:
                sys.exit(f"edge set {name}: composed inner corner {k} is all "
                         f"interior - the boundary masks did not intersect, so "
                         f"the threshold is wrong for this set")

        y = i * TILE
        for k, img in enumerate(piece + inner):
            out.paste(img, (k * TILE, y))
        entries.append((name, 0, y))
    return out, entries


# ---------------------------------------------------------------------------
# Overlay rings - land meeting land.
#
# Different in kind from the edge sets above. An edge set *replaces* a cell's
# tile; an overlay is drawn *over* one, so the dominant terrain can bleed a
# soft margin onto whatever it abuts without a tile per pair. The sheet carries
# exactly one such ring - grass, at (0,0) - and that is the one that matters,
# because grass is the ground everything else is a patch in.
#
# The ring is authored as a blob of grass fading outward, so its pieces are
# oriented the opposite way round from an edge set's: the piece with grass
# along its top edge sits at the ring's *south* position. Rather than make the
# renderer know that, the bake rotates the ring 180 degrees (index k -> 8-k) so
# both kinds index identically: piece k means "the other terrain lies in
# direction k".
# The sheet carries exactly one overlay ring, grass's. The others are
# *synthesised* from it: the ring's alpha is a jagged organic fringe, and
# filling that shape with another terrain's own fill tile gives that terrain
# the same irregular boundary. Checked by eye before being relied on - at this
# tile size the fringe reads as a natural edge rather than as grass blades, so
# sand and dirt wear it convincingly.
#
# Entries are (name, fill tile name), or (name, sheet, col, row) for a real
# ring read straight from the art.
OVERLAYS = [
    ("GRASS",    TERRAIN, 0, 0),   # real art; the shape all the others borrow
    ("FARMLAND", "FARMLAND"),
    ("DIRT",     "DIRT"),
    ("ROAD",     "ROAD"),
    ("SAND",     "SAND"),
    ("DESERT",   "DESERT"),
]


def _alpha_weight(piece):
    """An overlay ring's own alpha is its mask - no colour test needed."""
    a = piece.getchannel("A").load()
    return [a[x, y] / 255.0 for y in range(TILE) for x in range(TILE)]


def build_overlays(tile_images):
    cells_per_set = 13
    out = Image.new("RGBA", (cells_per_set * TILE, TILE * len(OVERLAYS)), (0, 0, 0, 0))
    entries = []
    cache = {}
    shape = None            # the grass ring's alpha, borrowed by the rest

    for i, spec in enumerate(OVERLAYS):
        name = spec[0]

        if len(spec) == 4:
            _, rel, c, r = spec
            if rel not in cache:
                cache[rel] = sheet(rel)
            src = cache[rel]
            ring = [src.crop(((c + k % 3) * TILE, (r + k // 3) * TILE,
                             (c + k % 3 + 1) * TILE, (r + k // 3 + 1) * TILE))
                    for k in range(9)]
            if ring[4].getchannel("A").getextrema()[0] != 255:
                sys.exit(f"overlay {name} at {rel} ({c},{r}) has a transparent "
                         f"centre - that is not a filled ring")
            # Rotate into "the other terrain lies in direction k" order.
            piece = [ring[8 - k] for k in range(9)]
            if shape is None:
                shape = [p.getchannel("A") for p in piece]
        else:
            if shape is None:
                sys.exit("the first overlay entry must be a real ring - the "
                         "synthesised ones borrow its shape")
            fill_name = spec[1]
            if fill_name not in tile_images:
                sys.exit(f"overlay {name} wants fill tile {fill_name}, which is "
                         f"not in TILES")
            fill = tile_images[fill_name]
            piece = []
            for k in range(9):
                img = fill.copy()
                img.putalpha(shape[k])
                piece.append(img)

        # Concave corners start from *nothing* rather than from a fill: a cell
        # touched only on one diagonal should carry a nub of grass and be
        # otherwise untouched, so whatever is under it shows through.
        blank = Image.new("RGBA", (TILE, TILE), (0, 0, 0, 0))
        w_n, w_s = _alpha_weight(piece[1]), _alpha_weight(piece[7])
        w_w, w_e = _alpha_weight(piece[3]), _alpha_weight(piece[5])
        inner = [
            _inner_corner(blank, piece[0], w_n, w_w),
            _inner_corner(blank, piece[2], w_n, w_e),
            _inner_corner(blank, piece[6], w_s, w_w),
            _inner_corner(blank, piece[8], w_s, w_e),
        ]
        for k, img in enumerate(inner):
            if img.getchannel("A").getextrema()[1] == 0:
                sys.exit(f"overlay {name}: composed inner corner {k} is empty")

        y = i * TILE
        for k, img in enumerate(piece + inner):
            out.paste(img, (k * TILE, y))
        entries.append((name, 0, y))
    return out, entries


def build_props():
    packer = Shelf(640)
    entries = []
    cache = {}
    for name, rel, c, r, wt, ht, foot, door, hollow, light in PROPS:
        if rel not in cache:
            cache[rel] = sheet(rel)
        src = cache[rel]
        img = src.crop((c * TILE, r * TILE, (c + wt) * TILE, (r + ht) * TILE))
        if not img.getchannel("A").getextrema()[1]:
            sys.exit(f"prop {name} at {rel} ({c},{r}) is entirely transparent - "
                     f"the pick is wrong")

        fx, fy, fw, fh = foot
        if fx + fw > wt or fy + fh > ht:
            sys.exit(f"prop {name}: footprint {foot} falls outside a {wt}x{ht} "
                     f"sprite")
        if door != NO_DOOR and door >= fw:
            sys.exit(f"prop {name}: door column {door} is outside a footprint "
                     f"{fw} wide")
        if hollow and (fw < 3 or fh < 3):
            sys.exit(f"prop {name} is hollow but its {fw}x{fh} footprint has "
                     f"no room inside its own walls")
        if hollow and door == NO_DOOR:
            sys.exit(f"prop {name} is hollow with no door - nothing could get in")
        if light > GG_LIGHT_MAX_RADIUS:
            sys.exit(f"prop {name} lights {light} tiles, past the "
                     f"{GG_LIGHT_MAX_RADIUS} the renderer scans for emitters")
        if hollow and not (1 <= door <= fw - 2):
            # A door in a corner column opens onto the side wall rather than
            # into the room. It looks perfectly fine from outside and is only
            # discovered by walking through it, which is how this was found.
            sys.exit(f"prop {name}: door column {door} is a corner of a {fw}-wide "
                     f"footprint, so it opens onto a wall. It must be between 1 "
                     f"and {fw - 2}.")

        # The anchor is the footprint's bottom-centre: the map cell the object
        # stands on. Derived here rather than declared, so a footprint and its
        # anchor cannot disagree.
        anchor_x = fx + (fw - 1) // 2
        anchor_y = fy + fh - 1

        # The anchor cell must have a real amount drawn on it, not merely a
        # texel. "Not entirely transparent" was the old test, and TORCH_WALL
        # passed it with nine texels of flame tip - 0.9% of the cell - so the
        # torch was baked, shipped and invisible. Every genuine prop clears 10%
        # (cattails are the sparsest), so the bar sits at 5%: low enough to
        # accuse nothing real, high enough that a speck cannot pass.
        cell = img.crop((anchor_x * TILE, anchor_y * TILE,
                         (anchor_x + 1) * TILE, (anchor_y + 1) * TILE))
        drawn = sum(1 for a in cell.getchannel("A").get_flattened_data() if a > 16)
        percent = 100 * drawn // (TILE * TILE)
        if percent < 5:
            sys.exit(f"prop {name}: only {percent}% of the anchor cell "
                     f"({anchor_x},{anchor_y}) is drawn - the pick or the "
                     f"footprint is wrong")

        x, y, w, h = packer.add(name, img)
        entries.append((name, x, y, w, h, wt, ht, anchor_x, anchor_y,
                        fw, fh, door, 1 if hollow else 0, light))
    return packer.render(), entries


def build_items():
    """Items are packed like props, and measured to the same bar.

    An item is drawn at its bottom row, the same as a prop stands on its
    anchor cell, so that a torch lying on the ground and a torch bracketed to
    a wall are the same picture in the same place.
    """
    packer = Shelf(512)
    entries = []
    cache = {}
    for (name, rel, c, r, wt, ht, one, many, short, weight,
         stack, use, heal, slot, light, value,
         damage, guard, reach, layers, frame) in ITEMS:
        if layers:
            # A character prop: composite the layers, take the named 64px
            # frame, crop to what is drawn, and centre that in a tile. A shield
            # exists in this set only as something somebody is holding, so
            # there is no cell of a sheet to point at.
            comp = None
            for rel_layer in layers:
                one_layer = sheet(rel_layer)
                comp = one_layer if comp is None else \
                    Image.alpha_composite(comp, one_layer)
            fx, fy = frame
            cut = comp.crop((fx * FRAME, fy * FRAME,
                             (fx + 1) * FRAME, (fy + 1) * FRAME))
            box = cut.getbbox()
            if not box:
                sys.exit(f"item {name}: frame {frame} of its prop is empty")
            drawn_part = cut.crop(box)
            if drawn_part.width > TILE or drawn_part.height > TILE:
                sys.exit(f"item {name}: {drawn_part.width}x{drawn_part.height} "
                         f"does not fit a {TILE}px tile")
            img = Image.new("RGBA", (TILE * wt, TILE * ht), (0, 0, 0, 0))
            img.paste(drawn_part,
                      ((TILE - drawn_part.width) // 2,
                       (TILE - drawn_part.height) // 2))
        else:
            if rel not in cache:
                cache[rel] = sheet(rel)
            img = cache[rel].crop((c * TILE, r * TILE,
                                   (c + wt) * TILE, (r + ht) * TILE))

        # The bottom row is what sits on the ground, so that is the cell that
        # has to carry the picture - the same rule, and the same 5% bar, as a
        # prop's anchor. The wall torch is exactly why: its top row is nine
        # texels of flame tip.
        cell = img.crop((0, (ht - 1) * TILE, TILE, ht * TILE))
        drawn = sum(1 for a in cell.getchannel("A").get_flattened_data() if a > 16)
        percent = 100 * drawn // (TILE * TILE)
        if percent < 5:
            sys.exit(f"item {name}: only {percent}% of its ground cell is drawn "
                     f"at {rel} ({c},{r}) - the pick is wrong")

        if slot != SLOT_NONE and light > GG_LIGHT_MAX_RADIUS:
            sys.exit(f"item {name} lights {light} tiles, past the "
                     f"{GG_LIGHT_MAX_RADIUS} the renderer scans for emitters")
        if use != USE_NONE and heal == 0:
            sys.exit(f"item {name} can be used but does nothing - either give it "
                     f"an effect or make it USE_NONE")
        if heal and use == USE_NONE:
            sys.exit(f"item {name} heals {heal} but cannot be used")

        # A slot that turns nothing aside and strikes no harder is a control
        # that does nothing, which is the thing this project keeps refusing to
        # ship. Every equippable has to earn its slot.
        if slot == SLOT_WEAPON and damage == 0:
            sys.exit(f"item {name} is a weapon that does no damage")
        if slot == SLOT_ARMOUR and guard == 0:
            sys.exit(f"item {name} is armour that turns nothing aside")
        if slot == SLOT_LIGHT and light == 0:
            sys.exit(f"item {name} is held as a light but gives none")
        if reach and slot != SLOT_WEAPON:
            sys.exit(f"item {name} has reach but is not a weapon")

        x, y, w, h = packer.add(name, img)
        entries.append((name, x, y, w, h, wt, ht, one, many, short, weight,
                        stack, use, heal, slot, light, value,
                        damage, guard, reach))
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


def emit_ids(tiles, props, actors, items, path):
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

    o.append("typedef enum {")
    for name, *_ in items:
        o.append(f"    GG_ITEM_{name},")
    o.append("    GG_ITEM_COUNT")
    o.append("} gg_item_id;")
    o.append("")
    o.append("// What using a thing does.")
    o.append("typedef enum { GG_USE_NONE, GG_USE_EAT, GG_USE_DRINK } gg_item_use;")
    o.append("")
    o.append("// Where a thing can be held. Each slot has to do something: a")
    o.append("// torch lights, a weapon strikes, a shield turns a blow aside.")
    o.append("typedef enum {")
    o.append("    GG_SLOT_NONE, GG_SLOT_LIGHT, GG_SLOT_WEAPON, GG_SLOT_ARMOUR,")
    o.append("    GG_SLOT_COUNT")
    o.append("} gg_slot_id;")
    o.append("")
    o.append("// A kind of carryable thing.")
    o.append("//")
    o.append("//   id              the name a data file calls it by - GINSENG - as")
    o.append("//                   opposed to the prose, which has spaces in it")
    o.append("//   one/many/short  how it reads in a sentence, and bare in a list")
    o.append("//   tiles_w/h       the sprite, in tiles, drawn from its bottom row")
    o.append("//   weight          in HUNDREDTHS OF A STONE - the unit that makes a")
    o.append("//                   coin weigh 1 and stay an integer")
    o.append("//   stack           whether several share one slot in the pack")
    o.append("//   use/heal        what using it does, and how much health it gives")
    o.append("//   slot/light      where it is held, and how far it lights while held")
    o.append("//   value           worth in copper, for when there is trade")
    o.append("//   damage/guard    what it adds to a blow, what it turns aside")
    o.append("//   reach           how many tiles away it strikes; 0 is arm's length,")
    o.append("//                   and anything further is thrown and left where it lands")
    o.append("typedef struct {")
    o.append("    const char *id;")
    o.append("    const char *one, *many, *short_name;")
    o.append("    uint8_t  tiles_w, tiles_h;")
    o.append("    uint16_t weight;")
    o.append("    uint8_t  stack;")
    o.append("    uint8_t  use, heal;")
    o.append("    uint8_t  slot, light;")
    o.append("    uint16_t value;")
    o.append("    uint8_t  damage, guard, reach;")
    o.append("} gg_item_def;")
    o.append("extern const gg_item_def GG_ITEM[GG_ITEM_COUNT];")
    o.append("")
    o.append("// Row order within an actor's block, matching LPC's Walk.png.")
    o.append("typedef enum { GG_FACE_UP, GG_FACE_LEFT, GG_FACE_DOWN, "
             "GG_FACE_RIGHT } gg_facing;")
    o.append("")
    o.append("// The largest radius any light may have. The renderer scans a box")
    o.append("// this big around each cell looking for emitters, so a brighter one")
    o.append("// would simply be clipped - this script refuses one rather than let")
    o.append("// that happen. Emitted here rather than written in two files that")
    o.append("// have to agree by hand, and here rather than in the renderer so the")
    o.append("// simulation can bound a spell without reaching across the layer.")
    o.append(f"#define GG_LIGHT_MAX_RADIUS {GG_LIGHT_MAX_RADIUS}")
    o.append("")
    o.append(f"#define GG_ACTOR_FRAMES  {WALK_FRAMES}")
    o.append(f"#define GG_ACTOR_DIRS    {DIRS}")
    o.append("")
    o.append("// A prop's geometry, which the simulation needs for collision and")
    o.append("// the renderer for placement.")
    o.append("//")
    o.append("//   tiles_w/h   the sprite, in tiles")
    o.append("//   anchor_x/y  the sprite cell that sits on the map cell the prop")
    o.append("//               occupies - always the footprint's bottom centre")
    o.append("//   foot_w/h    the footprint: what stands on the ground and blocks.")
    o.append("//               The sprite may be larger and overhang the rows above,")
    o.append("//               which is how a roof covers tiles you can walk behind.")
    o.append("//   door_dx     column within the footprint that is a doorway, or")
    o.append("//               GG_NO_DOOR. That cell stays walkable.")
    o.append("#define GG_NO_DOOR 255")
    o.append("typedef struct {")
    o.append("    uint8_t tiles_w, tiles_h;")
    o.append("    uint8_t anchor_x, anchor_y;")
    o.append("    uint8_t foot_w, foot_h;")
    o.append("    uint8_t door_dx;")
    o.append("    uint8_t hollow;   // a building: walls around a walkable interior")
    o.append("    uint8_t light;    // how far it lights the world, in tiles; 0 for most")
    o.append("} gg_prop_size;")
    o.append("extern const gg_prop_size GG_PROP_SIZE[GG_PROP_COUNT];")
    o.append("")
    o.append("#endif // GG_IDS_H")

    with open(path, "w") as f:
        f.write("\n".join(o) + "\n")


def emit_sizes(props, items, path):
    """The one generated .c file - tables the simulation links against."""
    o = [banner("gg_ids.c", "Prop geometry and the item table, for the "
                            "simulation."),
         '#include "core/gg_ids.h"', "",
         "const gg_prop_size GG_PROP_SIZE[GG_PROP_COUNT] = {"]
    for name, x, y, w, h, wt, ht, ax, ay, fw, fh, door, hollow, light in props:
        o.append(f"    [GG_PROP_{name}] = {{ {wt}, {ht}, {ax}, {ay}, "
                 f"{fw}, {fh}, {door}, {hollow}, {light} }},")
    o.append("};")
    o.append("")

    o.append("const gg_item_def GG_ITEM[GG_ITEM_COUNT] = {")
    for (name, x, y, w, h, wt, ht, one, many, short, weight,
         stack, use, heal, slot, light, value, damage, guard, reach) in items:
        uses = ["GG_USE_NONE", "GG_USE_EAT", "GG_USE_DRINK"][use]
        slots = ["GG_SLOT_NONE", "GG_SLOT_LIGHT",
                 "GG_SLOT_WEAPON", "GG_SLOT_ARMOUR"][slot]
        o.append(f"    [GG_ITEM_{name}] = {{")
        o.append(f'        "{name}",')
        o.append(f'        "{one}", "{many}", "{short}",')
        o.append(f"        {wt}, {ht}, {weight}, {stack},")
        o.append(f"        {uses}, {heal}, {slots}, {light}, {value},")
        o.append(f"        {damage}, {guard}, {reach},")
        o.append("    },")
    o.append("};")
    with open(path, "w") as f:
        f.write("\n".join(o) + "\n")


def emit_atlas(tiles, props, actors, items, edges, overlays, font_meta, path):
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
    for name, x, y, w, h, *_rest in props:
        o.append(f"    [GG_PROP_{name}] = {{ {x:4d}, {y:4d}, {w:3d}, {h:3d} }},")
    o.append("};")
    o.append("")

    o.append("// atlas_items.png - the things that can be picked up.")
    o.append("static const gg_rect GG_ITEM_RECT[GG_ITEM_COUNT] = {")
    for name, x, y, w, h, *_rest in items:
        o.append(f"    [GG_ITEM_{name}] = {{ {x:4d}, {y:4d}, {w:3d}, {h:3d} }},")
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

    o.append("// --- edge sets: 3x3 blob rings, in atlas_edges.png ---")
    o.append("// Index order, which is what gg_render computes from a neighbour mask:")
    o.append("//     0 NW  1 N  2 NE / 3 W  4 C  5 E / 6 SW  7 S  8 SE")
    o.append("typedef enum {")
    for name, *_ in edges:
        o.append(f"    GG_EDGE_{name},")
    o.append("    GG_EDGE_COUNT")
    o.append("} gg_edge_id;")
    o.append("")
    o.append("#define GG_EDGE_NW 0")
    o.append("#define GG_EDGE_N  1")
    o.append("#define GG_EDGE_NE 2")
    o.append("#define GG_EDGE_W  3")
    o.append("#define GG_EDGE_C  4")
    o.append("#define GG_EDGE_E  5")
    o.append("#define GG_EDGE_SW 6")
    o.append("#define GG_EDGE_S  7")
    o.append("#define GG_EDGE_SE 8")
    o.append("// Concave corners, composed at bake time - the sheets have none.")
    o.append("#define GG_EDGE_IN_NW 9")
    o.append("#define GG_EDGE_IN_NE 10")
    o.append("#define GG_EDGE_IN_SW 11")
    o.append("#define GG_EDGE_IN_SE 12")
    o.append("#define GG_EDGE_PIECES 13")
    o.append("")
    o.append("static const gg_rect GG_EDGE_RECT[GG_EDGE_COUNT][GG_EDGE_PIECES] = {")
    for name, x, y in edges:
        cells = ", ".join(f"{{ {x + i * TILE:4d}, {y:4d}, {TILE:3d}, {TILE:3d} }}"
                          for i in range(13))
        o.append(f"    [GG_EDGE_{name}] = {{ {cells} }},")
    o.append("};")
    o.append("")

    o.append("// --- overlay rings: land meeting land, drawn OVER a base tile ---")
    o.append("// Same 13-piece indexing as the edge sets: piece k means \"the")
    o.append("// other terrain lies in direction k\". In atlas_overlays.png.")
    o.append("typedef enum {")
    for name, *_ in overlays:
        o.append(f"    GG_OVERLAY_{name},")
    o.append("    GG_OVERLAY_COUNT")
    o.append("} gg_overlay_id;")
    o.append("")
    o.append("static const gg_rect GG_OVERLAY_RECT[GG_OVERLAY_COUNT][GG_EDGE_PIECES] = {")
    for name, x, y in overlays:
        cells = ", ".join(f"{{ {x + i * TILE:4d}, {y:4d}, {TILE:3d}, {TILE:3d} }}"
                          for i in range(13))
        o.append(f"    [GG_OVERLAY_{name}] = {{ {cells} }},")
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
                  {os.path.dirname(rel) for _n, rel, *_ in ITEMS if rel} |
                  {os.path.dirname(l) for it in ITEMS if it[-2]
                   for l in it[-2]} |
                  {os.path.dirname(rel) for _n, rel, *_ in EDGES} |
                  {os.path.dirname(s[1]) for s in OVERLAYS if len(s) == 4} |
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

    tiles_img, tiles, tile_images = build_tiles()
    tiles_img.save(os.path.join(ASSETS, "atlas_tiles.png"))
    print(f"  tiles   {tiles_img.size[0]:4d}x{tiles_img.size[1]:<4d} {len(tiles):3d} entries")

    edges_img, edges = build_edges()
    edges_img.save(os.path.join(ASSETS, "atlas_edges.png"))
    print(f"  edges   {edges_img.size[0]:4d}x{edges_img.size[1]:<4d} {len(edges):3d} sets")

    ov_img, overlays = build_overlays(tile_images)
    ov_img.save(os.path.join(ASSETS, "atlas_overlays.png"))
    print(f"  overlay {ov_img.size[0]:4d}x{ov_img.size[1]:<4d} {len(overlays):3d} sets")

    props_img, props = build_props()
    props_img.save(os.path.join(ASSETS, "atlas_props.png"))
    print(f"  props   {props_img.size[0]:4d}x{props_img.size[1]:<4d} {len(props):3d} entries")

    items_img, items = build_items()
    items_img.save(os.path.join(ASSETS, "atlas_items.png"))
    print(f"  items   {items_img.size[0]:4d}x{items_img.size[1]:<4d} {len(items):3d} entries")

    actors_img, actors = build_actors()
    actors_img.save(os.path.join(ASSETS, "atlas_actors.png"))
    print(f"  actors  {actors_img.size[0]:4d}x{actors_img.size[1]:<4d} {len(actors):3d} entries")

    font_img, font_meta = build_font()
    font_img.save(os.path.join(ASSETS, "atlas_font.png"))
    print(f"  font    {font_img.size[0]:4d}x{font_img.size[1]:<4d} "
          f"cell {font_meta[2]}x{font_meta[3]}")

    src = os.path.join(ROOT, "src")
    emit_ids(tiles, props, actors, items, os.path.join(src, "core", "gg_ids.h"))
    emit_sizes(props, items, os.path.join(src, "core", "gg_ids.c"))
    emit_atlas(tiles, props, actors, items, edges, overlays, font_meta,
               os.path.join(src, "gfx", "gg_atlas.h"))
    emit_credits(os.path.join(ASSETS, "CREDITS.md"))
    print("  wrote src/core/gg_ids.{h,c}, src/gfx/gg_atlas.h, assets/CREDITS.md")


if __name__ == "__main__":
    main()
