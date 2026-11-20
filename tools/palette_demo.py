#!/usr/bin/env python3
"""
Self-contained demo you can look at with your own eyes.

For a handful of maps it produces, under /tmp/ff4_palette_demo/:

  rom_<map>.png     the ROM-truth renderer: every NPC + the actor, each drawn
                    with the palette the SNES code puts in CGRAM
  eng_<map>.png     ff4_world's framebuffer for the same map
  cmp_<map>.png     ROM truth on the left, engine on the right
  sprites_<map>.png every visible NPC 4x: ROM truth above, engine below

and prints the pixel-match result per sprite.

Run:  python3 tools/palette_demo.py [map ...]
"""
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
OUT = Path("/tmp/ff4_palette_demo")

from ff4_palette_truth import (Rom, cgram_slots, slot_for_npc, slot_for_character,
                               palette_for_slot, render_npc_sprite, render_sprite,
                               NPC_SPRITE_TBL, PLAYER_SPRITE_TILES)

VIEW_W, VIEW_H = 256, 224


def capture(map_id, cx, cy, png, zoom=2):
    env = dict(os.environ)
    env.update({"FF4_WARPTO": str(map_id), "FF4_MAPX": str(cx), "FF4_MAPY": str(cy),
                "FF4_SHOT": str(png), "FF4_SHOT_DELAY": "8",
                "FF4_ZOOM": str(zoom), "FF4_NPCFREEZE": "1", "FF4_NOCLIP": "1"})
    r = subprocess.run([str(ROOT / "build" / "ff4_world")], env=env,
                       capture_output=True, text=True, timeout=180)
    return r.stdout


def upscale(img, k):
    return img.resize((img.width * k, img.height * k), 0)


