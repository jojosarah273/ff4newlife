#!/usr/bin/env python3
"""
Lock the message box: render it headlessly and assert every measured number.

Ground truth (256x224, real snes9x frame 2100 + the ASM):
    border white rows   17 (top), 89 (bottom)
    border white cols    9 (left), 246 (right)
    text line tops      27, 43, 59, 75   (pitch 16)
    no black band between the screen top and the box

Exit code 0 when everything matches, 1 on any drift.

    python3 tools/verify_messagebox.py
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = Path("/tmp/msgbox_lock.png")

EXPECT_WHITE_ROWS = (17, 89)
EXPECT_WHITE_COLS = (9, 246)
EXPECT_TEXT_TOPS = [27, 43, 59]
TOL = 1          # px, the game's IRQ scroll trick costs us 1px at the bottom


def capture():
    env = dict(os.environ)
    env.update({
        "FF4_WARPTO": "0", "FF4_MAPX": "14", "FF4_MAPY": "16",
        "FF4_DIALOG": "1", "FF4_ZOOM": "1.0",
        "FF4_SHOT": str(OUT), "FF4_NPCFREEZE": "1", "FF4_NOCLIP": "1",
    })
    r = subprocess.run([str(ROOT / "build" / "ff4_world")], env=env,
                       capture_output=True, text=True, timeout=180)
    return OUT.exists(), r.stdout + r.stderr


def main():
    ok, log = capture()
    if not ok:
        print("engine produced no screenshot:\n" + log[-800:])
        return 1

    from PIL import Image
    shot = Image.open(OUT).convert("RGB")
    ox, oy = (shot.width - 256) // 2, (shot.height - 224) // 2
    img = shot.crop((ox, oy, ox + 256, oy + 224))
    px = img.load()

    def white(c):
        return c[0] > 230 and c[1] > 230 and c[2] > 230

    fails = []

    rows = [y for y in range(224)
            if sum(1 for x in range(256) if white(px[x, y])) > 200]
    if len(rows) < 2:
        fails.append(f"no border lines found (rows={rows})")
    else:
        top, bottom = rows[0], rows[-1]
        if abs(top - EXPECT_WHITE_ROWS[0]) > TOL:
            fails.append(f"top border at y={top}, expected {EXPECT_WHITE_ROWS[0]}")
        if abs(bottom - EXPECT_WHITE_ROWS[1]) > 2:
            fails.append(f"bottom border at y={bottom}, expected {EXPECT_WHITE_ROWS[1]}")

    cols = [x for x in range(256)
            if sum(1 for y in range(224) if white(px[x, y])) > 60]
    if len(cols) < 2:
        fails.append(f"no border columns found (cols={cols})")
    else:
        left, right = cols[0], cols[-1]
        if abs(left - EXPECT_WHITE_COLS[0]) > TOL:
            fails.append(f"left border at x={left}, expected {EXPECT_WHITE_COLS[0]}")
        if abs(right - EXPECT_WHITE_COLS[1]) > TOL:
            fails.append(f"right border at x={right}, expected {EXPECT_WHITE_COLS[1]}")

    # text line tops (ignore the border rows themselves)
    tops, inband = [], False
    for y in range(20, 88):
        cnt = sum(1 for x in range(12, 244) if white(px[x, y]))
        if cnt > 3 and not inband:
            tops.append(y)
            inband = True
        elif cnt <= 3:
            inband = False
    for want in EXPECT_TEXT_TOPS:
        if not any(abs(t - want) <= TOL for t in tops):
            fails.append(f"no text line near y={want} (found {tops})")

    # the black-bar regression: the band between the top of the screen and the
    # box must show the map, never an opaque black strip
    for y in range(12, 16):
        dark = sum(1 for x in range(10, 246) if px[x, y] == (0, 0, 0))
        if dark > 100:
            fails.append(f"black band above the box at y={y} ({dark} px)")

    if fails:
        print("MESSAGE BOX REGRESSION:")
        for f in fails:
            print("  -", f)
        print(f"\nsee {OUT}")
        return 1

    print(f"message box OK: borders y={rows[0]}/{rows[-1]} x={cols[0]}/{cols[-1]}, "
          f"text lines {tops[:4]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
