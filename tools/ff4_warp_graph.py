#!/usr/bin/env python3
"""
Stitch together the map of warps and verify it against the ROM-derived tables.

Edges come from two places (both decoded from the ROM by the rip):

  sub-map side : mapTriggers1[0..255] / mapTriggers2[0..127]
                 (field/trigger.asm MapTriggers1/2) - a cell in the map
  world side   : worldTriggers1/2/3 (field/vehicle.asm loaders) - a 2x2 cell
                 block on a world map

Map ids >= 0xFB are world maps (251 = overworld, 252 = underground,
253 = moon; 254/255 appear in the tables but are not one of the three).

Checks
  * every edge target is a field map we have rendered, or a world map
  * every world->submap warp has a submap->world trigger back at its entry cell
  * reports unreachable maps (no incoming edge)

Usage
  python3 tools/ff4_warp_graph.py            # summary + checks
  python3 tools/ff4_warp_graph.py --dot f   # write Graphviz DOT
  python3 tools/ff4_warp_graph.py 0          # details for map 0
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
JSON = ROOT / "data" / "ff4-en-data.json"
SUB = ROOT / "data" / "submaps"

WORLD_KEYS = ["worldTriggers1", "worldTriggers2", "worldTriggers3"]


def load():
    o = json.load(open(JSON)).get("obj", {})
    return o


def sub_edges(o):
    """[(from_map, x, y, to_map, dx, dy, event)] from the cell triggers."""
    t1 = o.get("mapTriggers1") or []
    t2 = o.get("mapTriggers2") or []
    out = []
    for m in range(384):
        lst = t1[m] if m < 256 else (t2[m - 256] if m - 256 < len(t2) else [])
        for e in lst:
            out.append((m, e.get("x", 0), e.get("y", 0), e.get("map", 0),
                        e.get("xDest", 0), e.get("yDest", 0), e.get("event", 0)))
    return out


def world_edges(o):
    """[(world_index, x, y, to_map, dx, dy, event)] from the world warp tables."""
    out = []
    for wi, key in enumerate(WORLD_KEYS):
        for e in (o.get(key) or []):
            out.append((wi, e.get("x", 0), e.get("y", 0), e.get("map", 0),
                        e.get("xDest", 0), e.get("yDest", 0), e.get("event", 0)))
    return out


def main(argv):
    o = load()
    se = sub_edges(o)
    we = world_edges(o)
    have_png = {int(p.stem) for p in SUB.glob("*.png") if p.stem.isdigit()}

    print(f"sub-map cell triggers : {len(se)}")
    print(f"world-map warps       : {len(we)}")
    print(f"field maps rendered   : {len(have_png)}")

    # --- classify the edges ------------------------------------------------
    # Maps 254/255 have no field layout (gfx 0, no layout blob): they are the
    # two spare world slots.  Trigger records whose "map" lands there are not
    # plain warps - they are the event/overlay triggers on the same table
    # (the rip decodes one 7-byte record into x/y/map/dest/effect fields).
    WORLD_OK = (251, 252, 253)
    warps, other, missing = [], [], []
    for e in se:
        m, x, y, t = e[0], e[1], e[2], e[3]
        if t in WORLD_OK:
            warps.append(e)
        elif t in (254, 255):
            other.append(e)
        elif t in have_png:
            warps.append(e)
        else:
            missing.append(e)
    print(f"\ntrigger records classified:")
    print(f"  warps to a rendered map / world : {len(warps)}")
    print(f"  spare-world slots (254/255)     : {len(other)}  (event/overlay "
          f"records, not followed)")
    print(f"  targets with no map at all      : {len(missing)}")
    for b in missing[:8]:
        print(f"    map {b[0]} ({b[1]},{b[2]}) -> map {b[3]}")

    # --- round trip: world warp -> sub map entry has a trigger back --------
    missing_back = []
    for (wi, x, y, t, dx, dy, ev) in we:
        if t >= 0xFB:
            continue
        # the return trigger is usually just outside the entry cell (Baron
        # enters at (16,30) and exits from (16,31)), so allow a 3-cell radius
        back = [e for e in se if e[0] == t and e[3] >= 0xFB
                and abs(e[1] - dx) <= 3 and abs(e[2] - dy) <= 3]
        if not back:
            missing_back.append((wi, x, y, t, dx, dy))
    print(f"\nworld->map warps whose entry cell has no trigger back: "
          f"{len(missing_back)}")
    for b in missing_back[:10]:
        print(f"  world {b[0]} ({b[1]},{b[2]}) -> map {b[3]} at ({b[4]},{b[5]})")

    # --- reachability from the Baron start --------------------------------
    start = 0
    adj = {}
    for (m, x, y, t, dx, dy, ev) in warps:
        adj.setdefault(m, set()).add(t)
    seen, frontier = {start}, [start]
    while frontier:
        m = frontier.pop()
        for t in adj.get(m, ()):
            if t >= 0xFB or t not in have_png:
                continue
            if t not in seen:
                seen.add(t)
                frontier.append(t)
    print(f"\nfield maps reachable from map {start}: {len(seen)} / {len(have_png)}")

    if len(argv) > 1 and argv[0] == "--dot":
        out = Path(argv[1])
        lines = ["digraph warps {", "  rankdir=LR;"]
        for (m, x, y, t, dx, dy, ev) in se:
            lines.append(f'  "{m}" -> "{t}" [label="{x},{y}"];')
        for (wi, x, y, t, dx, dy, ev) in we:
            lines.append(f'  "W{wi}" -> "{t}" [label="{x},{y}"];')
        lines.append("}")
        out.write_text("\n".join(lines))
        print(f"wrote {out}")
        return 0

    if len(argv) > 0 and argv[0].isdigit():
        want = int(argv[0])
        print(f"\nmap {want}:")
        for (m, x, y, t, dx, dy, ev) in se:
            if m == want:
                print(f"  ({x:2d},{y:2d}) -> map {t:3d} enter ({dx},{dy}) event {ev}")
        for (wi, x, y, t, dx, dy, ev) in we:
            if t == want:
                print(f"  in : world {wi} ({x},{y}) -> here at ({dx},{dy})")

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
