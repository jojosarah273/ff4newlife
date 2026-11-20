#!/usr/bin/env python3
"""
Guard for the underground lava palette cycle (UpdateLavaPal @c4cd).

Mechanism (field/pal_anim.asm): the underground cycles the 8 colors at
palette group 1, entries 18-25 (CGRAM $0cff), through 4 sets of LavaAnimPal
(ROM 0xA7A16), stepped by LavaPalTbl = 0,1,2,3,3,2,1,0 with
($7a & 0x70) >> 4 - one step every 16 frames, 128 per full cycle.
Validated: set 0 matches the static underground palette within BGR555
rounding.

Checks
  1. pinned steps 0..3 change the view, and only in lava colors
     (every changed pixel's new colour comes from the 32 LavaAnimPal entries)
  2. the cycle is palindromic: step k and step 7-k render identically
  3. the natural cadence: two shots 16 frames apart differ,
     and a full 128-frame period returns to the same image
  4. the overworld is unaffected (same steps, identical shots)

Usage: python3 tools/verify_lava_pal.py
"""
import os
import struct
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "ff4_world"
VIEW_W, VIEW_H = 256, 224


def rgb555(w):
    r = (((w & 0x1F) << 3) | ((w >> 2) & 7))
    g = ((((w >> 5) & 0x1F) << 3) | ((w >> 7) & 7))
    b = ((((w >> 10) & 0x1F) << 3) | ((w >> 12) & 7))
    return (r, g, b)


def shot(world, step, out, delay=25):
    env = dict(os.environ)
    env.update({"FF4_WORLD": str(world), "FF4_MAPX": "99", "FF4_MAPY": "83",
                "FF4_ZOOM": "1.0", "FF4_SHOT": str(out),
                "FF4_SHOT_DELAY": str(delay)})
    if step is not None:
        env["FF4_LAVASTEP"] = str(step)
    subprocess.run(["timeout", "60", str(EXE)], env=env,
                   capture_output=True, timeout=90)
    im = Image.open(out).convert("RGB")
    ox, oy = (im.width - VIEW_W) // 2, (im.height - VIEW_H) // 2
    return im.crop((ox, oy, ox + VIEW_W, oy + VIEW_H))


def main():
    fails = []
    body = open(ROOT / "ff2.smc", "rb").read()
    if len(body) == 0x100200:
        body = body[0x200:]
    words = [struct.unpack_from("<H", body, 0xA7A16 + k * 2)[0]
             for k in range(32)]
    anim_colors = {rgb555(w) for w in words}

    steps = [shot(1, k, f"/tmp/lavachk_{k}.png") for k in range(8)]

    # 1. steps change only in lava colors
    for a in range(3):
        d = sum(1 for p, q in zip(steps[a].getdata(), steps[a + 1].getdata())
                if p != q)
        print(f"underground step {a} vs {a+1}: {d} px differ")
        if d == 0:
            fails.append(f"step {a}->{a+1} changed nothing")
        pa, pb = steps[a].load(), steps[a + 1].load()
        outside = [(x, y) for y in range(VIEW_H) for x in range(VIEW_W)
                   if pa[x, y] != pb[x, y] and pb[x, y] not in anim_colors]
        if outside:
            fails.append(f"step {a+1}: {len(outside)} px changed to "
                          f"non-lava colours (e.g. {outside[:3]})")
        else:
            print(f"  all changed colours are LavaAnimPal entries")

    # 2. palindromic table 0,1,2,3,3,2,1,0
    for k in range(4):
        d = sum(1 for p, q in zip(steps[k].getdata(), steps[7 - k].getdata())
                if p != q)
        if d:
            fails.append(f"step {k} vs {7-k}: {d} px differ "
                          f"(LavaPalTbl is palindromic)")
    if not any(f.startswith("step") and "vs" in f and "palindromic" in f
               for f in fails):
        print("cycle is palindromic: step k == step 7-k for k=0..3")

    # 3. natural cadence + full period
    n0 = shot(1, None, "/tmp/lavanat0.png", delay=25)
    n1 = shot(1, None, "/tmp/lavanat1.png", delay=41)     # +16 frames
    d = sum(1 for p, q in zip(n0.getdata(), n1.getdata()) if p != q)
    print(f"natural cadence (+16 frames): {d} px differ")
    if d == 0:
        fails.append("no change over 16 frames (cadence broken)")
    n2 = shot(1, None, "/tmp/lavanat2.png", delay=153)    # +128 frames
    d = sum(1 for p, q in zip(n0.getdata(), n2.getdata()) if p != q)
    print(f"full period (+128 frames): {d} px differ")
    if d > 100:               # NPCs may have moved a pixel or two
        fails.append(f"128 frames did not return to the same palette "
                     f"({d} px differ)")

    # 4. overworld unaffected
    o0 = shot(0, 0, "/tmp/lavaow0.png")
    o1 = shot(0, 3, "/tmp/lavaow3.png")
    d = sum(1 for p, q in zip(o0.getdata(), o1.getdata()) if p != q)
    print(f"overworld with steps 0 vs 3: {d} px differ")
    if d != 0:
        fails.append(f"overworld changed with the lava step ({d} px)")

    if fails:
        print("\nFAILURES:")
        for f in fails:
            print("  -", f)
        return 1
    print("\nlava palette cycle OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
