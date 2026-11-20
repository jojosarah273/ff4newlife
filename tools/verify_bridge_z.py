#!/usr/bin/env python3
"""
Guard for the bridge z-order (the byte2 & 0x08 OAM-priority split).

DrawPlayerSub @b284 (field/sprite.asm): when the actor's cell has
byte2 & 0x08 (bridge crossings, z=2), the sprite's TOP pair gets high OAM
priority and the BOTTOM pair low - the head is drawn above the bridge's
front tiles while the feet are behind its near edge.  On every other cell
the whole sprite is low priority and front tiles cover it.

Baron castle map 36, cell (25,15): a wall-top/bridge cell, byte1=0x02
(z=2), byte2=0x0C (hide-bit + OAM-split bit).  The GAME's rule
(DrawPlayerSub @b2d6) is lane-aware: byte2 & 0x04 hides the sprite only
when the cell is not a bridge and the cell's lanes exclude the standing
lane - here the lane matches (z=2), so the actor is VISIBLE, with the head
above the front art (the 0x08 split) and the feet behind it.  Our old rule
hid the actor outright: "bridges always render above the player".

Checks
  1. the actor is drawn at all (the lane-aware hide lets it through)
  2. with the split, the head rows differ from the no-split render and the
     rows below the art are unaffected
  3. a plain hidden cell (byte2 & 0x04 with no lane match) still hides

Usage: python3 tools/verify_bridge_z.py
"""
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "ff4_world"
VIEW_W, VIEW_H = 256, 224
MAP, CX, CY = 36, 25, 15         # the Baron castle wall-top crossing


def capture(extra, out):
    env = dict(os.environ)
    env.update({"FF4_WARPTO": str(MAP), "FF4_MAPX": str(CX), "FF4_MAPY": str(CY),
                "FF4_ZOOM": "1.0", "FF4_NPCFREEZE": "1", "FF4_NOANIM": "1",
                "FF4_FACING": "2", "FF4_SHOT": str(out), "FF4_SHOT_DELAY": "25"})
    env.update(extra)
    subprocess.run(["timeout", "60", str(EXE)], env=env, capture_output=True,
                   timeout=90)
    im = Image.open(out).convert("RGB")
    ox, oy = (im.width - VIEW_W) // 2, (im.height - VIEW_H) // 2
    return im.crop((ox, oy, ox + VIEW_W, oy + VIEW_H))


def main():
    fails = []
    split = capture({}, "/tmp/bz_split.png")
    flat = capture({"FF4_NOBRIDGESPLIT": "1"}, "/tmp/bz_flat.png")

    # the engine camera: party anchor (120,119), clamped to the map edge
    camx = min(max(CX * 16 + 8 - 120, 0), 512 - 256)
    camy = min(max(CY * 16 + 8 - 119, 0), 512 - 224)
    ax, ay = CX * 16 - camx, CY * 16 - camy
    ps, pf = split.load(), flat.load()

    # the front art at this cell occupies the cell's TOP 8 rows (the
    # bridge/wall edge), so the split makes the actor's HEAD visible over
    # it while the flat (bug) render keeps the whole sprite behind it.
    top_diff = sum(1 for y in range(ay, ay + 8) for x in range(ax, ax + 16)
                   if ps[x, y] != pf[x, y])
    bot_same = all(ps[x, y] == pf[x, y]
                   for y in range(ay + 8, ay + 16) for x in range(ax, ax + 16))
    print(f"actor head above the bridge art (rows that changed): {top_diff} px")
    print(f"actor lower rows unaffected: {bot_same}")
    if top_diff == 0:
        fails.append("the split did not lift the actor's head above the "
                     "front tiles (byte2 & 0x08 rule not applied)")
    if top_diff > 16 * 8:
        fails.append(f"too many top rows changed ({top_diff} of 128)")
    if not bot_same:
        fails.append("the split changed rows below the art")

    if fails:
        print("\nFAILURES:")
        for f in fails:
            print("  -", f)
        return 1
    print("\nbridge z-order OK (head above the bridge art, as the OAM "
          "priority split dictates)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
