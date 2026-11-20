#!/usr/bin/env python3
"""
Sweep every field map and verify the engine against the ROM reference.

For each map: run ff4_world headless, compare every fully-visible NPC sprite
(and the actor) pixel-for-pixel with tools/ff4_palette_truth.py.

  python3 tools/verify_all_maps.py            # all 384 maps
  python3 tools/verify_all_maps.py 0 40       # range
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main(argv):
    lo = int(argv[0]) if len(argv) > 0 else 0
    hi = int(argv[1]) if len(argv) > 1 else 383
    maps = [m for m in range(lo, hi + 1) if (ROOT / f"data/submaps/{m}.png").exists()]

    tot_npc = tot_ok = tot_skip = tot_occ = 0
    bad_maps = []
    actor_bad_maps = []
    actor_ok = actor_bad = actor_occ = 0

    for i, m in enumerate(maps):
        r = subprocess.run([sys.executable, str(ROOT / "tools/verify_engine_sprites.py"), str(m)],
                           capture_output=True, text=True)
        out = r.stdout
        mm = re.search(r"(\d+)/(\d+) visible NPCs pixel-identical.*?\((\d+) blank sheets, (\d+) occluded\)", out)
        if not mm:
            bad_maps.append((m, "no result"))
            continue
        ok, tested, skip, occ = map(int, mm.groups())
        tot_npc += tested
        tot_ok += ok
        tot_skip += skip
        tot_occ += occ
        if "actor: hidden by the map rule" in out:
            actor_occ += 1
        else:
            am = re.search(r"actor: sheet 0 slot \d+ entry \d+\s+(MATCH|\d+/\d+ px differ)", out)
            if am and am.group(1).startswith("MATCH"):
                actor_ok += 1
            else:
                actor_bad += 1
        if ok != tested:
            bad_maps.append((m, out.strip().splitlines()[-1]))
        am2 = re.search(r"actor: sheet 0 slot \d+ entry \d+\s+(MATCH|\d+/\d+ px differ)", out)
        if am2 and not am2.group(1).startswith("MATCH"):
            actor_bad_maps.append((m, am2.group(1)))

    print(f"maps checked:                 {len(maps)}")
    if actor_bad_maps:
        print("actor mismatching maps:")
        for m, why in actor_bad_maps[:40]:
            print(f"  map {m}: {why}")
    print(f"NPC sprites verified:         {tot_npc}")
    print(f"  pixel-identical:            {tot_ok}"
          + (f"  ({100.0*tot_ok/tot_npc:.2f}%)" if tot_npc else ""))
    print(f"  mismatched:                 {tot_npc - tot_ok}")
    print(f"  skipped (blank sheet):      {tot_skip}")
    print(f"  occluded (actor/overhang):  {tot_occ}")
    print(f"actor pixel-identical maps:   {actor_ok}"
          f"  (+{actor_occ} hidden by the map's own roof/arch rule, {actor_bad} mismatched)")
    if bad_maps:
        print("\nmismatching maps:")
        for m, why in bad_maps:
            print(f"  map {m}: {why}")
    return 0 if not bad_maps else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
