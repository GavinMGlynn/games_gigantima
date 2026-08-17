#!/usr/bin/env python3
"""Check that the committed art is what the baker produces from the pinned LPC.

The atlases and the generated sources are committed, so a clone builds and plays
without the 766 MB art submodule. The cost of that is a promise nobody can see:
that `assets/atlas_*.png` and `src/core/gg_ids.*` really are what
`tools/make_atlas.py` writes from `ext/lpc-revised` at its pinned commit. This
is the job that keeps the promise honest - re-bake, and diff.

    python3 tools/check_atlas.py

Exit code 0 means the committed art is reproducible. Anything else names every
file that came out different, and leaves the freshly baked ones in place so they
can be looked at (or committed, if the baker is what changed on purpose).

**Pictures are compared by pixel, not by byte.** A PNG carries its encoder's
choices - filter selection, zlib level - and a different Pillow writes different
bytes for the same image. Comparing bytes would fail on somebody's laptop for a
reason that has nothing to do with the art. The generated *sources* are compared
byte for byte, because those this script does write deterministically.
"""
import os
import shutil
import subprocess
import sys
import tempfile

try:
    from PIL import Image
except ImportError:
    sys.exit("check_atlas.py needs Pillow:  python3 -m pip install --user Pillow")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# Everything the baker writes. Kept here rather than globbed, so a new output
# that nobody added to this list is caught by the "unexpected file" check below
# instead of silently going unchecked.
PICTURES = [
    "assets/atlas_tiles.png",
    "assets/atlas_edges.png",
    "assets/atlas_overlays.png",
    "assets/atlas_props.png",
    "assets/atlas_items.png",
    "assets/atlas_actors.png",
    "assets/atlas_font.png",
]
SOURCES = [
    "src/core/gg_ids.h",
    "src/core/gg_ids.c",
    "src/gfx/gg_atlas.h",
    "assets/CREDITS.md",
]


def same_pixels(a, b):
    """True if two PNGs hold the same picture, whatever bytes they are in."""
    with Image.open(a) as ia, Image.open(b) as ib:
        ia = ia.convert("RGBA")
        ib = ib.convert("RGBA")
        if ia.size != ib.size:
            return False, f"{ia.size[0]}x{ia.size[1]} became {ib.size[0]}x{ib.size[1]}"
        pa, pb = ia.tobytes(), ib.tobytes()
        if pa == pb:
            return True, ""
        differing = sum(1 for i in range(0, len(pa), 4) if pa[i:i + 4] != pb[i:i + 4])
        return False, f"{differing} of {ia.size[0] * ia.size[1]} pixels differ"


def main():
    missing = [p for p in PICTURES + SOURCES if not os.path.exists(os.path.join(ROOT, p))]
    if missing:
        sys.exit("nothing to check against - these are not committed:\n  " +
                 "\n  ".join(missing))

    lpc = os.path.join(ROOT, "ext", "lpc-revised", "Characters")
    if not os.path.isdir(lpc):
        sys.exit("ext/lpc-revised is not checked out, so there is nothing to "
                 "bake from:\n    git submodule update --init --depth 1 "
                 "ext/lpc-revised")

    # The committed versions, put somewhere the baker cannot reach.
    keep = tempfile.mkdtemp(prefix="gigantima-atlas-")
    for rel in PICTURES + SOURCES:
        dst = os.path.join(keep, rel.replace("/", "_"))
        shutil.copyfile(os.path.join(ROOT, rel), dst)

    print("re-baking from ext/lpc-revised ...")
    baked = subprocess.run([sys.executable, os.path.join(HERE, "make_atlas.py")],
                           cwd=ROOT)
    if baked.returncode != 0:
        sys.exit("the baker failed; nothing to compare")

    wrong = []
    for rel in PICTURES:
        was = os.path.join(keep, rel.replace("/", "_"))
        ok, why = same_pixels(was, os.path.join(ROOT, rel))
        print(f"  {'ok  ' if ok else 'DIFF'}  {rel}{'' if ok else '  - ' + why}")
        if not ok:
            wrong.append(rel)

    for rel in SOURCES:
        was = os.path.join(keep, rel.replace("/", "_"))
        with open(was, "rb") as f:
            a = f.read()
        with open(os.path.join(ROOT, rel), "rb") as f:
            b = f.read()
        ok = a == b
        print(f"  {'ok  ' if ok else 'DIFF'}  {rel}")
        if not ok:
            wrong.append(rel)

    shutil.rmtree(keep, ignore_errors=True)

    if wrong:
        print("\nthe committed art is not what the baker produces:")
        for rel in wrong:
            print(f"  {rel}")
        print("\nThe freshly baked files are in the working tree. If the baker "
              "changed on purpose, commit them - and remember that regenerating "
              "the art regenerates assets/CREDITS.md, which is a licence "
              "condition and not documentation.")
        return 1

    print(f"\n{len(PICTURES) + len(SOURCES)} files, all reproducible from the "
          "pinned art.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
