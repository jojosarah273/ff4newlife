# TODO — field scene

Ordered. Everything at the top of a section is blocked on the item above it.

---

## 1. ~~Animated tiles~~ **DONE**

Runtime: per-phase map composites `<id>.png + <id>.f1..f3.png` (185 maps),
swapped every 8 frames exactly like `TfrBGAnimGfx @cb5f`
(`phase = ($7a & 0x18) >> 3`).  Compositing per phase keeps BG2 under BG1 by
construction - the failed overlay approach is gone.  Animated *front* tiles
(waterfalls, ts3 t69 / ts5 t51-52) get phase-matched overhangs
`<id>.fover1..3.png`.  `FF4_NOANIM=1` pins phase 0 for comparisons.
Guards: `tools/verify_animation.py` (engine == composite per phase, changes
confined to animated cells, natural cadence, NOANIM control stable).

Remaining animation work is *world* water/lava (item 1a) and *palette*
animation (item 5).

## 1a. ~~World-map water~~ MEASURED - slower than expected, deferred

Dense VRAM sampling while STANDING on the world map (frames 1050-1470,
`FF4_VRAM_EVERY=2`) shows the truth: the water "wave" is a **dither
re-step - one visible rotation per ~256 frames per buffer** (the earlier
"sweeping wave, 15 entries/frame" was the intro flyover SCROLLING, not
water).  Mechanism confirmed: `LoadWaterGfx` caches the 8bpp water chunk
tiles (mode-7 tiles 0x100-0x103 at VRAM 0x4000, and 0xF4-0xF7 at 0x3D00),
`UpdateWaterAnim` rotates 8-byte runs per `WaterShiftX/Y`, `TfrWaterGfx`
re-uploads.  Byte-exact reproduction needs the runtime CHUNK COMPOSER (the
0x80/0x25 double-dither art the game builds at VRAM 0x4000+), which is part
of the native tile renderer (item 5).  The VISIBLE underground lava glow
(`UpdateLavaPal`) IS done - see `docs/ACCOMPLISHED.md` 7f.

## 1b. ~~The empty-layout maps~~ **DONE** (see `docs/ACCOMPLISHED.md` 7e)

BONUS, also done: the layout GROUPS.  `mapLayouts2` is the group-1
(underground/moon) layout table - `DecodeSubTilemap @feba` selects it via
`layoutMSB | $1701`, the same `$1701` as the trigger groups.  The engine now
renders `<id>.g1.*` automatically when entering from the underground/moon
(127 maps exported).  Group 0 is EMULATOR-PROVEN: the intro airship scene ==
map 381, 386/390 cells of its VRAM tilemap.  Guard:
`tools/verify_layout_group.py`.

The rip's mapLayouts1/2 are EMPTY for every layout whose data range crosses a
ROM bank boundary (63 of 384 ids, 27 maps affected, map 27 a live warp
target).  `tools/ff4_layout_rom.py` decodes the `sub_tilemap` segment straight
from the ROM (`SubTilemapPtrs` @17/8000, ff4-map RLE: value = byte&0x7f,
count = next+1), matches the rip byte-for-byte on all 308 non-empty entries,
and fills the gaps -> `data/layout_rom.json`, used by the exporter
automatically.  Also: `mapLayouts2` is the GROUP-1 layout table (ids 0..127,
base 18/8000, selected by layoutMSB or entering from underground/moon) - the
engine still renders group-0 layouts everywhere, which is wrong for
underground towns; wiring group selection is part of item 3 (NPC/entry
variants).

## 2. Arrival events

Trigger records carry an `event` id (e.g. Baron `(16,31)` → event 102). Today it
is only logged. It must start `triggerScript[event]` through the event VM so
first-visit scenes and scripted map changes happen.

Acceptance: warping into Baron town runs event 16; the intro sequence advances.

---

## 3. Per-cell priority (`pri`)

The overhang layer is currently a pre-baked PNG from the sub-sprite `front` bit.
Replace with the tilemap priority bit (`0x2000`) tracked **per cell and per
layer**, so bridges, archways and roofs occlude exactly where the game does.

Acceptance: walk under a bridge and an archway; the sprite is hidden only while
the priority cell covers it.

---

## 4. NPC visibility and scripts

* `CheckNPCSwitch` gating (`switch0`/`switch1` vs the NPC switch table
  `initNPCSwitch`) — today only two hardcoded story NPCs are filtered.
* The per-NPC draw flag `$090a` / per-cell table `$0edc` (this is also what
  decides hidden/behind-sprite NPCs).
* `npcScript[npc]` on talk, instead of the static `mapDialog` line.
* The alternate NPC set selected by `$0fe5` bit 7 / `$1701` (gfx id + 256).

Acceptance: NPCs appear/disappear exactly as the story advances; talking runs
multi-page and conditional scripts.

---

## 5. Palette animation

`LavaAnimPal` / `TowerAnimBluePal` cycle CGRAM entries. Needs the same treatment
as tile animation: locate, verify, and drive from the frame counter.

---

## 6. Gameplay loop

| item | state |
|---|---|
| random encounters (`mapBattleProbability`, `TriggerRateTbl`) | logic exists in `field_engine.c`, not wired to the viewer |
| battle handoff and return to the same cell | not wired |
| menu reachable from the field (`menu_engine`) | not wired |
| save / load `FF4_GameState` | none |
| new game from ROM (`CharProp`, `CharInitEquip`, switches) | party still hardcoded in `sdl_main.c` |

---

## 7. Harness (do before the big items)

Note: the current per-map sprite sweep is occasionally flaky because it relies on
a screenshot fired N frames after the map transition (`FF4_SHOT_DELAY=20` now).
A real harness with state hashes would remove that class of noise.

`tools/field_harness.py` — tape-driven headless runs with a fixed seed, state
hash per frame and golden comparisons. Warps, collision, NPC cadence and the
message box all have ad-hoc checks now; a single harness would make every future
change provable instead of eyeballed.

---

## Done in this pass (warps + small-map rendering)

* **Duplicate tiles over sub-maps — FIXED.** The `.over.png` (and the opt-in
  anim) overlays were blitted with the *unclipped* source rectangle.  Centring
  a small room makes the camera negative, so the overlay slid relative to the
  map: the room's `front` sub-tiles appeared painted a second time, slightly
  offset (Baron's shops and houses).  The overlay art itself is pixel-identical
  to the map where it is opaque (verified: 8448/8448 px equal), so it was purely
  a positioning bug.  All overlays now use the same clipped rect as the map.
  Guard: `python3 tools/verify_submap_view.py` compares the engine viewport
  against the PNG composite for walkable cells, ignoring sprite pixels.
* **Small maps are centred** on their content box with black around them, in
  classic and expanded view (`docs/TODO.md` item was "per-cell priority").
* **Warps**: `MapTriggers2` is trigger *group 1* for maps 0..127 (not maps
  256..383) — 27 maps had no warps at all without it; the map stack is now
  pushed on world->submap and pops back onto the world; chest (`0xFE`) and
  script (`0xFF`) records are no longer treated as warps.
  Guard: `python3 tools/ff4_warp_audit.py` (byte-level ROM decode, reachability).
