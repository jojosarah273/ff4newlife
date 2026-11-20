# The FF4 keystones — every resource, what it's authoritative for, and how we use it

There are far more FF4 assets in `~/DEV` than the engine has been using.
This inventory lists each one, what it is *authoritative* for, current use,
and untapped value.  Last audited at the start of the world-map pack work
(step 1), together with the torch/flip audit.

## 1. The ROM itself

| item | path | authority |
|---|---|---|
| FF2 US v1.1 (headerless) | `ff2.smc` | THE ground truth.  Every table we locate is byte-searched here. |
| debug ROM | `ff2-debug.smc` (also `~/DEV/`) | a patched/trace ROM from earlier sessions; useful for breakpoint-style verification, barely tapped. |

## 2. SNES clean-room disassembly — `third_party/ff4` (== `~/DEV/FF4_FROM_SOURCE/ff4`, byte-identical)

Our primary logic reference: labeled routines (`DrawPlayerSub`, `DecodeSubTilemap`,
`TfrBGAnimGfx`, `UpdateLavaPal`, `CheckTilePass`...), data segments with
symbol names, and the .map with exact bank/offsets (`ff4-en.map`).
**Used constantly.**  Untapped: `event.asm` (the event VM we still need to
write), `battle/`, `menu/`, `vehicle.asm` (airship modes), `rand_battle.asm`.

## 3. FF4KSTER — `~/DEV/FF4KSTER_LINUX/ff4kster`

