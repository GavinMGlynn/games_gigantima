#!/usr/bin/env python3
"""Report which cells of an LPC sheet can serve as a base terrain fill.

Picking ground tiles by eye does not work. The LPC terrain sheets are laid out
as 3x3 blob rings - eight edge and corner pieces around a centre - and at a
glance a ring's centre and its neighbours look alike, so a plausible-looking
pick lands on a tile with a transparent hole or an edge baked into it. That is
invisible in a screenshot of one tile and glaring in a field of them.

Two properties decide it, and both are measurable:

  opaque   A ground tile has no transparent pixels. Anything less is a ring
           piece meant to be drawn over another terrain.
  seam     A ground tile butts against a copy of itself invisibly. Comparing
           the right edge column against the left, and the bottom row against
           the top, gives the discontinuity directly - near zero tiles cleanly,
           a large number is an edge piece.

`sd` is the per-channel standard deviation, which separates a flat fill from a
textured one. It is informational: a good grass tile has some texture, so a low
`sd` is not required, only a low seam.

    python3 tools/scan_sheet.py ext/lpc-revised/Terrain/terrain_summer.png
    python3 tools/scan_sheet.py --seam 6 ext/lpc-revised/Terrain/cliff_summer.png

Cells that are not fully opaque are omitted; they cannot be ground.
"""
import argparse
import statistics as st
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("scan_sheet.py needs Pillow:  python3 -m pip install --user Pillow")

TILE = 32


def scan(path, tile, max_seam):
    im = Image.open(path).convert("RGBA")
    cols, rows = im.width // tile, im.height // tile
    print(f"# {path}  {im.width}x{im.height}  {cols}x{rows} cells of {tile}px")
    print(f"# showing fully-opaque cells with seam <= {max_seam}")
    print("# col,row  mean RGB         sd     seam(L-R, T-B)")

    hits = 0
    for r in range(rows):
        for c in range(cols):
            t = im.crop((c * tile, r * tile, (c + 1) * tile, (r + 1) * tile))
            px = list(t.getdata())
            if any(p[3] != 255 for p in px):
                continue

            chans = [[p[i] for p in px] for i in range(3)]
            sd = sum(st.pstdev(ch) for ch in chans) / 3

            left = [px[y * tile] for y in range(tile)]
            right = [px[y * tile + tile - 1] for y in range(tile)]
            top, bottom = px[:tile], px[(tile - 1) * tile:]
            lr = sum(abs(a[i] - b[i]) for a, b in zip(right, left)
                     for i in range(3)) / (tile * 3)
            tb = sum(abs(a[i] - b[i]) for a, b in zip(bottom, top)
                     for i in range(3)) / (tile * 3)
            if max(lr, tb) > max_seam:
                continue

            mean = tuple(int(st.mean(ch)) for ch in chans)
            print(f"{c:3d},{r:-3d}  ({mean[0]:3d},{mean[1]:3d},{mean[2]:3d})"
                  f"  sd{sd:5.1f}  seam({lr:5.1f},{tb:5.1f})")
            hits += 1

    print(f"# {hits} candidate cell(s)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("sheet", nargs="+")
    ap.add_argument("--tile", type=int, default=TILE, help="cell size (default 32)")
    ap.add_argument("--seam", type=float, default=6.0,
                    help="largest edge discontinuity to report (default 6)")
    a = ap.parse_args()
    for s in a.sheet:
        scan(s, a.tile, a.seam)


if __name__ == "__main__":
    main()
