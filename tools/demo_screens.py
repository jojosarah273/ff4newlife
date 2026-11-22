#!/usr/bin/env python3
"""
The ff4newlife demo: a headless screenshot tour of the rebuilt field scene,
including an ANIMATED GIF of the world-water shimmer.

Produces /tmp/ff4_demo/:
  01_overworld.png      the overworld, ocean view
  02_world_anim.gif     the ocean shimmering (one full 32-frame cycle)
  03_baron.png          Baron town (map 0) with NPCs and the actor
  04_dialog.png          the message box over the field
  05_kaipo_phases.png   the sub-map tile animation (Kaipo's sea, 4 phases)
  06_bridge_z.png        the bridge z-split (head above the bridge art)
  07_underground.png    the underground with the lava palette cycling

Run:  python3 tools/demo_screens.py
Everything runs headless (SDL dummy driver) and is bounded per shot.
"""
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "ff4_world"
OUT = Path("/tmp/ff4demo")
OUT.mkdir(parents=True, exist_ok=True)


def shot(name, env_extra, delay=25):
    out = OUT / name
    env = dict(os.environ)
    env.update({"SDL_VIDEODRIVER": "dummy", "FF4_ZOOM": "1.0",
                "FF4_SHOT": str(out), "FF4_SHOT_DELAY": str(delay),
                "FF4_NOCLIP": "1", "FF4_NPCFREEZE": "1", "FF4_NOANIM": "1",
                "FF4_LAYOUTG1": "0"})
    for k, v in env_extra.items():
        if v is None:
            env.pop(k, None)
        else:
            env[k] = v
    subprocess.run(["timeout", "60", str(EXE)], env=env,
                   capture_output=True, timeout=90)
    return out if out.exists() else None


def crop(p):
    im = Image.open(p).convert("RGB")
    ox, oy = (im.width - 256) // 2, (im.height - 224) // 2
    return im.crop((ox, oy, ox + 256, oy + 224))


def main():
    missing = []
    print("ff4newlife demo ->", OUT)

    # 1: the overworld, standing before the ocean near the game start
    p = shot("01_overworld.png", {"FF4_WORLD": "0", "FF4_MAPX": "107",
                                  "FF4_MAPY": "158"})
    print("  01 overworld:", "ok" if p else "FAILED")
    if not p:
        missing.append("overworld")

    # 2: the animated ocean - one full 32-frame cycle at 4-frame steps,
    #    assembled into a looping GIF
    frames = []
    for delay in range(24, 58, 4):
        p = shot(f"_fr{delay}.png", {"FF4_WORLD": "0", "FF4_MAPX": "128",
                                     "FF4_MAPY": "128", "FF4_NOANIM": None},
                 delay=delay)
        if p:
            frames.append(crop(p))
    if len(frames) >= 8:
        frames[0].save(OUT / "02_world_anim.gif", save_all=True,
                        append_images=frames[1:], duration=80, loop=0)
        print(f"  02 world-water animation: ok ({len(frames)} frames)")
    else:
        print("  02 world-water animation: FAILED")
        missing.append("world anim")

    # 3: Baron town with NPCs and the party
    p = shot("03_baron.png", {"FF4_WARPTO": "0", "FF4_MAPX": "14",
                              "FF4_MAPY": "16"})
    print("  03 baron:", "ok" if p else "FAILED")
    if not p:
        missing.append("baron")

    # 4: the message box
    p = shot("04_dialog.png", {"FF4_WARPTO": "0", "FF4_MAPX": "14",
                               "FF4_MAPY": "16", "FF4_DIALOG": "1"})
    print("  04 dialog:", "ok" if p else "FAILED")
    if not p:
        missing.append("dialog")

    # 5: the sub-map tile animation - Kaipo's sea through its 4 phases
    ph = []
    for phase in range(4):
        p = shot(f"_kp{phase}.png", {"FF4_WARPTO": "2", "FF4_MAPX": "16",
                                    "FF4_MAPY": "20", "FF4_NOANIM": None,
                                    "FF4_ANIMPHASE": str(phase)})
        if p:
            ph.append(crop(p))
    if len(ph) == 4:
        sheet = Image.new("RGB", (4 * 256, 224))
        for i, im in enumerate(ph):
            sheet.paste(im, (i * 256, 0))
        sheet.save(OUT / "05_kaipo_phases.png")
        print("  05 kaipo phases: ok")
    else:
        print("  05 kaipo phases: FAILED")
        missing.append("kaipo")

    # 6: the bridge z-order
    p = shot("06_bridge_z.png", {"FF4_WARPTO": "36", "FF4_MAPX": "25",
                                 "FF4_MAPY": "15", "FF4_FACING": "2"})
    print("  06 bridge z:", "ok" if p else "FAILED")
    if not p:
        missing.append("bridge")

    # 7: the underground - lava palette cycling (two steps side by side)
    a = shot("_lv0.png", {"FF4_WORLD": "1", "FF4_MAPX": "99", "FF4_MAPY": "83",
                          "FF4_LAVASTEP": "0", "FF4_NOANIM": None})
    b = shot("_lv3.png", {"FF4_WORLD": "1", "FF4_MAPX": "99", "FF4_MAPY": "83",
                          "FF4_LAVASTEP": "3", "FF4_NOANIM": None})
    if a and b:
        sheet = Image.new("RGB", (512, 224))
        sheet.paste(crop(a), (0, 0))
        sheet.paste(crop(b), (256, 0))
        sheet.save(OUT / "07_underground.png")
        print("  07 underground: ok")
    else:
        print("  07 underground: FAILED")
        missing.append("underground")

    if missing:
        print("\nFAILED:", ", ".join(missing))
        return 1
    print(f"\nall demo shots in {OUT} - open 02_world_anim.gif to watch "
          "the water move")
    return 0


if __name__ == "__main__":
    sys.exit(main())