A *working editor*: authoritative for any display/format question where our
render and the game disagree, because its decoders have been user-tested for
years.  Provenance: the tile flag-table decode (sub-tile flags, flips,
`IsAnimated()`), the sheet map, the trigger-group structure.
**Caution (proven by the torch audit):** FF4KSTER's graphics loader reads
columns LSB-first while hardware is MSB-first, and its flip convention
(flip when flag bit6 CLEAR) compensates.  Copy either half alone and art
inverts.  Our animated-tile decode did exactly that (mirrored torches);
fixed, with the proof chain in `docs/FIELD-ANIM-ANALYSIS.md` and a
regression guard in `verify_animation.py`.  Convention must be proven
against `CopyTilemapToBuffer @f872` (the game's own tilemap word builder),
never assumed from a working editor.

## 4. snes9x + `tools/retro_dump.c` (our harness)

THE arbiter.  `FF4_VRAM`/`FF4_VRAM_EVERY` (dense sampling), `FF4_RAM`,
scripted input (`press_start_at/every`, hold-direction).  Provenance: layout
groups (map 381 airship == group 0, 386/390 cells), the animation clock
(4 successive phases), staged animated tiles, world-water cadence, the
lava palette slot.  This is the only tool that can catch *shared* bugs (where
engine and exporter agree with each other but not with hardware — the torch
flip being the live example).
`~/DEV/SNES9X/snes9x` = the emulator binary used by the harness.

## 5. PSX decompilation — `~/DEV/FF4_PS1_DECOMP`  ⭐ *largely untapped*

A **100% C-written (2516/2516), 827-byte-verified decompilation of the PS1
port** (Tose's rebuild, SLUS_013.60), with a Phase-B native port
(`port/` → `build/ff4-native`), a psxrecomp oracle, and extracted data packs:

* `port/data/{dialogue,battles,characters,cinematics}.json` — decoded game
  content, generated from the *SNES* rip JSON (`tools/rip/content_pack.py`)
  → **direct source for our event/dialogue work**.
* `port/src/{event,battle,menu,shop,sprite,db}` — interpreted C modules with
  semantic names (`port/docs/functions.md` catalogs 1859/2516): the event
  queue, battle flow, menu rows — a readable reference for the same game
  logic our SNES 65816 disassembly expresses.
* `reference/` psxrecomp build — runnable oracle for behaviour questions.
* Uses for us: (a) event VM semantics (the PSX eventq is a C transcription of
  the same scripting), (b) battle/menu logic in C rather than asm,
  (c) cross-checking table semantics (the PSX port kept the data model).
  Not authoritative for SNES *graphics formats* (PSX assets re-encoded).

## 6. snesrecomp output — `~/DEV/FF4_FROM_SOURCE/ff4recomp` ⭐ untapped

An snesrecomp-generated C recompilation of the **SNES** ROM:
`generated/bank*.c` (static C per bank), `config/*.cfg` (function
boundaries), `snes9x/runner`.  A second, *machine-level* view of the same
ROM our disassembly documents.  Uses: cross-checking 65816 control flow,
indirect-dispatch resolution (jump tables the disassembly labels poorly),
and eventually a bridge if we want hardware-register-accurate behaviour.

## 7. FreeEnterprise ecosystem — `~/DEV/FE_GOLBEZ`, `~/DEV/FREEENTERPRISE_APP`, `~/DEV/FE_ANDROID_TEST`, `~/DEV/FE_LINUX_ARCHIVE`, `~/DEV/FE_APK_ARCHIVE`

* `FE_GOLBEZ/f4c` — a **compiler/assembler for FF4 ROM patches**
  (`compile_event.py`, `compile_ai_script.py`, `compile_actor.py`, ...) plus
  `rescripter.py` (event-script rewriter).  This is a *complete, working
  event/AI script encoder* — i.e. the event VM's opcode table, already
  decoded by someone else.  ⭐ Big shortcut for step 3 (the event VM).
* `FE_GOLBEZ/fetools` (`z_tools.py`, `asset_tools.py`, `harp` music tools) —
  asset patch tooling, sprite import (`import_z_sprites.py`).
* `FREEENTERPRISE_APP` — an Android/desktop app (Kotlin/Gradle) with an
  FF4-native UI design doc + the game font — a proven Android delivery
  pipeline and design reference for our eventual platform shell.
* `FE_ANDROID_TEST/*.apk` — built APKs, i.e. the Android toolchain works.

## 8. HSS_SNES — `~/DEV/HSS_SNES`

RM2K3-style SNES RPG maker (pvsneslib Mode 3): map/charset editor, actor
system, warps, message box, fades.  Value to us: (a) it already solved
"engine-side" map/actor/event data formats in Python — a design reference for
our pack schema; (b) its editor UI is a model for a future FF4 map tool.

## 9. Others in `~/DEV`

* `MINISHCAP`, `SOTN_ANDROID` — other porting projects; reference material
  only.
* `ARCHIVE`, `HSS_GAME`, `HSS_ITEMTOOL` — earlier experiments.

## 10. Our own stack (current engine)

* `tools/ff4data.py` — the uniform schema/pack emitter (sheets, flags,
  palettes, anim, `data/maps/*.ff4map`).
* `tools/ff4_export_submaps.py` / `ff4_export_worldmap.py` — PNG references +
  the ROM layout decode (`ff4_layout_rom.py`).
* `tools/ff4_warp_audit.py`, `ff4_anim_analysis.py`, `ff4_world_anim_report.py`,
  `ff4_palette_truth.py`, `ff4_window_font.py`, `ff4_bg_anim.py` — table
  decoders, each with `--verify`.
* Guards: `verify_all.sh` → 15 checks; `retro_dump.c`; `refs/` emulator
  captures; `data/world/water_report_raw.txt` (your V/B reports).

## How this changes the plan

1. **The event VM (step 3) should start from FE's `f4c` opcode tables + the
   PSX `port/src/event/` C modules**, with the SNES `event.asm` as the
   byte-exact authority — three sources for the same semantics.
2. **Battle (step 5) has a C reference**: the PSX `port/src/battle/` +
   `port/data/battles.json`, cross-checked with the SNES `btlgfx/` asm.
3. **Android delivery (step 7)** already has a proven pipeline in
   FREEENTERPRISE_APP; our `ff4_core_tick` split slots into it.
4. **The flip audit** (in progress) must be settled against VRAM/hardware —
   FF4KSTER's loader cannot be trusted on bit order (it reads LSB-first).
