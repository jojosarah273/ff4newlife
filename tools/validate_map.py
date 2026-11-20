#!/usr/bin/env python3
"""validate_map.py - capture a map from the running game and diff it against
our export, automatically.

For each map:
  1. patch retail ff2.smc so it boots straight onto that map (make_map_rom)
  2. run it in the libretro harness for ~1200 frames, dumping a frame every 24
  3. diff our data/submaps/<id>.png against the last frame, using the other
     tail frames as the animated-pixel mask (compare_map --mask-frame)
  4. print one JSON line per map

usage:
    python3 tools/validate_map.py 0,1,2,3 [--spawn 16,16] [--radius 24]
                                    [--frames 1200] [--every 24] [--keep]
"""
import json
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_map_rom                                    # noqa: E402

CORE = "/usr/lib/x86_64-linux-gnu/libretro/snes9x_libretro.so"
DUMP = os.environ.get("RETRO_DUMP", "/tmp/rd2")
WORK = "/tmp/validate"
EXPECT = (144, 145)          # camera anchor for spawn cell (16,16), verified


def dump_frames(mid, x, y, frames, every, keep):
    d = os.path.join(WORK, str(mid))
    if os.path.isdir(d):
        shutil.rmtree(d)
    os.makedirs(d)
    rom = os.path.join(WORK, "m%d.smc" % mid)
    make_map_rom.make_rom(mid, x, y, rom)
    # press Start at frame 400 so the intro is skipped and the map loads
    subprocess.run([DUMP, CORE, rom, d, str(frames), str(every), "400", "100"],
                   check=True, capture_output=True)
    pngs = sorted(f for f in os.listdir(d) if f.endswith(".png"))
    return d, [os.path.join(d, f) for f in pngs]


def validate(mid, spawn, radius, frames, every):
    x, y = spawn
    d, pngs = dump_frames(mid, x, y, frames, every, False)
    if len(pngs) < 3:
        return {"id": mid, "error": "not enough frames (%d)" % len(pngs)}
    ref = pngs[-1]
    masks = pngs[-9:-1]          # eight phases for the animation mask
    cmd = [sys.executable, os.path.join(os.path.dirname(__file__),
                                        "compare_map.py"),
           os.path.join("data", "submaps", "%d.png" % mid), ref,
           # camera formula: origin = 16*(cell-7) [, +1 for y]
           "--expect", "%d,%d" % (EXPECT[0] + (x - 16) * 16,
                                  EXPECT[1] + (y - 16) * 16),
           "--radius", str(radius), "--step", "2", "--json"]
    for m in masks:
        cmd += ["--mask-frame", m]
    out = subprocess.run(cmd, capture_output=True, text=True)
    try:
        res = json.loads(out.stdout.strip().splitlines()[-1])
    except Exception:                                  # noqa: BLE001
        return {"id": mid, "error": "compare failed: %s%s"
                % (out.stdout[-200:], out.stderr[-200:])}
    res["id"] = mid
    return res


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    ids = [int(v, 0) for v in args[0].split(",")]
    spawn = (16, 16)
    radius = 24
    frames = 1200
    every = 24
    i = 1
    while i < len(args):
        a = args[i]
        if a == "--spawn":
            i += 1
            spawn = tuple(int(v) for v in args[i].split(","))
        elif a == "--radius":
            i += 1
            radius = int(args[i])
        elif a == "--frames":
            i += 1
            frames = int(args[i])
        elif a == "--every":
            i += 1
            every = int(args[i])
        else:
            print("unknown option %s" % a)
            return 2
        i += 1

    os.makedirs(WORK, exist_ok=True)
    rows = []
    for mid in ids:
        r = validate(mid, spawn, radius, frames, every)
        rows.append(r)
        if "error" in r:
            print("map %3d  ERROR %s" % (mid, r["error"]))
        else:
            print("map %3d  match %6.2f%%  bg %6.2f%%  offset %s%+d,%+d  "
                  "anim px %d"
                  % (mid, r["match_pct"], r["bg_match_pct"],
                     r["offset"], r["offset_delta"][0],
                     r["offset_delta"][1], r["animated_px"]))
    ok = [r for r in rows if "error" not in r]
    if ok:
        avg = sum(r["match_pct"] for r in ok) / len(ok)
        print("\n%d/%d maps measured, mean match %.2f%%, min %.2f%%"
              % (len(ok), len(rows), avg, min(r["match_pct"] for r in ok)))
    bad = [r for r in ok if r["match_pct"] < 90.0]
    if bad:
        print("BELOW 90%%: %s" % ", ".join(str(r["id"]) for r in bad))
    return 0


if __name__ == "__main__":
    sys.exit(main())
