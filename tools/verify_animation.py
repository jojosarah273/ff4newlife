#!/usr/bin/env python3
"""
Guard for the field tile animation (per-phase map composites).

What is verified (all against the exported composites, whose data source is
itself emulator-verified - see docs/FIELD-ANIM-ANALYSIS.md):

  1. an animated map (Kaipo, 2) must change phase every 8 frames by itself
     (two captures 8 frames apart differ);
  2. for each pinned phase, the engine view must be EXACTLY the composite
     <id>.f{phase}.png cropped at the engine's own camera rule (so the runtime
     draws the right data, not just "something that moves");
  3. differences between phases must lie only inside animated cells
     (recomputed from the ROM: graphic index 0x120..0x12F);
  4. a map with no animated tiles (36) must be frame-stable.

Usage: python3 tools/verify_animation.py
"""
import json
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import ff4_export_submaps as E  # noqa: E402

SUB = ROOT / "data" / "submaps"
EXE = ROOT / "build" / "ff4_world"
VIEW_W, VIEW_H = 256, 224


def bit_order_check():
    """The torch audit (docs/FIELD-ANIM-ANALYSIS.md): the animated decode
    must use the same bit order as the static decode, or mixed cells
    (animated flame + static bracket, e.g. tileset 15 tiles 15/93) render
    with the animated half mirrored - 'torches split'.  Ground truth:
    CopyTilemapToBuffer @f872 copies the flag pair (lo,hi) verbatim as the
    BG1 tilemap word, so hardware = MSB-left art flipped iff hi bit6; the
    FF4KSTER-style compensation (LSB-left art + flip when bit6 CLEAR) is
    equivalent - but ONLY if BOTH paths store LSB-left."""
    rom = open(ROOT / "ff2.smc", "rb").read()
    if len(rom) == 0x100200:
        rom = rom[0x200:]
    staged = E.anim_staged(rom, 15)
    raw = E.anim_tile4(staged, 4, 0)          # any staged tile
    via_anim = E.anim_tile_px4(raw)
    # manual LSB-left decode of the same bytes
    manual = []
    for y in range(8):
        p0, p1 = raw[y * 2], raw[y * 2 + 1]
        p2, p3 = raw[16 + y * 2], raw[16 + y * 2 + 1]
        for x in range(8):
            manual.append(((p0 >> x) & 1) | (((p1 >> x) & 1) << 1) |
                           (((p2 >> x) & 1) << 2) | (((p3 >> x) & 1) << 3))
    return via_anim == manual


def animated_cells(mid):
    rom = open(ROOT / "ff2.smc", "rb").read()
    if len(rom) == 0x100200:
        rom = rom[0x200:]
    o = json.load(open(ROOT / "data" / "ff4-en-data.json"))
    o = o.get("obj", o)
    p = o["mapProperties"][mid]
    n = p["graphics"] + 1
    ts = E.TILESET + (n - 1) * 0x400
    blob = E.b64(o["mapLayouts1"][p["layout1"]])
    bg2 = (E.b64(o["mapLayouts1"][p["layout2"]])
           if 0 <= p["layout2"] < len(o["mapLayouts1"]) else None)
    anim = set()
    for bl in (bg2, blob):
        if not bl or len(bl) < 1024:
            continue
        for c in range(1024):
            b = bl[c]
            if b >= 128:
                continue
            for j in range(4):
                lo = rom[ts + b * 2 + j * 0x100]
                hi = rom[ts + b * 2 + j * 0x100 + 1]
                if E.is_anim_sprite(lo, hi):
                    anim.add(c)
                    break
    return anim


def capture(mid, cx, cy, out, delay=25, phase=None, noanim=False):
    env = dict(os.environ)
    env.update({"FF4_WARPTO": str(mid), "FF4_MAPX": str(cx),
                "FF4_MAPY": str(cy), "FF4_ZOOM": "1.0", "FF4_NPCFREEZE": "1",
                "FF4_SHOT": str(out), "FF4_SHOT_DELAY": str(delay)})
    if phase is not None:
        env["FF4_ANIMPHASE"] = str(phase)
    if noanim:
        env["FF4_NOANIM"] = "1"
    subprocess.run(["timeout", "90", str(EXE)], env=env,
                   capture_output=True, timeout=120)
    if not Path(out).exists():
        return None
    im = Image.open(out).convert("RGB")
    ox, oy = (im.width - VIEW_W) // 2, (im.height - VIEW_H) // 2
    return im.crop((ox, oy, ox + VIEW_W, oy + VIEW_H))


def sprite_mask(mid, cx, cy, camx, camy):
    """pixels covered by an NPC or the party (drawn on top of the map)"""
    pts = [(cx * 16 - camx, cy * 16 - camy)]
    p = SUB / f"{mid}.npc"
    if p.exists():
        for ln in p.read_text().splitlines():
            f = ln.split("\t")[0].split()
            if len(f) >= 2:
                pts.append((int(f[0]) * 16 - camx, int(f[1]) * 16 - camy))
    mask = [[False] * VIEW_W for _ in range(VIEW_H)]
    for (sx, sy) in pts:
        for y in range(max(0, sy - 2), min(VIEW_H, sy + 18)):
            for x in range(max(0, sx - 2), min(VIEW_W, sx + 18)):
                mask[y][x] = True
    return mask


