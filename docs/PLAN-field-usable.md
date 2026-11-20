# Plan — field scene to "usable"

Goal of this phase: **you can start a new game from ROM data, walk out of
Baron, cross the world map, walk into Kaipo, talk to people with the real
windows, get a random encounter, open the menu, save and reload.** Nothing
beyond the field scene (no battle depth, no shops/inns/bugs) is required to
call it usable — those come after.

Everything below assumes the palette/sprite ground truth in
`docs/PALETTE_GROUND_TRUTH.md` (settled: 462/462 sprites pixel-identical).

---

## 0. Cross-cutting first: a fidelity harness  ← do this before features

Every past detour came from testing by eye. Build the harness once and every
later phase becomes a pass/fail number.

`tools/field_harness.py`

* boots the field engine headless with a **fixed seed** and a scripted input
  tape (`down x16, right x32, up x16, talk, ...` in frames);
* can start on any map/cell (`--map 0 --x 16 --y 30`);
* dumps, at chosen frames: framebuffer PNG, the logical state
  (map id, cell, facing, z, party HP/MP, gil, switches digest), and a
  `sha256` of the whole state;
* asserts against an expected JSON so a change either matches or is a
  deliberate, reviewable diff.

Acceptance: `python3 tools/field_harness.py --tape tapes/baron_walk.json`
reproduces byte-identical output twice in a row, and fails loudly on any
state change.

Engine support needed (small): `FF4_TAPE=` (comma-separated directions per
frame), `FF4_STATE_DUMP=` (JSON), `FF4_SEED=`. The field logic already lives in
`ff4_field_frame`, so the tape just feeds `FF4_Direction`.

---

## A. Map traversal — the field stops being one room

**Data already exported:** `mapTriggers1[0..255]` and `mapTriggers2[0..127]`
in `data/ff4-en-data.json` — 20 triggers for Baron, 7 for map 257, etc. Each is
`{x, y, map, xDest, yDest, direction, event}`. `triggerScript[256]` and
`eventScript[256]` are present.

| # | task | file |
|---|---|---|
| A1 | export per-map triggers to `data/submaps/<id>.trig` | `tools/ff4_palette_truth.py` style exporter |
| A2 | on entering a cell, look up the trigger → warp (dest map/cell/facing) | `world_view.c` |
| A3 | destination `>= 0xFB` = world map (251/252/253, and identify 254) | same |
| A4 | map stack: exit tiles (`props2 & 0x10`) pop the previous map | same |
| A5 | door tiles (`props1 & 0x10`): step-in then warp | same |
| A6 | arrival event id → run `triggerScript[event]` in the event VM | new `field_scene.c` |
| A7 | wipe in/out on transition (already a 32-frame curtain) | reuse |

Acceptance (harness): tape walks Baron (36) south to (16,31) → lands on world
map at (102,158) facing left; walking back returns to Baron (16,30). A second
tape walks out of Baron town (0) into its overworld link. No manual `W` key.

**Risk found:** `CheckTilePass` says floor is `props1 & 0x0A`, but that makes
most town floors solid; the engine currently uses `& 0x03`. This must be
settled here, because traversal quality depends on it. Plan: dump the real
`$06a1` values with the harness and fit the rule to observed walkability, then
pin it in `tests/test_world_rules.c`.

---

## B. New game from ROM data (kills the hardcoded party)

`src/platform/sdl2/sdl_main.c` currently does `ff4_party_add(... max_hp = 300)`.

| # | task | data |
|---|---|---|
| B1 | party roster from `CharProp[14]` (class, level, HP/MP, stats, exp) | rip |
| B2 | starting gear from `CharInitEquip[21]` (row = char−1) | rip |
| B3 | initial spells from `spellListInitial[13]` | rip |
| B4 | inventory (48) + gil from `$16A0` block | rip / init table |
| B5 | event switches `initEventSwitch` (256), NPC switches `initNPCSwitch` (512) | rip |
| B6 | treasure flags cleared | engine |
| B7 | start position from `CharProp` + map `0x24` (Baron), `x=16,y=30`, facing up | rip |
| B8 | config block: message speed, dash, sound | optional now |

