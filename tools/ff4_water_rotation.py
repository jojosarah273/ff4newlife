#!/usr/bin/env python3
"""
Prove the world water rotation (UpdateWaterAnim @8fa1) byte-for-byte.

Mechanism (field.asm): the overworld water is NOT re-uploaded graphics -
LoadWaterGfx caches two 256-byte windows of VRAM into WRAM
($7f5800 = bytes 0x4000-0x41FF, $7f5900 = bytes 0x3D00-0x3DFF), and every
field frame UpdateWaterAnim rotates one 8-byte RUN inside each window
(X pass: consecutive bytes; Y pass: stride-8 = a column), then TfrWaterGfx
DMAs the windows back.  The run's position comes from

    WaterShiftX @90aa: 00 88 10 98 20 a8 30 b8 80 08 90 18 a0 28 b0 38
    WaterShiftY @909a: 00 41 02 43 04 45 06 47 40 01 42 03 44 05 46 07
    X: run head = (WaterShiftX[($7c>>1)&15] | 7)  -> then +0x47 block
    Y: run head =  WaterShiftY[$7c&15] + 0x38     -> then +0xb8 block
with $7c incremented once per field frame (X only when $7c even).

This tool extracts the two windows from dense emulator VRAM dumps and
searches for the (index, pass) whose rotation reproduces the observed
window transition EXACTLY.  A byte-exact match proves the mechanism and
hands us a formula we can run in the engine.

Usage: python3 tools/ff4_water_rotation.py
"""
import glob
import re
import sys

WATER_SHIFT_X = [0x00, 0x88, 0x10, 0x98, 0x20, 0xA8, 0x30, 0xB8,
                 0x80, 0x08, 0x90, 0x18, 0xA0, 0x28, 0xB0, 0x38]
WATER_SHIFT_Y = [0x00, 0x41, 0x02, 0x43, 0x04, 0x45, 0x06, 0x47,
                 0x40, 0x01, 0x42, 0x03, 0x44, 0x05, 0x46, 0x07]


def rotate_run(buf, x, count=7, stride=1):
    """one 8-byte right-rotation: buf[x] <- buf[x-stride], 7 times, with the
    saved head placed at the run's tail by the caller."""
    saved = buf[x]
    for _ in range(count):
        buf[x] = buf[x - stride]
        x -= stride
    return saved


def x_pass(buf, idx):
    """UpdateWaterAnim's X pass on window buf (256 B), table index idx."""
    b = bytearray(buf)
    x0 = WATER_SHIFT_X[idx] | 7
    head = rotate_run(b, x0, 7, 1)            # 8-byte run, consecutive bytes
    x1 = (x0 & ~7) + 0x47
    b2 = (x1 - 0x47) & 0xFF                     # sta $7f57b9,x = buf[x1-0x47]
    saved2 = b[x1]
    b[b2] = saved2                              # the +0x47 bridge store
    rotate_run(b, x1, 7, 1)
    b[x1 & ~7 & 0xFF] = head                    # restore the saved head
    return b


def y_pass(buf, idx):
    """UpdateWaterAnim's Y pass: stride-8 (a column), blocks at +0x38/+0xb8."""
    b = bytearray(buf)
    y0 = WATER_SHIFT_Y[idx] + 0x38
    head = rotate_run(b, y0, 7, 8)
    y1 = WATER_SHIFT_Y[idx] + 0xB8
    b2 = (y1 - 0xB8) & 0xFF
    saved2 = b[y1]
    b[b2] = saved2
    rotate_run(b, y1, 7, 8)
    b[y1 & ~7 & 0xFF] = head
    return b


def load_windows(dumps):
    """(frame, Xwindow 0x4000-0x41FF, Ywindow 0x3D00-0x3DFF) per dump."""
    out = []
    for f in dumps:
        fr = int(re.search(r"_(\d+)\.bin$", f)[1])
        v = open(f, "rb").read()
        out.append((fr, v[0x4000:0x4200], v[0x3D00:0x3E00]))
    return out


def main():
    dumps = sorted(glob.glob("/tmp/gt/water_*.bin")) or \
        sorted(glob.glob("/tmp/gt/wlong_*.bin"))
    if not dumps:
        print("no VRAM dumps found (run the standing capture first)")
        return 1
    wins = load_windows(dumps)
    print(f"{len(wins)} dumps, frames {wins[0][0]}..{wins[-1][0]}")

    # find every transition where a window changed
    ok = fail = 0
    for k in range(1, len(wins)):
        _, x0, y0 = wins[k - 1]
        _, x1, y1 = wins[k]
        dx = sum(1 for a, b in zip(x0, x1) if a != b)
        dy = sum(1 for a, b in zip(y0, y1) if a != b)
        if not dx and not dy:
            continue
        frame = wins[k][0]
        print(f"\nframe {wins[k-1][0]}->{frame}: X changed {dx} B, Y {dy} B")
        if dx:
            hit = None
            for i in range(16):
                if x_pass(x0, i) == x1:
                    hit = ("X", i)
                    break
            print(f"   X: {'MATCH shift index %d' % hit[1] if hit else 'no 16-index match'}")
            ok += 1 if hit else 0
            fail += 0 if hit else 1
        if dy:
            hit = None
            for i in range(16):
                if y_pass(y0, i) == y1:
                    hit = ("Y", i)
                    break
            print(f"   Y: {'MATCH shift index %d' % hit[1] if hit else 'no 16-index match'}")
            ok += 1 if hit else 0
            fail += 0 if hit else 1
    print(f"\n{ok} matched transitions, {fail} unmatched")
    if fail:
        print("unmatched: the rotation model needs revision vs the ASM")
        return 1
    print("WORLD WATER ROTATION PROVEN (byte-exact vs emulator VRAM)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