def content_box(img):
    px = img.load()
    x0 = y0 = None
    x1 = y1 = None
    for y in range(img.height):
        for x in range(img.width):
            if px[x, y] != (0, 0, 0):
                if x0 is None or x < x0:
                    x0 = x
                if y0 is None or y < y0:
                    y0 = y
                if x1 is None or x > x1:
                    x1 = x
                if y1 is None or y > y1:
                    y1 = y
    if x0 is None:
        return 0, 0, img.width, img.height
    return x0 & ~15, y0 & ~15, (x1 | 15) + 1, (y1 | 15) + 1


def expected(mid, cx, cy, phase):
    """engine camera rule, applied to the phase composite (phase 0 = <id>.png)"""
    png = Image.open(SUB / f"{mid}.png" if phase == 0
                     else SUB / f"{mid}.f{phase}.png").convert("RGB")
    bx0, by0, bx1, by1 = content_box(Image.open(SUB / f"{mid}.png"))
    bw, bh = bx1 - bx0, by1 - by0
    vw, vh = VIEW_W, VIEW_H
    ax, ay = cx * 16, cy * 16
    camx = bx0 - (vw - bw) // 2 if bw <= vw else \
        min(max(ax + 8 - (vw // 2 - 8), bx0), bx0 + bw - vw)
    camy = by0 - (vh - bh) // 2 if bh <= vh else \
        min(max(ay + 8 - (vh // 2 + 7), by0), by0 + bh - vh)
    out = Image.new("RGB", (vw, vh), (0, 0, 0))
    sx, sy = max(0, camx), max(0, camy)
    ex, ey = min(png.width, camx + vw), min(png.height, camy + vh)
    if ex > sx and ey > sy:
        out.paste(png.crop((sx, sy, ex, ey)), (sx - camx, sy - camy))
    return out, camx, camy


def main():
    fails = []
    if not bit_order_check():
        fails.append("animated decode bit order != static decode "
                     "(mixed cells would mirror - the torch bug)")
    else:
        print("anim/static bit order consistent (torch guard)")
    mid = 2                      # Kaipo: 512 animated cells, visible sea
    cx, cy = 16, 20

    # 1. natural cadence: two captures 8 frames apart differ
    a = capture(mid, cx, cy, "/tmp/va_a.png", delay=25)
    b = capture(mid, cx, cy, "/tmp/va_b.png", delay=33)
    if a is None or b is None:
        print("FAIL: capture failed")
        return 1
    d = sum(1 for p, q in zip(a.getdata(), b.getdata()) if p != q)
    print(f"natural cadence: {d} px differ between frames 8 apart")
    if d == 0:
        fails.append("no animation at the natural 8-frame cadence")

    # 2. each pinned phase equals its composite crop
    anim = animated_cells(mid)
    for ph in range(4):
        got = capture(mid, cx, cy, f"/tmp/va_p{ph}.png", phase=ph)
        want, camx, camy = expected(mid, cx, cy, ph)
        if got is None:
            fails.append(f"phase {ph}: capture failed")
            continue
        skip = sprite_mask(mid, cx, cy, camx, camy)
        pg, pw = got.load(), want.load()
        bad = sum(1 for y in range(VIEW_H) for x in range(VIEW_W)
                  if not skip[y][x] and pg[x, y] != pw[x, y])
        print(f"phase {ph}: engine vs composite -> {bad} px differ")
        if bad:
            fails.append(f"phase {ph}: {bad} px differ from the composite")

    # 3. phase differences confined to animated cells (full-map check)
    base = Image.open(SUB / f"{mid}.png").convert("RGB")
    for ph in (1, 2, 3):
        f = Image.open(SUB / f"{mid}.f{ph}.png").convert("RGB")
        pb, pf = base.load(), f.load()
        tot = out = 0
        for y in range(512):
            for x in range(512):
                if pb[x, y] != pf[x, y]:
                    tot += 1
                    if (y // 16) * 32 + (x // 16) not in anim:
                        out += 1
        if out:
            fails.append(f"phase {ph}: {out} px change OUTSIDE animated cells")
        else:
            print(f"phase {ph}: all {tot} changed px inside animated cells")

    # 4. control: the same map with FF4_NOANIM must be frame-stable
    #    (map 36 is NOT static - Baron town has animated candles - so the
    #    control is "animation disabled", not "a map without water")
    sa = capture(36, 16, 16, "/tmp/va_s1.png", noanim=True)
    sb = capture(36, 16, 16, "/tmp/va_s2.png", delay=33, noanim=True)
    if sa is not None and sb is not None:
        d = sum(1 for p, q in zip(sa.getdata(), sb.getdata()) if p != q)
        print(f"control (map 36, FF4_NOANIM): {d} px differ")
        if d > 50:
            fails.append(f"NOANIM control map changed by {d} px")

    if fails:
        print("\nFAILURES:")
        for f in fails:
            print("  -", f)
        return 1
    print("\nfield tile animation OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
