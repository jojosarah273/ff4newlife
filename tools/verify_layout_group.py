#!/usr/bin/env python3
"""
Guard for the underground/moon layout group (the $1701 mechanism).

DecodeSubTilemap @feba: group = ($0fe5 & 1)  (mapProperties.layoutMSB)
                        OR $1701 != 0        (entered from underground/moon)
Group 1 reads its tilemap from SubTilemap2 (pointer word 256+id, base 18/8000)
- the SAME map id renders as a different room "under" the overworld.
The rip's mapLayouts2 == that table; the rip's mapLayouts1 == group 0, which
is emulator-proven (the intro airship scene == map 381, group 0, 386/390
cells of the VRAM tilemap).

Checks
  1. the engine picks the .g1 files automatically when entering from the
     underground (map 7, the Inn, via its world-1 warp) and renders the
     group-1 composite exactly (like tools/verify_submap_view.py, but for
     the g1 files)
  2. forcing group 0 (FF4_LAYOUTG1=0) renders the group-0 composite
  3. the two groups genuinely differ for the test map (a wrong-table decode
     would make them identical)

Usage: python3 tools/verify_layout_group.py
"""
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SUB = ROOT / "data" / "submaps"
EXE = ROOT / "build" / "ff4_world"
VIEW_W, VIEW_H = 256, 224
MAP = 7                      # the Inn, entered from the underground world


def capture(env_extra, out):
    env = dict(os.environ)
    env.update({"FF4_WARPTO": str(MAP), "FF4_ZOOM": "1.0", "FF4_NPCFREEZE": "1",
                "FF4_NOANIM": "1", "FF4_SHOT": str(out), "FF4_SHOT_DELAY": "25"})
    env.update(env_extra)
    if "cx" in env_extra:
        env["FF4_MAPX"] = str(env_extra.pop("cx"))
        env["FF4_MAPY"] = str(env_extra.pop("cy"))
    env.pop("cx", None)
    env.pop("cy", None)
    subprocess.run(["timeout", "60", str(EXE)], env=env, capture_output=True,
                   timeout=90)
    im = Image.open(out).convert("RGB")
    ox, oy = (im.width - VIEW_W) // 2, (im.height - VIEW_H) // 2
    return im.crop((ox, oy, ox + VIEW_W, oy + VIEW_H))


def expected(suffix, cx, cy):
    png = Image.open(SUB / f"{MAP}{suffix}.png").convert("RGB")
    bx0, by0, bx1, by1 = content_box(png)
    bw, bh = bx1 - bx0, by1 - by0
    ax, ay = cx * 16, cy * 16
    camx = bx0 - (VIEW_W - bw) // 2 if bw <= VIEW_W else \
        min(max(ax - 112, bx0), bx0 + bw - VIEW_W)
    camy = by0 - (VIEW_H - bh) // 2 if bh <= VIEW_H else \
        min(max(ay - 111, by0), by0 + bh - VIEW_H)
    out = Image.new("RGB", (VIEW_W, VIEW_H), (0, 0, 0))
    sx, sy = max(0, camx), max(0, camy)
    ex, ey = min(png.width, camx + VIEW_W), min(png.height, camy + VIEW_H)
    if ex > sx and ey > sy:
        out.paste(png.crop((sx, sy, ex, ey)), (sx - camx, sy - camy))
    return out, camx, camy


def content_box(img):
    px = img.load()
    x0 = y0 = x1 = y1 = None
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


def sprite_mask(cx, cy, camx, camy):
    pts = [(cx * 16 - camx, cy * 16 - camy)]
    p = SUB / f"{MAP}.npc"
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


def main():
    fails = []
    g1, g0 = Image.open(SUB / f"{MAP}.g1.png"), Image.open(SUB / f"{MAP}.png")
    if list(g1.convert("RGB").getdata()) == list(g0.convert("RGB").getdata()):
        fails.append(f"map {MAP}: group-0 and group-1 composites are identical")

    for tag, extra, suffix in (("auto (underground entry)", {}, ".g1"),
                               ("forced group 0", {"FF4_LAYOUTG1": "0"}, "")):
        wf = (SUB / f"{MAP}{suffix}.walk").read_bytes()
        cx = cy = 16
        for i in range(1024):
            if wf[i]:
                cx, cy = i % 32, i // 32
                break
        extra = dict(extra)
        extra["cx"], extra["cy"] = cx, cy
        got = capture(extra, f"/tmp/lg_{tag.split()[0]}.png")
        want, camx, camy = expected(suffix, cx, cy)
        mask = sprite_mask(cx, cy, camx, camy)
        pg, pw = got.load(), want.load()
        bad = sum(1 for y in range(VIEW_H) for x in range(VIEW_W)
                  if not mask[y][x] and pg[x, y] != pw[x, y])
        print(f"map {MAP} {tag}: engine vs {suffix or 'group-0'} composite "
              f"-> {bad} px differ")
        if bad:
            fails.append(f"{tag}: {bad} px differ from the composite")

    if fails:
        print("\nFAILURES:")
        for f in fails:
            print("  -", f)
        return 1
    print("\nlayout group selection OK (underground entry renders .g1)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
