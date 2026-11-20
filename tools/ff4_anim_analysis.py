#!/usr/bin/env python3
"""
Which tiles does FF4 animate?  — analysis + visual evidence.

Answers the question "is there something special about these tiles?".

Short answer: yes, and it is not a semantic (water/lava) rule.  Animation is a
property of the **tile inside the tileset flag table**: a sub-tile whose graphic
index is 0x120..0x12F is one of the 16 shared animated slots, which is exactly
VRAM tiles $120..$12F - the 16 tiles `TfrBGAnimGfx` re-uploads every 8 frames.

Evidence chain (all in this file's output):
  1. the flag-table decode and per-tileset animated tile numbers
  2. the animated block: MapAnimGfx @0xFB9C0 + BGAnimTbl[tileset]*384 (3bpp),
     phase layout 16*(k//4) + 4*phase + (k%4)
  3. emulator VRAM dumps: only tiles $120..$12F change over 8 frames; in the
     castle-interior scene 16/16 tiles match the staged block for two frames
     (see docs/FIELD-ANIM-ANALYSIS.md for the raw numbers)
  4. phase sheets: /tmp/animtiles_<tileset>.png, drawn with a real map palette
  5. world-map anomaly: the overworld animates a *different* VRAM range
     ($221..$23C), so world water needs its own investigation
  6. palette animation (candles/lights/lava/ocean shimmer) is a separate
     mechanism - field/pal_anim.asm - listed at the end

Usage: python3 tools/ff4_anim_analysis.py
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ff4_export_submaps as E

ROM, JSON_PATH = "ff2.smc", "data/ff4-en-data.json"

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ff4_palette_truth import Rom, cgram_slots  # noqa: E402


def flag_table(rom, tileset_n):
    """[[(lo,hi) x4] x128] for a tileset."""
    ts = E.TILESET + (tileset_n - 1) * 0x400
    out = []
    for b in range(128):
        out.append([(rom[ts + b * 2 + j * 0x100], rom[ts + b * 2 + j * 0x100 + 1])
                    for j in range(4)])
    return out


def main():
    rom = open(ROM, "rb").read()
    if len(rom) == 0x100200:
        rom = rom[0x200:]
    obj = json.load(open(JSON_PATH))
    o = obj.get("obj", obj)
    props, L1 = o["mapProperties"], o["mapLayouts1"]

    # ---- 1. per-tileset animated tiles
    print("=" * 78)
    print("1. ANIMATED TILES PER TILESET  (graphic index 0x120..0x12F)")
    print("=" * 78)
    total_tiles = 0
    ts_anim = {}
    for n in range(1, 17):
        flags = flag_table(rom, n)
        anim = {}
        for b, subs in enumerate(flags):
            slots = [(j, lo + (hi & 3) * 0x100, (hi >> 2) & 7)
                     for j, (lo, hi) in enumerate(subs)
                     if 0x120 <= lo + (hi & 3) * 0x100 <= 0x12F]
            if slots:
                anim[b] = slots
        ts_anim[n] = anim
        total_tiles += len(anim)
        if anim:
            print(f"  tileset {n:2d}  BGAnimTbl={E.BG_ANIM_TBL[n-1]:#04x}  "
                  f"block=MapAnimGfx+{E.BG_ANIM_TBL[n-1]*384:#06x}  "
                  f"animated tiles: {sorted(anim)}")
            for b, slots in sorted(anim.items()):
                print(f"      tile {b:3d}: slots "
                      + ", ".join(f"{s:#05x}(sub{j},grp{g})" for j, s, g in slots))
        else:
            print(f"  tileset {n:2d}  BGAnimTbl={E.BG_ANIM_TBL[n-1]:#04x}  "
                  f"no animated tiles")
    print(f"\n  {total_tiles} animated tile numbers in the whole game, "
          f"out of {16*128} possible tile slots")

    # ---- 2. maps and which of them use animated tiles
    print()
    print("=" * 78)
    print("2. MAPS USING ANIMATED TILES")
    print("=" * 78)
    per_ts = {}
    used = {}
    for mid, p in enumerate(props):
        if not isinstance(p, dict):
            continue
        lay = p.get("layout1", -1)
        if not (0 <= lay < len(L1)):
            continue
        blob = E.b64(L1[lay])
        if len(blob) < 1024:
            continue
        n = p.get("graphics", 0) + 1
        if not (1 <= n <= 16) or not ts_anim.get(n):
            continue
        lay2 = p.get("layout2", -1)
        bg2 = E.b64(L1[lay2]) if 0 <= lay2 < len(L1) else None
        hits = set()
        for bl in (bg2, blob):
            if not bl or len(bl) < 1024:
                continue
            for cell in range(512):
                b = bl[cell]
                if b < 128 and b in ts_anim[n]:
                    hits.add(b)
        if hits:
            used[mid] = (n, sorted(hits))
            per_ts.setdefault(n, []).append(mid)
    print(f"  {len(used)} of {len(props)} maps reference an animated tile")
    for n in sorted(per_ts):
        print(f"    tileset {n:2d}: {len(per_ts[n]):3d} maps  "
              f"(e.g. {per_ts[n][:12]})")

    # ---- 3. phase sheets with real palettes
    print()
    print("=" * 78)
    print("3. PHASE SHEETS  ->  /tmp/animtiles_<tileset>.png")
    print("=" * 78)
    from PIL import Image
    for n in sorted(ts_anim):
        if not ts_anim[n]:
            continue
        # real palette: first map that uses this tileset
        mid = next((m for m, (t, _) in used.items() if t == n), None)
        pal = props[mid].get("palette", 0) if mid is not None else 0
        tiles = sorted(ts_anim[n])
        S = 4
        W = 8 + len(tiles) * (4 * (8 * S + 6) + 12)
        H = 8 + 2 * (8 * S + 8)
        im = Image.new("RGB", (W, H), (16, 16, 22))
        px = im.load()
        for ci, b in enumerate(tiles):
            x0 = 6 + ci * (4 * (8 * S + 6) + 12)
            for phase in range(4):
                grp = ts_anim[n][b][0][2]
                t = E.anim_phase_px(rom, n, ts_anim[n][b][0][1], phase)
                for p in range(2):
                    y0 = 6 + p * (8 * S + 8)
                    tx = x0 + phase * (8 * S + 6)
                    for y in range(8):
                        for x in range(8):
                            c = t[y * 8 + x]
                            if c == 0:
                                continue
                            r, g, bl = E.color16(rom, pal, pal, grp, c & 7)
                            for dy in range(S // 2):
                                for dx in range(S // 2):
                                    px[tx + x * (S // 2) + dx,
                                       y0 + y * (S // 2) + dy] = (r, g, bl)
        path = f"/tmp/animtiles_{n}.png"
        im.save(path)
        print(f"  tileset {n:2d}: tiles {tiles} (palette {pal}, map {mid}) -> {path}")

    # ---- 4. world map check
    print()
    print("=" * 78)
    print("4. WORLD MAP (250..255)")
    print("=" * 78)
    for mid in (250, 251, 252, 253):
        if not isinstance(props[mid], dict):
            continue
        n = props[mid].get("graphics", 0) + 1
        print(f"  map {mid}: tileset {n}, animated tiles in its flag table: "
              f"{sorted(ts_anim[n])}")
    print("  => the world map flag table has NO 0x120..0x12F tile, yet the")
    print("     emulator shows VRAM tiles $221..$23C changing every 8 frames on")
    print("     the overworld.  World water is a separate animation pool.")

    # ---- 5. palette animation
    print()
    print("=" * 78)
    print("5. PALETTE ANIMATION (candles, lights, lava, ocean shimmer)")
    print("=" * 78)
    for line in (
        "field/pal_anim.asm:",
        "  UpdateAnimPal @c41a  per tileset, CGRAM entries:",
        "    tileset 8/10/11 (whale, zot/babil, giant):",
        "      TowerAnimBlueLight   -> $0cfd (bg pal 2 col 1)",
        "      TowerAnimRedLight    -> $0d1d (bg pal 4 col 1)",
        "      TowerAnimYellowLight -> $0d5d (bg pal 8 col 1)",
        "      TowerAnimBluePal x16 -> $0dbd (a whole bg palette)",
        "    tileset 9 (underground cave, +0x40 for Sylvan):",
        "      MonsterCaveAnimPal   -> $0cfd/$0cff/$0d7f/$0d01/$0d7d",
        "  UpdateLavaPal  @c4cd  underground only, LavaPalTbl 8 steps,",
        "      LavaAnimPal x8 colors -> $0cff (bg pal 2 colours 1..8)",
        "  UpdateBabilPal @c3ea  flashing tower colours",
        "  UpdateWhalePal @c3bd  WhalePal x8 -> $0ec7/$0ec8",
        "  UpdateZoomPal  @c505  ZoomPalTbl -> ocean colours while zooming",
        "                        (this is the overworld ocean shimmer)",
    ):
        print("  " + line)


if __name__ == "__main__":
    main()
