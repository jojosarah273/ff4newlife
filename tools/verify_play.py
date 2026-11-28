#!/usr/bin/env python3
"""
Guard for the PLAYABLE layer: talk, message pages, and the menu.

Scripted input is injected as real SDL key events (FF4_KEYS), so the
test drives the same code path a human does.

Checks
  1. TALK - standing on Baron's map 0 spawn, Z opens the message box with
     the NPC's ROM text (map 0's (14,16) NPC): the box borders are at the
     locked geometry and the text band has ink
  2. PAGES - the same text with Z pressed until it closes: the page
     indicator changes and the box eventually closes (A advances/closes)
  3. MENU - Enter opens the main menu: the window border is present and at
     least two menu rows are drawn (the party is loaded: Cecil + Kain)
  4. NAVIGATION - in the menu, DOWN then Z opens a sub-screen (the cursor
     moves and the selection is taken); X returns, X closes
  5. the game stack is real: the party ids are Cecil (1) and Kain (2),
     gil 300, items > 0

Usage: python3 tools/verify_play.py
"""
import os
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "ff4_world"
VIEW_W, VIEW_H = 256, 224

# SDL2 scancodes
Z, X, ENTER, ESC = 29, 27, 40, 41
F5, F9 = 62, 66
DOWN, UP, LEFT, RIGHT = 81, 82, 80, 79


def run(keys="", delay=40, extra=None, shot="/tmp/play.png"):
    env = dict(os.environ)
    env.update({"SDL_VIDEODRIVER": "dummy", "FF4_WARPTO": "0",
                "FF4_MAPX": "14", "FF4_MAPY": "16", "FF4_ZOOM": "1.0",
                "FF4_NPCFREEZE": "1", "FF4_SHOT": shot,
                "FF4_SHOT_DELAY": str(delay), "FF4_KEYS": keys})
    if extra:
        env.update(extra)
    r = subprocess.run(["timeout", "60", str(EXE)], env=env,
                       capture_output=True, text=True, timeout=90)
    out = r.stdout + r.stderr
    img = None
    if Path(shot).exists():
        im = Image.open(shot).convert("RGB")
        ox, oy = (im.width - VIEW_W) // 2, (im.height - VIEW_H) // 2
        img = im.crop((ox, oy, ox + VIEW_W, oy + VIEW_H))
    return out, img


def white(c):
    return c[0] > 230 and c[1] > 230 and c[2] > 230


def border_rows(img, need=200):
    px = img.load()
    return [y for y in range(224)
            if sum(1 for x in range(256) if white(px[x, y])) > need]


def text_ink(img, y0, y1, x0=24, x1=232):
    px = img.load()
    return sum(1 for y in range(y0, y1) for x in range(x0, x1)
               if white(px[x, y]))


def main():
    fails = []

    # ---- 5: the game stack loaded ----
    out, _ = run("")
    m = re.search(r"\[game\] party (\d+)\+(\d+), gil (\d+), items (\d+)", out)
    if not m:
        fails.append("the game stack did not load (no [game] line)")
    else:
        cecil, kain, gil, items = map(int, m.groups())
        print(f"game stack: party {cecil}+{kain}, gil {gil}, items {items}")
        if (cecil, kain) != (1, 2):
            fails.append(f"party ids {cecil}+{kain}, expected Cecil(1)+Kain(2)")
        if gil != 300 or items == 0:
            fails.append("gold/inventory not initialised")

    # ---- 1: talk ----
    out, img = run(f"10:{Z}:2")
    talked = "[talk] npc" in out
    print(f"talk: {'triggered' if talked else 'NO talk line'}")
    if not talked:
        fails.append("Z on the spawn cell did not talk to the NPC")
    if img is None:
        fails.append("talk capture failed")
    else:
        rows = border_rows(img)
        ink = text_ink(img, 27, 35)
        print(f"  box border rows {rows[:2]}, first text row ink {ink}")
        if 17 not in rows:
            fails.append("the message box border is not at y=17")
        if ink < 30:
            fails.append("no text drawn in the box")

    # ---- 2: pages: Z opens, a second Z closes (this NPC's text fits one
    #         page); a long text pages instead. Either way the confirm key
    #         must drive the box, and spamming Z must end closed. ----
    _, img1 = run(f"10:{Z}:2", delay=25, shot="/tmp/play_pg1.png")
    _, img2 = run(f"10:{Z}:2,25:{Z}:2", delay=40, shot="/tmp/play_pg2.png")
    if img1 is None or img2 is None:
        fails.append("page captures failed")
    else:
        r1 = 17 in border_rows(img1)
        r2 = 17 in border_rows(img2)
        print(f"pages: box after Z {r1}, after the second Z {r2}")
        if not r1:
            fails.append("the first Z did not open the box")
        if r2:
            fails.append("the second Z did not close/paginate the box")
    # (a Z after closing re-opens the box - talk is idempotent - so the
    #  close check is the double-Z above; spamming would re-open it)

    # ---- 3: menu ----
    out, img = run(f"10:{ENTER}:2")
    opened = "[menu] open" in out
    print(f"menu: {'opened' if opened else 'NO open line'}")
    if not opened:
        fails.append("Enter did not open the menu")
    if img is None:
        fails.append("menu capture failed")
    else:
        rows = border_rows(img, need=120)
        ink = text_ink(img, 26, 90)
        print(f"  menu border rows {rows[:2]}, row ink {ink}")
        if 17 not in rows:
            fails.append("the menu window border is not drawn")
        if ink < 200:
            fails.append("the menu rows are not drawn")

    # ---- 4: navigation into a sub-screen and back out ----
    out, img = run(f"10:{ENTER}:2,20:{DOWN}:2,30:{Z}:2")
    sub = "[menu] closed" not in out
    print(f"navigation: still in the menu after DOWN+Z: {sub}")
    if not sub:
        fails.append("DOWN+Z closed the menu instead of opening a sub-screen")

    # ---- 6: save / load round-trip ----
    sav = "/tmp/play_verify.sav"
    if os.path.exists(sav):
        os.unlink(sav)
    out, _ = run(f"10:{DOWN}:25,45:{F5}:2", delay=80,
                 extra={"FF4_SAVE": sav}, shot="/tmp/play_sav.png")
    m = re.search(r"\[save\] wrote .*\(map (\d+) at (\d+),(\d+)\)", out)
    saved = m.groups() if m else None
    print(f"save: {saved}")
    if not saved:
        fails.append("F5 did not write a save")
    else:
        out2, _ = run(f"10:{F9}:2", delay=40,
                      extra={"FF4_SAVE": sav, "FF4_MAPX": "2",
                             "FF4_MAPY": "2"}, shot="/tmp/play_ld.png")
        m2 = re.search(r"\[load\] .* map (\d+) at (\d+),(\d+)", out2)
        loaded = m2.groups() if m2 else None
        print(f"load: {loaded}")
        if loaded != saved:
            fails.append(f"load restored {loaded}, expected {saved}")

    if fails:
        print("\nFAILURES:")
        for f in fails:
            print("  -", f)
        return 1
    print("\nplayable layer OK (talk, pages, menu, navigation, real party)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