Acceptance: harness dumps `state.json` for a fresh game; a new test
`tests/test_newgame.c` compares every field against `CharProp`/`CharInitEquip`
read straight from the rip, so a regression is caught without a human.

---

## C. Dialogue and windows from the ROM (the FF4 "feel")

Today: `draw_dialog` draws rectangles and text with `ff4.ttf`.

**C1–C3 are DONE as a verified reference implementation** — see
`docs/PALETTE_GROUND_TRUTH.md` and `tools/ff4_window_font.py`:
`WindowGfx` @0x57000 (frame + ASCII font), `WindowPal` @0x687D0, the three
`DlgTiles` strips @0xA76D8, 30 tiles wide / 26 text columns, tile number ==
text code, DTE expansion. `--demo` renders a pixel-correct box.

| # | task | source | state |
|---|---|---|---|
| C1 | window tile bank + 9-slice | asm + ROM | ✅ verified |
| C2 | font glyph bank | asm + ROM | ✅ same bank |
| C3 | glyph tile mapping + DTE | `charTable` / `DecodeDTE` | ✅ verified |
| C4 | engine draws the box from tiles instead of TTF + rectangles | `world_view.c` | ✅ done |
| C5 | wire VM `DIALOG`/`DIALOG2`/`DIALOG3` (reveal `$df`, paging, `\song`/`\wait`/`\line`) | `event_engine` | ☐ |
| C6 | speaker name / portrait variant | `DIALOG3` | ☐ |

Acceptance: a harness tape triggers Baron's guard line; the rendered window is
compared to a reference frame, and the text bytes equal `mapDialog[map][i]`
after DTE expansion. Keep `ff4.ttf` as a fallback behind `FF4_TTF=1`.

---

## D. NPC behaviour parity

Sprites and palettes are correct; the *logic* is still simplified (all NPCs
wander on a timer, no switch gating, no blocking).

| # | task | original |
|---|---|---|
| D1 | visibility gating from `switch0`/`switch1` + NPC switches | `CheckNPCSwitch` |
| D2 | wander driven by the RNG table (`rngTable`, `$78`), direction candidates, 1/0x80 split | `CheckMoveNPCs` |
| D3 | animation off the global `$7a` frame counter (already close) | `DrawNPCs` |
| D4 | NPC blocks the player's cell; `CheckNPCBlock` + `$0900` speed bump | `CheckNPCs` / `MoveNPCs` |
| D5 | face-the-player on talk (`$0901 bit 4`) | `CheckNPCs` |
| D6 | NPC scripts: `npcScript[npc]` on interact instead of static `.npc` text | `ExecNPCScript` |

Acceptance: harness runs 600 frames on Kaipo with a fixed seed; NPC cell
positions after N frames must equal a golden list captured from the first
correct run (determinism), and no NPC enters a wall.

---

## E. Encounters → battle handoff

`ff4_field_random_battle` exists in `field_engine.c` but nothing calls it in
the viewer.

| # | task |
|---|---|
| E1 | port the per-step counter from `mapBattleProbability` (+ `TriggerRateTbl`) |
| E2 | on encounter: flash/irised transition, then `ff4_battle_start` with the formation |
| E3 | on battle end: return to the same cell, restore scroll, keep step counter |
| E4 | boss/no-encounter flags on specific maps |

Acceptance: seed + N steps on a known map always yields the same formation id;
test asserts it against a value computed from the ROM tables.

---

## F. Menu and save

`menu_engine` exists and is tested (`tests/test_menu.c`) but is not reachable
from the field.

