#!/usr/bin/env python3
"""The three FF4 field water/liquid animation systems, modeled from the
disassembly and (for the cave system) byte-proven against an in-game
WRAM capture.

1. FIELD TILE ANIMATION (4-phase) - DONE in the engine, guarded by
   tools/verify_animation.py.  TfrBGAnimGfx @cb5f, phase every 8 frames.

2. CAVE WATER (UpdateCaveWaterGfx @8ac4) - MODELED HERE, byte-exact:
   99/99 frames reproduce data/field/cavewater_capture/ (dense WRAM
   sampling of $7f5800, frames 15462-15660 step 2, snes9x).

   Mechanism (field.asm @8ac4 + RotateWord @8b1d):
     - runs on EVEN $7a (every other frame)
     - k = ($7a & 0x1e) >> 1, cycling 0..15 (32-frame cycle)
     - x = CaveWaterTbl[k] * 2
       CaveWaterTbl @fc66: 00 21 02 23 04 25 06 27 20 01 22 03 24 05 26 07
     - three RotateWord chains, the carry threading through all three:
         (x, x+0x20), (x+1, x+0x21), (x+0x10, x+0x30)
     - RotateWord(buf, x, c):  with A = buf[x+0x20]:
         buf[x+0x20]' = (A >> 1) | ((buf[x] & 1) << 7)
         buf[x]'      = (buf[x] >> 1) | ((A & 1) << 7)
         new carry    = buf[x] >> 7
       (the 65816 sequence sta $09/sta $07, ror $09, ror $08, ror $07)

   The buffer is the 256-byte window the game caches from VRAM word
   $2000 (LoadWaterGfx @8f69) and re-uploads with 8-byte slivers
   (TfrWaterTiles @8e0e).  Which displayed tiles it maps to is the
   remaining question for the renderer wiring (see
   docs/WATER-ANIM.md).

3. WORLD OCEAN/LAVA (UpdateWaterAnim @8fa1 / UpdateLavaAnim @8ee3) -
   ASM-decoded, pending the coastal emulator capture for byte-proof:
     - X phase on EVEN $7c, k = ($7c >> 1) & 0xf, run base =
       WaterShiftX[k] (all entries 8-aligned):
       WaterShiftX @90aa: 00 88 10 98 20 A8 30 B8 80 08 90 18 A0 28 B0 38
       the run pair (A = buf[base..base+8], B = buf[base+0x40..+0x48])
       becomes  A' = [B[7]] + A[0:7],  B' = [A[7]] + B[0:7]
       (a cross-conveyor: each run shifts down one byte, the heads swap)
     - Y phase every frame, 4 rotations at WaterShiftY[k]+0x38 and
       +0xb8 (twice each), stride 8 (columns):
       WaterShiftY @909a: 00 41 02 43 04 45 06 47 40 01 42 03 44 05 46 07
     - the X window is the ocean tile art: it matched worldGraphics
       tiles 0x80..0x83 (+assign 32) EXACTLY (flyover VRAM); the Y
       window matched tiles 0x7A..0x7D.
     - the buffer gets re-uploaded as 8-byte slivers to VRAM word
       $20{WaterShiftX[k]} and +0x40 (TfrWaterGfx @8e67), and the Y
       window DMA'd to word $1e80.

Usage:
  python3 tools/ff4_water_models.py            # replay the capture
  python3 tools/ff4_water_models.py --verify   # same, exit code
"""
import glob
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

CAVE_TBL = [0x00, 0x21, 0x02, 0x23, 0x04, 0x25, 0x06, 0x27,
            0x20, 0x01, 0x22, 0x03, 0x24, 0x05, 0x26, 0x07]
WSX = [0x00, 0x88, 0x10, 0x98, 0x20, 0xA8, 0x30, 0xB8,
       0x80, 0x08, 0x90, 0x18, 0xA0, 0x28, 0xB0, 0x38]
WSY = [0x00, 0x41, 0x02, 0x43, 0x04, 0x45, 0x06, 0x47,
       0x40, 0x01, 0x42, 0x03, 0x44, 0x05, 0x46, 0x07]


def rotate_word_exact(buf, x, c):
    """The exact 65816 sequence: sta $09/$07, ror $09, ror $08, ror $07.

    state: C=c, $08 = buf[x], A = buf[x+0x20]
      $09 = A; $07 = A
      ror $09: $09 = (A>>1)|(C<<7), C = A&1
      ror $08: $08 = (buf[x]>>1)|(C<<7), C = buf[x]&1
      ror $07: $07 = (A>>1)|(C<<7), C = (A>>1)&1... careful: ror uses the
      CURRENT C (from ror $08), and $07's input is A (sta $07 happened
      before any ror).
    out: buf[x] = $08, buf[x+0x20] = $07
    """
    a = buf[x + 0x20]                      # A
    r9 = ((a >> 1) | (c << 7)) & 0xFF      # ror $09
    c = a & 1
    r8 = ((buf[x] >> 1) | (c << 7)) & 0xFF  # ror $08
    c = buf[x] & 1
    r7 = ((a >> 1) | (c << 7)) & 0xFF      # ror $07
    c = a & 1                              # C after ror $07 = $07's bit0
    buf[x] = r8
    buf[x + 0x20] = r7
    return c


def cave_step(buf, k, c):
    """UpdateCaveWaterGfx @8ac4 - one even-$7a step. Returns new carry."""
    x = CAVE_TBL[k & 0xF] * 2
    c = rotate_word_exact(buf, x, c)
    c = rotate_word_exact(buf, x + 1, c)
    c = rotate_word_exact(buf, x + 0x10, c)
    return c


def ocean_x_step(buf, k):
    """UpdateWaterAnim X phase @8faa - one even-$7c step (ASM model,
    pending the coastal emulator capture)."""
    base = WSX[k & 0xF]
    A = bytes(buf[base:base + 8])
    B = bytes(buf[base + 0x40:base + 0x48])
    buf[base:base + 8] = bytes([B[7]]) + A[0:7]
    buf[base + 0x40:base + 0x48] = bytes([A[7]]) + B[0:7]


def ocean_y_step(buf, k):
    """UpdateWaterAnim Y phase @8ff3 - one frame step (ASM model)."""
    for _ in range(2):
        x = WSY[k & 0xF] + 0x38
        save = buf[x]
        for _i in range(7):
            buf[x] = buf[x - 8]
            x -= 8
        x = WSY[k & 0xF] + 0xB8
        buf[x - 0xB8] = buf[x]
        for _i in range(7):
            buf[x] = buf[x - 8]
            x -= 8
        buf[x] = save


def main(argv):
    # the exact replay with phase 1, like the proof run
    files = sorted(glob.glob(str(ROOT / "data/field/cavewater_capture"
                                          "/w_*.bin")))
    frames = {int(f.split("_")[-1].split(".")[0]): open(f, "rb").read()
              for f in files}
    frs = sorted(frames)
    buf = bytearray(frames[frs[0]][:256])
    c = 0
    ok = 0
    k = 1                                    # the captured run's phase
    for i, fr in enumerate(frs[1:], 1):
        cave_step(buf, k, c)
        k = (k + 1) & 0xF
        real = bytearray(frames[fr][:256])
        if buf == real:
            ok += 1
        else:
            print(f"  divergence at frame {fr} (step {i})")
            break
    print(f"cave-water model: {ok}/{len(frs) - 1} capture steps "
          f"reproduced byte-exactly")
    if "--verify" in argv:
        return 0 if ok == len(frs) - 1 else 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
