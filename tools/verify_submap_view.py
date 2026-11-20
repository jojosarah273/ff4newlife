#!/usr/bin/env python3
"""
Guard: a sub-map must be drawn exactly like its exported PNG, centred when it
is smaller than the viewport, with black around it - and with NO duplicated
tiles on top (the overhang/anim overlays must be clipped like the map).

Why: the V/overlays are separate textures.  When the camera went negative to
centre a small room, the overlays were still blitted with the unclipped
rectangle and slid relative to the map, which read as "duplicate tiles overlaid
over the core tiles" in Baron's shops and houses.

The comparison ignores 16x16 cells that hold an NPC or the party (those are
sprites, drawn on top by design), so any remaining difference is a map bug.

Usage: python3 tools/verify_submap_view.py [map ...]
"""
import os
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SUB = ROOT / "data" / "submaps"
EXE = ROOT / "build" / "ff4_world"
VIEW_W, VIEW_H = 256, 224


def content_box(img):
    x0, y0, x1, y1 = None, None, None, None
    px = img.load()
    for y in range(img.height):
        for x in range(img.width):
            if px[x, y] == (0, 0, 0):
                continue
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


def expected_view(mid, cx, cy, expanded=False, zoom=1.0):
    png = Image.open(SUB / f"{mid}.png").convert("RGB")
    bx0, by0, bx1, by1 = content_box(png)
    bw, bh = bx1 - bx0, by1 - by0
    vw, vh = VIEW_W, VIEW_H
    ax, ay = cx * 16, cy * 16
    if bw <= vw:
        camx = bx0 - (vw - bw) // 2
    else:
        camx = min(max(ax + 8 - (vw // 2 - 8), bx0), bx0 + bw - vw)
    if bh <= vh:
        camy = by0 - (vh - bh) // 2
    else:
        camy = min(max(ay + 8 - (vh // 2 + 7), by0), by0 + bh - vh)
    out = Image.new("RGB", (vw, vh), (0, 0, 0))
    sx = max(0, camx)
    sy = max(0, camy)
    ex = min(png.width, camx + vw)
    ey = min(png.height, camy + vh)
    if ex > sx and ey > sy:
        out.paste(png.crop((sx, sy, ex, ey)), (sx - camx, sy - camy))
    return out, camx, camy


def sprite_mask(mid, cx, cy, camx, camy):
    """pixels covered by an NPC or the party (sprites sit on top, and the OAM
    tables shift their rows by 1, so pad by 2 px)"""
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


def run(mid, cx, cy, out, expanded=False):
    env = dict(os.environ)
    env.update({"FF4_WARPTO": str(mid), "FF4_MAPX": str(cx), "FF4_MAPY": str(cy),
                "FF4_ZOOM": "1.0", "FF4_NPCFREEZE": "1", "FF4_SHOT": str(out),
                "FF4_SHOT_DELAY": "25", "FF4_NOANIM": "1",
                "FF4_LAYOUTG1": "0"})   # group selection has its own guard
    if expanded:
        env["FF4_EXPANDED"] = "1"
    subprocess.run(["timeout", "60", str(EXE)], env=env, capture_output=True,
                   timeout=90)
    return Path(out).exists()


def main(argv):
    maps = [int(a) for a in argv] or [8, 9, 69, 162]
    fails = 0
    for mid in maps:
        # use walkable cells: the engine nudges the party to the nearest
        # walkable cell, which would invalidate the sprite mask
        cells = []
        wp = SUB / f"{mid}.walk"
        if wp.exists():
            w = wp.read_bytes()
            for i in range(32 * 32):
                if w[i] and i % 32 < 30 and i // 32 < 30:
                    cells.append((i % 32, i // 32))
                    if len(cells) == 2:
                        break
        if not cells:
            cells = [(5, 5)]
        for cx, cy in cells:
            out = f"/tmp/subview_{mid}_{cx}_{cy}.png"
            if not run(mid, cx, cy, out):
                print(f"map {mid}: capture failed")
                fails += 1
                continue
            im = Image.open(out).convert("RGB")
            ox, oy = (im.width - VIEW_W) // 2, (im.height - VIEW_H) // 2
            got = im.crop((ox, oy, ox + VIEW_W, oy + VIEW_H))
            want, camx, camy = expected_view(mid, cx, cy)
            skip = sprite_mask(mid, cx, cy, camx, camy)
            px_g, px_w = got.load(), want.load()
            bad = 0
            first = []
            for y in range(VIEW_H):
                for x in range(VIEW_W):
                    if skip[y][x]:
                        continue
                    if px_g[x, y] != px_w[x, y]:
                        bad += 1
                        if len(first) < 5:
                            first.append((x, y, px_g[x, y], px_w[x, y]))
            status = "OK" if bad == 0 else f"{bad} bad px {first}"
            print(f"map {mid:3d} at ({cx:2d},{cy:2d}): {status}")
            if bad:
                fails += 1
    if fails:
        print(f"\nFAIL: {fails} view(s) differ - duplicated overlay or camera bug")
        return 1
    print("\nsub-map views match their PNG exactly (only sprite cells differ)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