| # | task |
|---|---|
| F1 | `START` opens the main menu; world freezes (`$b1`) |
| F2 | status / item / magic / equip / config screens as read-only first |
| F3 | item use and equip write back to `FF4_GameState` |
| F4 | save/load: serialise `FF4_GameState` + map id/cell/facing + switches |
| F5 | one save slot, file in the user config dir |

Acceptance: open menu on the field, change equipment, save, reload, state hash
matches the pre-save hash.

---

## Order and rough size

| phase | why now | size |
|---|---|---|
| 0 harness | makes everything else measurable | S |
| A traversal | turns a room demo into a world | M |
| B new game | makes the run meaningful, removes hardcoding | S |
| C dialogue | the single biggest "is this FF4?" factor | M/L |
| D NPC logic | correctness of the world | M |
| E encounters | closes the gameplay loop | M |
| F menu + save | "usable" as a game, not a viewer | L |

Recommended first slice: **0 + A + B** (harness, traversal, new game). That
gives a genuinely playable Baron → world → Kaipo loop, and everything after
that can be verified by tape instead of by eye.

---

## Known blockers to keep in mind

1. **Collision rule** (A) — `0x0A` documented vs `0x03` behaviour; settle with data.
2. **Window/font art** (C) — banks not yet identified; this is the same class of
   problem as the palette one, so locate by byte pattern against the ROM, not by
   guesswork, and pin it in a `--verify` step.
3. **Map 254** (A) — appears as a warp target (Baron town triggers) but is not
   one of the documented world maps; decode before wiring.
4. **`$0fe5` / alternate NPC table** — `InitNPCs` can select a second
   `NPCPropPtrs` set (gfx id + 256). Not needed yet (Baron uses the first), but
   it will bite on event-driven maps; keep it in the plan for D.
5. **Sub-map size** — engine assumes 512×512/32×32; verify no map exceeds it
   before adding camera-relative features.


---

> The message box is now **locked** — full spec, guards and the three
> black-bar causes are in `docs/FIELD-MESSAGEBOX.md`.

## Message box rules (measured, do not regress)

Ground truth = a real snes9x frame captured with `tools/retro_dump.c`:

```
gcc -O2 -o /tmp/retro_dump tools/retro_dump.c -lz -ldl
/tmp/retro_dump /usr/lib/x86_64-linux-gnu/libretro/snes9x_libretro.so \
               ff2.smc /tmp/f2 2400 20 400     # press Start at frame 400
# frame_2100.png is the intro dialogue (screen 256x224, no scaling)
```

Measured on `frame_2100.png` (256x224):

| item | value |
|---|---|
| box x | **8 .. 247** (240 px = 30 tiles) |
| box tile-row origin y | **12** |
| top border white line | y = **17** |
| bottom border white line | y = **89** |
| navy body | y = 19 .. 87 |
| text lines | y = **27, 43, 59, 75** (pitch **16**), 4 lines max |
| text left x | **24** (box + 2 tiles) |

ASM confirmation (`field/window.asm`):
* `OpenDlgWindow` sets `$df = 1` and increments it **1..8, once per two IRQs**
  → `$df` is the number of **body** rows; the box is drawn in top to bottom.
* IRQ cut line = `$df*8 + 0x14`; the bottom border is a separate tile row below
  the body.
* `CloseDlgWindow` decrements `$df` to 0, after `$ec` clears the text.
* Strips: top `16/17/18`, body `19/FF/1A`, bottom `1B/1C/1D`; glyph tile == FF4
  text code.

Engine implementation (`world_view.c draw_dialog`):

1. Box anchored top-left: `x = 8`, tile-row origin `y = 12`.
2. Height = `1 + $df + 1` tile rows (`dlg_lines` 1..8).
3. **Sequence: window draws in (top→bottom, one row per frame) → the whole page
   of text appears at once → closing hides the text first → the box shrinks.**
4. **No typewriter.** Text is only drawn when `dlg_lines == dlg_lines_max`.
5. SNES space only: placement/scale come from `sc = zoom` over 256x224; the
   expanded view does not move or stretch the box.
