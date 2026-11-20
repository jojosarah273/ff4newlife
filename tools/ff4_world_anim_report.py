#!/usr/bin/env python3
"""
Turn the in-game V/B tile reports (ff4_cell.txt) into a summary of the world
map's animated tiles.

  python3 tools/ff4_world_anim_report.py [log]      (default data/world/tile_report_raw.txt)

Prints, per world, the composed tiles that are fully animated (all four
sub-tiles in the 0x80..0x9f + 0xac/0xad band) and the coastline/lava-edge ones
(1..3 sub-tiles), plus the exact band members seen.  This is the data the
world-map animation needs; the mechanism itself is the tilemap rotation in
field/field.asm (UpdateWaterAnim @8fa1 / UpdateLavaAnim @8ee3).
"""
import re
import sys
from collections import defaultdict

BAND = set(range(0x80, 0xA0)) | {0xAC, 0xAD}
WORLD = {0: "overworld (water + foam)", 1: "underground (lava)",
         2: "moon (no seas)"}


def parse(path):
    out = []
    cur = None
    for line in open(path):
        m = re.search(r"\[wtile\] world=(\d) \((\w+)\) at \((\d+),(\d+)\) "
                      r"composed=\d+ \(0x(\w+)\)", line)
        if m:
            cur = {"w": int(m.group(1)), "x": int(m.group(3)),
                   "y": int(m.group(4)), "c": int(m.group(5), 16),
                   "base": [], "anim": None}
            out.append(cur)
            continue
        m2 = re.search(r"base=([^ ]*) water=(\d)/4", line)
        if m2 and cur is not None:
            cur["anim"] = int(m2.group(2))
            cur["base"] = [int(t, 16) for t in
                           re.findall(r"0x(\w\w)", m2.group(1))]
    return out


def main(argv):
    path = argv[0] if argv else "data/world/tile_report_raw.txt"
    cells = parse(path)
    print(f"{len(cells)} reported cell(s) from {path}")
    per = defaultdict(list)
    for c in cells:
        per[c["w"]].append(c)
    for w in sorted(per):
        rows = per[w]
        full = sorted({c["c"] for c in rows if c["anim"] == 4})
        part = sorted({c["c"] for c in rows if 0 < c["anim"] < 4})
        band = sorted({b for c in rows for b in c["base"] if b in BAND})
        print(f"\nworld {w}: {WORLD.get(w, '?')} - {len(rows)} cell(s)")
        print("  fully animated composed tiles: "
              + " ".join(f"0x{c:02x}" for c in full))
        print("  edge/coast composed tiles:     "
              + " ".join(f"0x{c:02x}" for c in part))
        print("  band sub-tiles seen:           "
              + " ".join(f"0x{b:02x}" for b in band))
        if w == 2:
            print("  (moon has no water; reported cells are surface only)")
    print("\nBand = base tiles 0x80..0x9f (animated frames) + 0xac/0xad (foam).")
    print("The animation is a TILEMAP ROTATION, not a graphics upload:")
    print("  field/field.asm UpdateWaterAnim @8fa1 (overworld, every frame),")
    print("  UpdateLavaAnim @8ee3 (underground, even frames only),")
    print("  tables WaterShiftX @90aa / WaterShiftY @909a (16 steps),")
    print("  transfers: TfrWaterGfx @8e67 / TfrLavaGfx @8f34,")
    print("  palette:   UpdateLavaPal @c4cd.")


if __name__ == "__main__":
    main(sys.argv[1:])
