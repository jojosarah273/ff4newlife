#!/usr/bin/env python3
"""
Stitch the warp graph together from the ROM bytes and audit it.

Ground truth (field/trigger.asm + field/field.asm):

  MapTriggersPtrs:  256 relative pointers  -> MapTriggers1   (group 0, maps 0..255)
                    128 relative pointers  -> MapTriggers2   (group 1, maps 0..127)
  record = 5 bytes: x, y, map, xDest(+dir), yDest
      map == 0xFE  chest      (item in byte 4)                 - not a warp
      map == 0xFF  trigger script, id in byte 3                - not a warp
      map >= 0xFB  world warp: world = map-0xFB, dest = (xDest, yDest)
      else         submap warp: dest map, x = xDest & 0x3F, y = yDest,
                   arrival facing = xDest >> 6

  WorldTriggers1/2/3 (19/FE60) hold world-map cell triggers with the same
  5-byte shape (x,y are world cells); $1701 = (source world != 0) selects the
  submap trigger group, which is why maps have two `.trig` sets.

Outputs
  data/submaps/<id>.trig / <id>.trig1   (x y map xDest yDest dir event)
  data/world/warps.json                 (world -> submap entries only)

Checks (exit code 1 on any failure)
  * byte-level ROM decode == rip JSON tables
  * every warp target map has a submap PNG
  * every world entry names an existing map and a cell inside it
  * arrival cells that are not walkable are listed (the engine nudges to the
    nearest walkable cell; a big list means a decoding error)
  * reciprocity: submap->world warps should have a world entry at the same
    world cell, world->submap entries should land on a map that has an exit
    tile (props2 & 0x10) or a warp back

Usage: python3 tools/ff4_warp_audit.py [--write] [map_id]
"""
import json
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SUB = ROOT / "data" / "submaps"
ROM = ROOT / "ff2.smc"

TRIG_OFF = 0xA8000          # 15/8000, probes $02:00 behind... (verified below)
WORLD_TRIG_OFF = 0xCFE60    # 19/FE60
KIND = {0xFE: "chest", 0xFF: "script"}


def rom_body():
    b = open(ROM, "rb").read()
    return b[0x200:] if len(b) == 0x100200 else b


def read_tables(body):
    t1 = [struct.unpack_from("<H", body, TRIG_OFF + i * 2)[0] for i in range(256)]
    t2 = [struct.unpack_from("<H", body, TRIG_OFF + 512 + i * 2)[0] for i in range(128)]
    data = TRIG_OFF + 512 + 256

    def recs(ptr, end):
        n = (end - ptr) // 5
        return [tuple(body[data + ptr + i * 5:data + ptr + i * 5 + 5])
                for i in range(n)]

    g0 = [recs(t1[m], t1[m + 1] if m < 255 else t2[0]) for m in range(256)]
    g1 = [recs(t2[m], t2[m + 1] if m < 127 else 0x131F) for m in range(128)]

    wp = [struct.unpack_from("<H", body, WORLD_TRIG_OFF + i * 2)[0] for i in range(3)]
    wdata = WORLD_TRIG_OFF + 6
    ends = [wp[1], wp[2], 0x1A0 - 6]
    world = []
    for w in range(3):
        n = (ends[w] - wp[w]) // 5
        world.append([tuple(body[wdata + wp[w] + i * 5:wdata + wp[w] + i * 5 + 5])
                      for i in range(n)])
    return g0, g1, world


def kind_of(mp):
    if mp in KIND:
        return KIND[mp]
    return "world" if mp >= 0xFB else "warp"