6. Drawn last, above map/NPCs/actor/overhang.

Verified pixel positions in our render: white rows 17/90, white cols 9/246,
text bands 27/43/59 — within 1 px of the emulator frame (the 1 px difference at
the bottom comes from the game's IRQ scroll trick).

### Gotchas that produced a "black bar above the box"

1. **Texture blend mode.** `blit_win_tile()` copied each tile to its own
   texture; without `SDL_SetTextureBlendMode(..., SDL_BLENDMODE_BLEND)` the
   alpha was ignored and the transparent upper half of the top border tiles
   (`0x16/17/18` only draw rows 4..7) was written as opaque black — a 4 px
   black strip above the box. Blend mode is now set.
2. **Warp curtain.** `FF4_SHOT` used to fire while the 32-frame wipe was still
   closing. It now waits until the frame just drawn had `warp_frames == 0`, so
   captures are never taken through the curtain.
3. **Viewport outline.** A 1 px black debug rectangle was drawn around the
   256x224 viewport; the real game has none. Removed.

**Screenshots**: with those three fixed, `FF4_ZOOM=1 FF4_DIALOG=1` produces a
frame whose border lines (17/90, 9/246) and text bands (27/43/59) match the
snes9x capture within 1 px, with the map visible above the box.

## Field-scene backlog (the things you flagged), with status

| area | problem today | where | state |
|---|---|---|---|
| **Centering** | camera anchor is the measured `(112,109)` sprite with `cam = actor*16 - 112/-111`; in expanded view and at map edges the actor visibly drifts from centre, and `hide_bottom` clipping is not applied | `world_view.c update_camera`, `draw_actor` | needs a proper anchor + clamp matrix, plus a harness screenshot per orientation |
| **Maps** | background is correct (verified against `data/submaps/<id>.png`), but `byte2 & 0x04` (hide sprite) and `0x08` (hide bottom half) are only half-implemented; layer priority bit 13 (`walk under bridges/roofs`) is handled by the pre-baked overhang PNG instead of per-cell priority | `ff4_export_submaps.py`, `world_view.c` | medium |
| **Warps** | world↔town works; **cell triggers, door tiles, exit tiles, map stack are missing**; map 254 is an unknown warp target | `world_view.c` | ▶ next (data ready in `mapTriggers1/2`) |
| **NPC dialogue** | `.npc` carries `mapDialog[npcIndex]`, which is right for simple talkers but ignores `npcScript[npc]` (multi-page/conditional lines, `npcEventCalls`) and the `switch0/switch1` gate | `tools/ff4_palette_truth.py export_npc_files` + `world_view.c` | needs `npcScript` export + a real "talk" path |
| **Dialogue box** | now real BG3 9-slice + ROM font + DTE, 26-col wrap, reveal | `world_view.c draw_dialog` | ✅ done |

### Suggested order from here

1. ~~C4 window integration~~ **done** — `window_tiles_load()` + `blit_win_tile`
   now use the real SNES 2bpp layout (2 bytes/row), the strips in the correct
   order (top/middle/bottom), `color 0` transparent, and glyphs drawn from
   `ff4_ascii_to_code[]` generated from the rip's `charTable`.
   Check: `FF4_WARPTO=0 FF4_DIALOG=1 FF4_DIALOG_FULL=1 FF4_SHOT=... ./build/ff4_world`
2. **Centering**: fix the camera anchor and the `hide_bottom` clip; capture a
   screenshot per map size/edge and pin it in the harness.
3. **Warps (A)**: `.trig` export + cell triggers + exit/door tiles + map stack.
   This is what makes the field a world instead of a room.
4. **NPC dialogue (D6)**: export `npcScript` per NPC and run it, so talkers can
   have multi-page and conditional text.
5. **Maps polish**: implement per-cell priority + `hide_bottom` natively and
   retire the pre-baked overhang PNG.