def main(argv):
    from PIL import Image, ImageDraw
    OUT.mkdir(parents=True, exist_ok=True)
    maps = [int(a) for a in argv] or [0, 1, 2, 36, 3, 38]
    rom = Rom()
    summary = []

    for map_id in maps:
        entries, mp = cgram_slots(rom, map_id)
        npcs = rom.npc_list(map_id)
        if not npcs:
            continue

        # choose a cell with several NPCs in view (same preference order as
        # tools/verify_engine_sprites.py so both agree)
        def fully(q, cx, cy):
            camx = max(0, min(512 - VIEW_W, cx * 16 - 112))
            camy = max(0, min(512 - VIEW_H, cy * 16 - 111))
            sx, sy = q["x"] * 16 - camx, q["y"] * 16 - camy
            return 0 <= sx and 0 <= sy and sx + 16 <= VIEW_W and sy + 16 <= VIEW_H
        cand = [(14, 16), (16, 18), (10, 26), (20, 20), (16, 16)]
        cand += [(q["x"], q["y"]) for q in npcs]
        best, bestn = cand[0], -1
        for c in cand:
            n = sum(1 for q in npcs if fully(q, c[0], c[1]))
            if n > bestn:
                bestn, best = n, c
        cx, cy = best

        eng_png = OUT / f"eng_{map_id}.png"
        log = capture(map_id, cx, cy, eng_png, zoom=2)
        m = re.search(r"moved to \((\d+),(\d+)\)", log)
        if m:
            cx, cy = int(m.group(1)), int(m.group(2))
        if not eng_png.exists():
            print(f"map {map_id}: engine capture failed")
            continue
        shot = Image.open(eng_png).convert("RGB")
        zoom = 2
        ox = (shot.width - VIEW_W * zoom) // 2
        oy = (shot.height - VIEW_H * zoom) // 2
        eng = shot.crop((ox, oy, ox + VIEW_W * zoom, oy + VIEW_H * zoom))
        cam_x = max(0, min(512 - VIEW_W, cx * 16 - 112))
        cam_y = max(0, min(512 - VIEW_H, cy * 16 - 111))

        over = None
        op = ROOT / f"data/submaps/{map_id}.over.png"
        if op.exists():
            over = Image.open(op).convert("RGBA")

        # ---- ROM truth contact sheet -------------------------------------
        cell = 16 * 3 + 12
        cols = min(8, max(1, len(npcs)))
        rows = (len(npcs) + cols) // cols + 1
        sheet = Image.new("RGB", (cols * cell, rows * cell), (16, 16, 24))
        dr = ImageDraw.Draw(sheet)

        def blit(fr, px, py, k=3):
            for y in range(16):
                for x in range(16):
                    p = fr[y][x]
                    if p[3]:
                        for dy in range(k):
                            for dx in range(k):
                                sheet.putpixel((px + x * k + dx, py + y * k + dy), p[:3])

        aslot = slot_for_character(rom, 0)
        blit(render_sprite(0, 4, palette_for_slot(rom, entries, aslot)), 0, 0)
        dr.text((1, 50), f"actor e{entries[aslot]}", fill=(255, 255, 128))

        # ---- per-sprite comparison strip --------------------------------
        strip = Image.new("RGB", (max(1, len(npcs)) * 54 + 6, 108), (16, 16, 24))
        ds = ImageDraw.Draw(strip)
        matched = tested = 0

        for i, n in enumerate(npcs):
            slot = slot_for_npc(n)
            cols_pal = palette_for_slot(rom, entries, slot)
            sheetid = rom.npc_gfx_id(n["gfx_id"])
            frame = (n["facing"] & 3) * 2
            truth = render_npc_sprite(sheetid, frame, cols_pal)

            cxx = (i % cols) * cell
            cyy = (i // cols + 1) * cell
            blit(truth, cxx, cyy)
            dr.text((cxx + 1, cyy + 50), f"#{i} g{n['gfx_id']}", fill=(190, 190, 150))

            # engine crop (only sprites fully inside the 256x224 viewport
            # can be compared; the engine clips at the letterbox edge)
            sx = ox + (n["x"] * 16 - cam_x) * zoom
            sy = oy + (n["y"] * 16 - cam_y) * zoom
            vx0, vy0 = ox, oy
            vx1, vy1 = ox + VIEW_W * zoom, oy + VIEW_H * zoom
            in_view = (sx >= vx0 and sy >= vy0 and
                       sx + 16 * zoom <= vx1 and sy + 16 * zoom <= vy1)
            crop = shot.crop((sx, sy, sx + 16 * zoom, sy + 16 * zoom)) if in_view else None

            same_cell = [k for k, q in enumerate(npcs)
                         if (q["x"], q["y"]) == (n["x"], n["y"])]
            occluded = ((n["x"], n["y"]) == (cx, cy) or
                        (len(same_cell) > 1 and i != same_cell[-1]))
            if occluded:
                in_view = False
            diff = total = 0
            if in_view:
                for y in range(16):
                    for x in range(16):
                        p = truth[y][x]
                        if p[3] == 0:
                            continue
                        wx, wy = n["x"] * 16 + x, n["y"] * 16 + y
                        if over is not None and 0 <= wx < over.width and 0 <= wy < over.height \
                                and over.getpixel((wx, wy))[3]:
                            continue
                        total += 1
                        if shot.getpixel((sx + x * zoom, sy + y * zoom)) != p[:3]:
                            diff += 1
            verdict = ("occ" if occluded else "off") if not in_view else "n/a"
            if total:
                tested += 1
                ok = diff == 0 or diff / total <= 0.05
                matched += 1 if ok else 0
                verdict = "OK" if ok else f"{diff}/{total}"

            # sprite row: truth (3x) over engine (3x)
            tx = i * 54 + 3
            for y in range(16):
                for x in range(16):
                    p = truth[y][x]
                    if p[3]:
                        for dy in range(3):
                            for dx in range(3):
                                strip.putpixel((tx + x * 3 + dx, y * 3 + dy), p[:3])
            if crop is not None:
                strip.paste(crop.resize((16 * 3, 16 * 3), 0), (tx, 54))
            col = (120, 255, 120) if verdict == "OK" else (
                  (150, 150, 150) if verdict in ("off", "occ") else (255, 160, 120))
            ds.text((tx, 2), verdict, fill=col)

        sheet.save(OUT / f"rom_{map_id}.png")
        strip.save(OUT / f"sprites_{map_id}.png")

        # ---- side by side ------------------------------------------------
        cmp_img = Image.new("RGB", (sheet.width + eng.width + 12,
                                    max(sheet.height, eng.height)), (8, 8, 12))
        cmp_img.paste(sheet, (0, 0))
        cmp_img.paste(eng, (sheet.width + 12, 0))
        cmp_img.save(OUT / f"cmp_{map_id}.png")

        title = ""
        try:
            import json
            o = json.load(open(ROOT / "data" / "ff4-en-data.json"))["obj"]
            t = o["mapProperties"][map_id].get("title")
            if isinstance(t, int):
                title = o["mapTitle"][t]
        except Exception:
            pass
        summary.append((map_id, title, matched, tested))
        print(f"map {map_id:3d} {title[:18]:18s}  npcPalette1={mp['npc_palette1']} "
              f"npcPalette2={mp['npc_palette2']}  {len(npcs)} npcs, "
              f"{matched}/{tested} compared sprites OK")

    print(f"\nwrote images to {OUT}")
    print("  rom_<map>.png      ROM-truth sprites with the code's palettes")
    print("  cmp_<map>.png      ROM truth (left) vs ff4_world (right)")
    print("  sprites_<map>.png  each NPC: ROM truth (top) vs engine (bottom)")
    print("  eng_<map>.png      raw engine framebuffer")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