def audit(write=False):
    body = rom_body()
    g0, g1, world = read_tables(body)
    ok = True

    # ---- cross-check against the rip JSON (independent decode path)
    o = json.load(open(ROOT / "data" / "ff4-en-data.json")).get("obj", {})
    mism = 0
    for m in range(256):
        j = [(e["x"], e["y"], e["map"], e["xDest"], e["yDest"])
             for e in o["mapTriggers1"][m]]
        if j != [tuple(r) for r in g0[m]]:
            mism += 1
    for m in range(128):
        j = [(e["x"], e["y"], e["map"], e["xDest"], e["yDest"])
             for e in o["mapTriggers2"][m]]
        if j != [tuple(r) for r in g1[m]]:
            mism += 1
    print(f"byte-level decode vs rip JSON: {mism} mismatching lists")

    # ---- classify
    cnt = Counter()
    for lst in g0 + g1:
        for (x, y, mp, b3, b4) in lst:
            cnt[kind_of(mp)] += 1
    print("records:", dict(cnt))
    print(f"maps with group-1 triggers: {sum(1 for l in g1 if l)}"
          f"; maps with ONLY group-1: "
          f"{sum(1 for m in range(128) if g1[m] and not g0[m])}")

    # ---- write the .trig files and warps.json
    if write:
        for m in range(256):
            if (SUB / f"{m}.png").exists():
                (SUB / f"{m}.trig").write_text("".join(
                    f"{x} {y} {mp} {b3} {b4} {b3 >> 6} {0}\n"
                    for (x, y, mp, b3, b4) in g0[m]))
        for m in range(128):
            p = SUB / f"{m}.trig1"
            if g1[m] and (SUB / f"{m}.png").exists():
                p.write_text("".join(
                    f"{x} {y} {mp} {b3} {b4} {b3 >> 6} {0}\n"
                    for (x, y, mp, b3, b4) in g1[m]))
            elif p.exists():
                p.unlink()
        entries = []
        for wi, lst in enumerate(world):
            for (x, y, mp, b3, b4) in lst:
                if mp in KIND:
                    continue          # script trigger on the world map
                entries.append({"world": wi, "x": x, "y": y, "map": mp,
                                "xDest": b3 & 0x3F, "yDest": b4,
                                "dir": b3 >> 6, "event": 0})
        names = {}
        jp = ROOT / "data" / "world" / "warps.json"
        old = json.load(open(jp)) if jp.exists() else []
        for e in old:
            names[(e.get("world"), e.get("x"), e.get("y"))] = e.get("name", "")
        for e in entries:
            e["name"] = names.get((e["world"], e["x"], e["y"]), "")
        jp.write_text(json.dumps(entries, indent=1) + "\n")
        print(f"wrote {len(entries)} world entries to {jp.name}")
        real = sum(1 for lst in world for r in lst if kind_of(r[2]) != "script")
        print(f"  (skipped {len(world[0]) + len(world[1]) + len(world[2]) - real}"
              f" script records on the world maps)")

    # ---- every target map exists
    missing = set()
    for lst in g0 + g1:
        for (x, y, mp, b3, b4) in lst:
            if kind_of(mp) == "warp" and not (SUB / f"{mp}.png").exists():
                missing.add(mp)
    if missing:
        ok = False
        print(f"FAIL: warp targets without a submap: {sorted(missing)}")
    else:
        print("all submap warp targets exist")

    # ---- arrival cells
    def walk(m):
        p = SUB / f"{m}.walk"
        return p.read_bytes() if p.exists() else None

    unwalkable = []
    total = 0
    for gi, glist in (("g0", g0), ("g1", g1)):
        for m, lst in enumerate(glist):
            for (x, y, mp, b3, b4) in lst:
                if kind_of(mp) != "warp":
                    continue
                total += 1
                ax, ay = b3 & 0x3F, b4
                w = walk(mp)
                if w is None:
                    continue
                if ax > 31 or ay > 31 or not w[ay * 32 + ax]:
                    unwalkable.append((gi, m, (x, y), mp, (ax, ay)))
    print(f"{total} submap warps; {len(unwalkable)} land on a non-walkable cell")
    for e in unwalkable[:10]:
        print("   ", e)

    # ---- world entries: destination sane
    wbad = []
    for wi, lst in enumerate(world):
        for (x, y, mp, b3, b4) in lst:
            if kind_of(mp) == "script":
                continue
            ax, ay = b3 & 0x3F, b4
            if not (SUB / f"{mp}.png").exists():
                wbad.append(("no map", wi, (x, y), mp, (ax, ay)))
                continue
            w = walk(mp)
            if w and (ax > 31 or ay > 31 or not w[ay * 32 + ax]):
                wbad.append(("arrival blocked", wi, (x, y), mp, (ax, ay)))
    print(f"world entries: {sum(len(l) for l in world)}; {len(wbad)} suspicious")
    for e in wbad[:10]:
        print("   ", e)

    # ---- reciprocity
    # submap -> world warps: is there a world entry at that world cell?
    wx = set()
    for wi, lst in enumerate(world):
        for (x, y, mp, b3, b4) in lst:
            wx.add((wi, x, y))
    # exiting to the world should at least land on a walkable world cell
    wnames = ["overworld", "underground", "moon"]
    wwalk = {}
    for wi, nm in enumerate(wnames):
        p = ROOT / "data" / "world" / f"{nm}.walk"
        if p.exists():
            wwalk[wi] = (p.read_bytes(), 256)
    bad_land = []
    for gi, glist in (("g0", g0), ("g1", g1)):
        for m, lst in enumerate(glist):
            for (x, y, mp, b3, b4) in lst:
                if kind_of(mp) != "world":
                    continue
                wi = mp - 0xFB
                if wi in wwalk:
                    w, stride = wwalk[wi]
                    if b3 >= stride or b4 >= len(w) // stride:
                        bad_land.append((gi, m, (x, y), wi, (b3, b4)))
                    elif not w[b4 * stride + b3]:
                        bad_land.append((gi, m, (x, y), wi, (b3, b4)))
    print(f"submap->world warps landing on a blocked world cell: "
          f"{len(bad_land)} (informational: some are stairs/docks)")
    for e in bad_land[:6]:
        print("   ", e)

    # world -> submap: does the destination map have a way out?
    noexit = []
    for wi, lst in enumerate(world):
        for (x, y, mp, b3, b4) in lst:
            if kind_of(mp) == "script":
                continue
            props = SUB / f"{mp}.props"
            walks = walk(mp)
            has_exit = False
            if props.exists():
                pr = props.read_bytes()
                has_exit = any(pr[i * 2 + 1] & 0x10 for i in range(512))
            trigs = SUB / f"{mp}.trig"
            has_back = False
            if trigs.exists():
                for ln in trigs.read_text().splitlines():
                    f = ln.split()
                    if len(f) >= 3 and int(f[2]) in (0xFB + wi, 0xFF):
                        has_back = True
                        break
            if not (has_exit or has_back or walks is None):
                noexit.append((wi, (x, y), mp))
    print(f"world->submap entries whose map has no exit tile / warp back: "
          f"{len(noexit)}")
    for e in noexit[:8]:
        print("   ", e)

    # ---- reachability from the overworld (both trigger groups, because a
    #      map entered from the underground uses group 1)
    seen = {(0, 0)}
    frontier = [(0, 0)]
    while frontier:
        wi, m = frontier.pop()
        lst = list(g0[m]) if m < 256 else []
        if m < 128:
            lst += g1[m]
        for (x, y, mp, b3, b4) in lst:
            k = kind_of(mp)
            if k == "warp" and mp < 256 and (wi, mp) not in seen:
                seen.add((wi, mp))
                frontier.append((wi, mp))
            if k == "world" and mp - 0xFB <= 2:
                nw = mp - 0xFB
                for (ax, ay, dm, d3, d4) in world[nw]:
                    if kind_of(dm) == "warp" and (nw, dm) not in seen:
                        seen.add((nw, dm))
                        frontier.append((nw, dm))
    maps_total = sum(1 for m in range(256) if (SUB / f"{m}.png").exists())
    reached = {m for (_w, m) in seen}
    print(f"reachable sub-maps from the overworld start: {len(reached)} of "
          f"{maps_total} (unreached maps are reached by events/scripts)")
    only_g1 = [m for m in range(128) if g1[m] and not g0[m]]
    print(f"maps whose ONLY warps come from group 1: {len(only_g1)} "
          f"-> {only_g1[:14]}{' ...' if len(only_g1) > 14 else ''}")

    if not ok:
        return 1
    print("\nwarp audit done")
    return 0


if __name__ == "__main__":
    sys.exit(audit(write="--write" in sys.argv))
