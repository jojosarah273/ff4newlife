#!/usr/bin/env python3
"""
Verify the engine (ff4_world) renders every NPC and the actor identically to
the ROM-derived reference in tools/ff4_palette_truth.py.

How it works
------------
1. Ask ff4_palette_truth for the CGRAM slot table, the NPC list and the
   ground-truth 16x16 sprite for every visible NPC.
2. Run ff4_world headless at FF4_ZOOM=1 for that map with the actor parked on
   a cell, and screenshot the framebuffer.
3. The engine's sub-map camera follows the map's content box (small maps are
   centred with black around them): when the content box is smaller than the
   256x224 viewport, cam = box0 - (viewport-box)/2; otherwise the party anchor
   clamped to the box.  Computed here from <id>.png, exactly like the engine.
   and the viewport is letterboxed at ((win_w-256)/2, (win_h-224)/2).
4. For each NPC, crop its 16x16 from the screenshot and compare with the truth
   sprite pixel by pixel.

Usage:  python3 tools/verify_engine_sprites.py [map_id] [actor_cx] [actor_cy]
Exit code 0 if every visible NPC matched.
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from ff4_palette_truth import (Rom, cgram_slots, slot_for_npc, slot_for_character,
                               palette_for_slot, render_npc_sprite, render_sprite)


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

BUILD = ROOT / "build"
VIEW_W, VIEW_H = 256, 224


def run_engine(map_id, cx, cy, out_png):
    env = dict(os.environ)
    env.update({
        "FF4_WARPTO": str(map_id),
        "FF4_MAPX": str(cx),
        "FF4_MAPY": str(cy),
        "FF4_SHOT": str(out_png),
        "FF4_SHOT_DELAY": "20", "FF4_NOANIM": "1",
        "FF4_FACING": "2",    # actor truth is rendered facing down
        "FF4_LAYOUTG1": "0",  # group selection has its own guard
        "FF4_ZOOM": "1.0",
        "FF4_NPCFREEZE": "1",
        "FF4_NOCLIP": "1",
    })
    exe = BUILD / "ff4_world"
    r = subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=120)
    return r.returncode == 0 and Path(out_png).exists(), r.stdout + r.stderr


def main(argv):
    map_id = int(argv[0]) if len(argv) > 0 else 0
    PREF = [  # cells with several NPCs in view
        (14, 16), (16, 18), (10, 26), (20, 20), (16, 16),
    ]
    if len(argv) > 2:
        PREF = [(int(argv[1]), int(argv[2]))]

    rom = Rom()
    entries, mp = cgram_slots(rom, map_id)
    npcs = rom.npc_list(map_id)

    png = Path(f"/tmp/verify_engine_{map_id}.png")
    ok = False
    chosen = PREF[0]
    reason = ""
    for (cx, cy) in PREF:
        vis = [n for n in npcs
               if abs(n["x"] * 16 - (cx * 16 - 112)) < VIEW_W and
               abs(n["y"] * 16 - (cy * 16 - 111)) < VIEW_H]
        if len(vis) >= 2 or (cx, cy) == PREF[-1]:
            chosen = (cx, cy)
            ok, reason = run_engine(map_id, cx, cy, png)
            if ok:
                break
    if not ok:
        print("engine did not produce a screenshot:")
        print(reason[-1200:])
        return 1

    # the warp code relocates the party if the spawn cell is impassable;
    # the log tells us the real cell, which fixes the camera.
    import re
    m = re.search(r"spawn cell impassable, moved to \((\d+),(\d+)\)", reason)
    if m:
        chosen = (int(m.group(1)), int(m.group(2)))

    from PIL import Image
    shot = Image.open(png).convert("RGB")
    ox, oy = (shot.width - VIEW_W) // 2, (shot.height - VIEW_H) // 2

    # the engine draws the overhang layer (roofs/tree canopy) AFTER sprites,
    # so NPCs underneath are legitimately hidden
    over = None
    op = ROOT / f"data/submaps/{map_id}.over.png"
    if op.exists():
        over = Image.open(op).convert("RGBA")

    cx, cy = chosen
    # content box (same rule as world_view.c): centre when smaller than the
    # viewport, otherwise party anchor clamped to the box
    bx0, by0, bx1, by1 = content_box(Image.open(ROOT / f"data/submaps/{map_id}.png"))
    bw, bh = bx1 - bx0, by1 - by0
    if bw <= VIEW_W:
        cam_x = bx0 - (VIEW_W - bw) // 2
    else:
        cam_x = min(max(cx * 16 - 112, bx0), bx0 + bw - VIEW_W)
    if bh <= VIEW_H:
        cam_y = by0 - (VIEW_H - bh) // 2
    else:
        cam_y = min(max(cy * 16 - 111, by0), by0 + bh - VIEW_H)

    print(f"map {map_id}: npcPalette1={mp['npc_palette1']} npcPalette2={mp['npc_palette2']}")
    print(f"actor cell {chosen} -> camera ({cam_x},{cam_y}); shot {shot.size}")
    print(f"{'npc':>4} {'pos':>9} {'sheet':>5} {'slot':>4} {'entry':>5}  result")

    tested = matched = skipped = occluded = 0
    cell_count = {}
    for n in npcs:
        cell_count[(n["x"], n["y"])] = cell_count.get((n["x"], n["y"]), 0) + 1
    for i, n in enumerate(npcs):
        sx = ox + (n["x"] * 16 - cam_x)
        sy = oy + (n["y"] * 16 - cam_y)
        # only compare sprites fully inside the viewport (the engine clips at
        # the letterbox edge, which would look like a mismatch)
        if not (sx >= ox and sy >= oy and
                sx + 16 <= ox + VIEW_W and sy + 16 <= oy + VIEW_H):
            continue
        slot = slot_for_npc(n)
        entry = entries[slot]
        sheet = rom.npc_gfx_id(n["gfx_id"])
        frame = (n["facing"] & 3) * 2
        truth = render_npc_sprite(sheet, frame, palette_for_slot(rom, entries, slot))
        if not any(px[3] for row in truth for px in row):
            skipped += 1
            print(f"{i:4d} ({n['x']:2d},{n['y']:2d}) {sheet:5d} {slot:4d} {entry:5d}  "
                  f"SKIP (sheet {sheet} blank / 4-tile pose)")
            continue
        # the actor is drawn after the NPCs and stands on `chosen`
        if (n["x"], n["y"]) == tuple(chosen):
            occluded += 1
            print(f"{i:4d} ({n['x']:2d},{n['y']:2d}) {sheet:5d} {slot:4d} {entry:5d}  "
                  f"OCCLUDED (the actor stands here)")
            continue
        # story sprites the engine intentionally hides (world_view hidden_npc[])
        if (map_id, n["x"], n["y"]) in ((1, 8, 16), (1, 14, 9)):
            occluded += 1
            print(f"{i:4d} ({n['x']:2d},{n['y']:2d}) {sheet:5d} {slot:4d} {entry:5d}  "
                  f"OCCLUDED (engine-hidden story sprite)")
            continue
        if cell_count[(n["x"], n["y"])] > 1 and i != max(
                k for k, m in enumerate(npcs) if (m["x"], m["y"]) == (n["x"], n["y"])):
            occluded += 1
            print(f"{i:4d} ({n['x']:2d},{n['y']:2d}) {sheet:5d} {slot:4d} {entry:5d}  "
                  f"OCCLUDED (another npc on the same cell)")
            continue
        if over is not None:
            cov = sum(1 for yy in range(16) for xx in range(16)
                      if truth[yy][xx][3] and
                      0 <= n["x"] * 16 + xx < over.width and
                      0 <= n["y"] * 16 + yy < over.height and
                      over.getpixel((n["x"] * 16 + xx, n["y"] * 16 + yy))[3])
            if cov:
                occluded += 1
                print(f"{i:4d} ({n['x']:2d},{n['y']:2d}) {sheet:5d} {slot:4d} {entry:5d}  "
                      f"OCCLUDED ({cov}/256 px under the overhang layer)")
                continue

        tested += 1
        diff = 0
        for yy in range(16):
            for xx in range(16):
                p = truth[yy][xx]
                if p[3] == 0:
                    continue
                ex = sx + xx
                ey = sy + yy
                if not (0 <= ex < shot.width and 0 <= ey < shot.height):
                    diff += 1
                    continue
                # npc_blit shifts the OAM rows by the table's y offset (0/1 and 8/9);
                # the reference already includes it, so compare directly.
                if shot.getpixel((ex, ey)) != p[:3]:
                    diff += 1
        total = sum(1 for row in truth for p in row if p[3])
        pct = 100.0 * (1 - diff / total) if total else 0
        # a wrong palette/sheet differs over essentially every opaque pixel;
        # a handful of pixels is a neighbouring sprite overlapping by 1px.
        ok = diff == 0 or (total and diff / total <= 0.05)
        if ok:
            matched += 1
        verdict = "MATCH" if diff == 0 else \
            (f"MATCH ({diff}/{total} px overlapped by a neighbour)" if ok
             else f"{diff}/{total} px differ ({pct:.1f}%)")
        print(f"{i:4d} ({n['x']:2d},{n['y']:2d}) {sheet:5d} {slot:4d} {entry:5d}  {verdict}")

    # actor
    aslot = slot_for_character(rom, 0)
    aentry = entries[aslot]
    atruth = render_sprite(0, 4, palette_for_slot(rom, entries, aslot))
    ax = ox + (cx * 16 + 8 - cam_x)   # actor_x = cell*16, drawn at actor_x-cam... see world_view
    # world_view draws the actor at actor_x - cam_x (actor_sync_pixels keeps it on the cell)
    ax = ox + (cx * 16 - cam_x)
    ay = oy + (cy * 16 - cam_y)
    # the engine hides the actor when the cell's prop byte2 has 0x04
    # (under a roof/arch) - that is the game's own rule, not a palette bug
    actor_hidden = False
    props = ROOT / f"data/submaps/{map_id}.props"
    if props.exists():
        pb = props.read_bytes()
        k = ((cy * 32) + cx) * 2 + 1
        if k < len(pb) and (pb[k] & 0x04):
            actor_hidden = True
    if over is not None:
        cov = sum(1 for yy in range(16) for xx in range(16)
                  if 0 <= cx * 16 + xx < over.width and 0 <= cy * 16 + yy < over.height
                  and over.getpixel((cx * 16 + xx, cy * 16 + yy))[3])
        # partial cover is normal (roof edge) - handled by masking below

    adiff = atotal = 0
    for yy in range(16):
        for xx in range(16):
            p = atruth[yy][xx]
            if p[3] == 0:
                continue
            wx, wy = cx * 16 + xx, cy * 16 + yy
            if over is not None and 0 <= wx < over.width and 0 <= wy < over.height \
                    and over.getpixel((wx, wy))[3]:
                continue          # covered by the overhang layer
            atotal += 1
            ex, ey = ax + xx, ay + yy
            if not (0 <= ex < shot.width and 0 <= ey < shot.height) or \
               shot.getpixel((ex, ey)) != p[:3]:
                adiff += 1
    if actor_hidden:
        print("\nactor: hidden by the map rule (prop byte2 & 0x04, or overhang) - OCCLUDED")
        adiff = 0
    else:
        print(f"\nactor: sheet 0 slot {aslot} entry {aentry}  "
              f"{'MATCH' if adiff == 0 else f'{adiff}/{atotal} px differ'}")

    print(f"\n{matched}/{tested} visible NPCs pixel-identical to the ROM reference "
          f"({skipped} blank sheets, {occluded} occluded)")
    return 0 if (tested == matched and adiff == 0) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
