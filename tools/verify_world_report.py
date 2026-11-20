#!/usr/bin/env python3
"""
Guard for the world-map tile report (the V key).

Runs the viewer's batch hook (FF4_REPORT_CELL) and checks that a known ocean
cell reports the animated water tiles, and a known land cell does not.

The expected numbers come from the ROM, not from the engine:
  overworld composed tile 0x19 = ocean, base tiles 0x80,0x81,0x82,0x83
  (data/world/overworld.tiles / .base, exported by ff4_export_worldmap.py)

Usage: python3 tools/verify_world_report.py
"""
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "ff4_world"


def run(spec):
    env = dict(os.environ)
    env["SDL_VIDEODRIVER"] = "dummy"
    env["FF4_REPORT_CELL"] = spec
    env["FF4_CELLFILE"] = "/tmp/world_report_check.txt"
    r = subprocess.run([str(EXE)], env=env, capture_output=True, text=True,
                       timeout=120)
    return r.stdout + r.stderr


def main():
    if not EXE.exists():
        print("build/ff4_world missing")
        return 1
    if Path("/tmp/world_report_check.txt").exists():
        os.unlink("/tmp/world_report_check.txt")

    fails = []
    out = run("0 128,128 10,10 60,90")

    ocean = re.findall(r"cell=\(128,128\) composed=(\d+) \(0x(\w\w)\).*?"
                       r"base=128\(0x80W\),129\(0x81W\),130\(0x82W\),131\(0x83W\) "
                       r"water=(\d)/4", out)
    if not ocean:
        fails.append("ocean cell (128,128) did not report base 0x80..0x83")
    else:
        c, h, w = ocean[0]
        print(f"ocean cell: composed {c} (0x{h}) base 0x80..0x83 water={w}/4")
        if int(h, 16) != 0x19:
            fails.append(f"ocean composed tile 0x{h}, expected 0x19")
        if w != "4":
            fails.append(f"ocean water count {w}/4, expected 4/4")

    land = re.search(r"cell=\(60,90\) composed=\d+ \(0x(\w\w)\).*?water=(\d)/4", out)
    if not land:
        fails.append("land cell (60,90) not reported")
    else:
        print(f"land cell: composed 0x{land.group(1)} water={land.group(2)}/4")
        if land.group(2) != "0":
            fails.append("land cell was reported as water")

    log = Path("/tmp/world_report_check.txt")
    if not log.exists():
        fails.append("no log file written")
    else:
        lines = log.read_text().strip().splitlines()
        print(f"log: {len(lines)} line(s)")
        blocks = [l for l in lines if l.startswith("[wtile] world=")]
        compact = [l.strip() for l in lines if l.strip().startswith("w")]
        if len(blocks) != 3:
            fails.append(f"log has {len(blocks)} report blocks, expected 3")
        if not blocks or not blocks[0].startswith(
                "[wtile] world=0 (Overworld) at (128,128)"):
            fails.append(f"unexpected first block: {blocks[:1]}")
        if not compact or not compact[0].startswith(
                "w0 128 128 0x19 128 129 130 131 "):
            fails.append(f"bad compact line: {compact[:1]}")

    # replay the recorded V/B session (data/world/water_report_raw.txt) and
    # require the same water classification - real user data as a test vector
    raw = ROOT / "data" / "world" / "water_report_raw.txt"
    if raw.exists():
        import re as _re

        def parse(path):
            out, cur = [], None
            for line in open(path):
                m = _re.search(r"composed=\d+ \(0x(\w+)\)", line)
                if m:
                    cur = m.group(1)
                m2 = _re.search(r"water=(\d)/4", line)
                if m2 and cur:
                    out.append((cur, int(m2.group(1))))
                    cur = None
            return out

        recorded = parse(raw)
        cells = [l for l in open(raw) if l.startswith("[wtile] world=")]
        spec_cells = []
        for line in cells:
            m = _re.search(r"at \((\d+),(\d+)\)", line)
            if m:
                spec_cells.append(f"{m.group(1)},{m.group(2)}")
        if spec_cells and recorded:
            os.unlink("/tmp/world_report_check.txt")
            run("0 " + " ".join(spec_cells))
            replayed = parse("/tmp/world_report_check.txt")
            print(f"replayed the recorded session: {len(replayed)} cells")
            if replayed != recorded:
                diff = [(a, b) for a, b in zip(recorded, replayed) if a != b]
                fails.append(f"replay differs from the record: {diff[:4]}")
            else:
                print("  classification identical to the recorded session")

    # the moon is 64x64: the same call must not crash or wrap
    out2 = run("2 20,20")
    if "world=2 (Moon)" not in out2:
        fails.append("moon world report failed")

    if fails:
        print("\nFAILURES:")
        for f in fails:
            print("  -", f)
        return 1
    print("\nworld tile report OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
