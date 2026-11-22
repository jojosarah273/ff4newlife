#!/usr/bin/env python3
"""
Guard for the world-water shimmer (TfrBGAnimGfx @cb5f on the world maps).

Mechanism (byte-proven against emulator VRAM captures,
data/field/worldwater_capture/ + tools/ff4_water_models.py --verify):
  - 8 animated tile slots hold the staged MapAnimGfx phases
  - one phase every 8 frames, 4 phases, period 32
  - the staged art's nonzero bytes are the wave flecks (palette index +
    the tile's assign), its zero bytes leave the static base visible

Engine contract under test (FF4_WORLD=0, an ocean view):
  1. each pinned phase renders EXACTLY the model's merge of
     (static band art + staged phase art) over the same pack data -
     computed here independently from the .ff4world pack + the ROM
  2. the natural cadence: +8 frames differ, +32 frames identical
  3. FF4_NOANIM pins phase 0

Usage: python3 tools/verify_world_water.py
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
CX, CY = 128, 128          # deep ocean: composed 0x19, base 0x80..0x83


def run_engine(extra, out, delay=25):
    env = dict(os.environ)
    env.update({"SDL_VIDEODRIVER": "dummy",
                "FF4_WORLD": "0", "FF4_MAPX": str(CX), "FF4_MAPY": str(CY),
                "FF4_ZOOM": "1.0", "FF4_SHOT": str(out),
                "FF4_SHOT_DELAY": str(delay), "FF4_NOCLIP": "1"})
    env.update(extra)
    subprocess.run(["timeout", "60", str(EXE)], env=env,
                   capture_output=True, timeout=90)
    if not Path(out).exists():
        return None
    im = Image.open(out).convert("RGB")
    ox, oy = (im.width - VIEW_W) // 2, (im.height - VIEW_H) // 2
    return im.crop((ox, oy, ox + VIEW_W, oy + VIEW_H))


def pack_data():
    """The overworld pack: cells, base, assign, gfx, pal, wateranim."""
    d = (ROOT / "data/world/overworld.ff4world").read_bytes()
    magic, w, h, nart, npal, _lava = struct.unpack_from("<4sHHHBB", d, 0)
    o = 12
    cells = d[o:o + w * h]; o += w * h
    base = d[o:o + 512]; o += 512
    assign = d[o:o + 256]; o += 256
    gfx = d[o:o + nart * 64]; o += nart * 64
    pal = d[o:o + 256]; o += 256
    wateranim = d[o:o + 4 * 512]
    return w, h, cells, base, assign, gfx, pal, wateranim


def expected_view(phase):
    """The model's merge, computed independently of the engine."""
    w, h, cells, base, assign, gfx, pal, wateranim = pack_data()
    out = Image.new("RGB", (VIEW_W, VIEW_H))
    px = out.load()
    ax, ay = CX * 16 + 8, CY * 16 + 8
    camx = ax - 128
    camy = ay - 112          # the engine's world camera anchor
    for cyy in range(camy // 16 - 1, camy // 16 + VIEW_H // 16 + 2):
        for cxx in range(camx // 16 - 1, camx // 16 + VIEW_W // 16 + 2):
            cx, cy = cxx & 255, cyy & 255
            c = cells[cy * 256 + cx]
            for k in range(4):
                bt = base[c * 4 + k]
                pb = assign[bt]
                art = gfx[bt * 64:bt * 64 + 64]
                anim = None
                if 0x80 <= bt <= 0x87:
                    anim = wateranim[phase * 512 + (bt - 0x80) * 64:
                                     phase * 512 + (bt - 0x80) * 64 + 64]
                for y in range(8):
                    for x in range(8):
                        sx = cxx * 16 + (k % 2) * 8 + x - camx
                        sy = cyy * 16 + (k // 2) * 8 + y - camy
                        if not (0 <= sx < VIEW_W and 0 <= sy < VIEW_H):
                            continue
                        ci = art[y * 8 + x]
                        if anim and anim[y * 8 + x]:
                            ci = anim[y * 8 + x]
                        ci = (ci + pb) % 64
                        px[sx, sy] = (pal[ci * 4], pal[ci * 4 + 1],
                                      pal[ci * 4 + 2])
    return out


def diff(a, b, skip_actor=True):
    n = 0
    for y in range(VIEW_H):
        for x in range(VIEW_W):
            if skip_actor and 104 < x < 152 and 96 < y < 144:
                continue
            if a.getpixel((x, y)) != b.getpixel((x, y)):
                n += 1
    return n


def main():
    fails = []
    shots = {}
    for ph in range(4):
        shots[ph] = run_engine({"FF4_ANIMPHASE": str(ph)},
                               f"/tmp/ww_ph{ph}.png")
        if shots[ph] is None:
            print(f"phase {ph}: capture failed")
            return 1
        exp = expected_view(ph)
        d = diff(shots[ph], exp)
        print(f"phase {ph}: engine vs model merge -> {d} px differ")
        if d:
            fails.append(f"phase {ph} render ({d} px)")

    # phases must differ from each other (the animation is visible)
    d01 = diff(shots[0], shots[1], skip_actor=False)
    print(f"phase 0 vs 1: {d01} px differ (must be > 0)")
    if d01 == 0:
        fails.append("the phases do not differ - no visible shimmer")

    # the natural cadence
    a = run_engine({}, "/tmp/ww_c25.png", delay=25)
    b = run_engine({}, "/tmp/ww_c33.png", delay=33)
    c = run_engine({}, "/tmp/ww_c57.png", delay=57)
    d8 = diff(a, b, skip_actor=False)
    d32 = diff(a, c, skip_actor=False)
    print(f"cadence +8: {d8} px differ, +32: {d32} px differ")
    if d8 == 0:
        fails.append("no animation at the 8-frame cadence")
    if d32 > 0:
        fails.append(f"32 frames did not return to phase 0 ({d32} px)")

    # NOANIM pins phase 0
    n = run_engine({"FF4_NOANIM": "1"}, "/tmp/ww_noanim.png")
    d = diff(n, shots[0], skip_actor=False)
    print(f"FF4_NOANIM vs phase 0 pin: {d} px differ")
    if d:
        fails.append(f"FF4_NOANIM did not pin phase 0 ({d} px)")

    if fails:
        print("\nFAILURES:")
        for f in fails:
            print("  -", f)
        return 1
    print("\nworld-water shimmer OK (phases match the proven model, "
          "8-frame cadence, 32-frame period, NOANIM pins)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
