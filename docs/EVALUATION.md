# Project evaluation and the path forward

## Purpose

An unofficial, modern port of Final Fantasy IV built from the original ROM's
resources: same art, same maps, same feel, running natively at any resolution
with the fidelity of the SNES original. The field scene is the current target:
palettes, animated tiles, z-order (bridges, water), per-cell display priority
and transparency - all provably correct against the real game.

## Resources, and how each is used

| resource | role | rule of use |
|---|---|---|
| `ff2.smc` + clean-room ASM (`third_party/ff4`) | ground truth for every rule, table offset and routine | never guess: locate by byte-pattern, then pin the finding with a `--verify` step |
| FF4KSTER (`DEV/FF4KSTER_LINUX`) | a *working editor* - its decoders are reference implementations | cross-check ours; when they disagree, the emulator arbitrates |
| snes9x + `tools/retro_dump.c` | the arbiter: RAM/VRAM dumps, frame captures | anything visual/geometric is proven here or it does not ship |
| the rip JSON (`data/ff4-en-data.json`) | convenience bulk export | *not* authoritative - two of its decodes were already wrong (trigger groups, world tileset table) |

**Workflow that has kept us honest:** find the mechanism in the ASM → reproduce
it byte-for-byte in a Python tool → verify against an emulator dump → only then
wire it into the C engine → lock it with an automated guard.  When something
ships unproven, it gets reverted (that rule saved us three times: the palette
clip, the NPC hide, the animated overlay).

## Where the field scene stands

| goal area | state | proof |
|---|---|---|
| **palette usage** | **done** | 485/485 NPC sprites + 350/361 actors pixel-identical (`tools/verify_all_maps.py`), slot model from `LoadPlayerPal`/`LoadNPCPal` |
| **animated tiles** | **done** (this pass) | identification `0x120..0x12F` emulator-verified; per-phase composites; engine == composite, changes confined to animated cells (`tools/verify_animation.py`) |
| **z-order, bridges** | done | lane `$d2` tracked, not reset on bridges; `AND $0A` direct-page rule pinned by `tests/test_field.c` |
| **z-order, water pools** | partial | the actor draws correctly in water; the half-covered "wading" look still needs the `$090a`/OAM-priority decode |
| **display priority** | baked, working | `.over.png` (now phase-matched `.fover1..3`) draws `front` sub-tiles above the actor; verified by the sprite sweep (0 mismatches) |
| **transparency** | correct in the composite | BG1 colour-0 over BG2; a native tile renderer will make it runtime-true (see path) |
| **bridge z-order** | **done** | `DrawPlayerSub @b284/@b2d6`: `byte2&0x08` OAM split + LANE-AWARE hide; `tools/verify_bridge_z.py` |
| warps / map stack / trigger groups | done this pass | `tools/ff4_warp_audit.py`: ROM byte-level decode, 27 group-1-only maps recovered |
| **layouts from ROM** | **done** | `tools/ff4_layout_rom.py`: rip-gap layouts decoded from `sub_tilemap`, 27 maps recovered incl. warp target 27 |
| **layout groups (underground/moon)** | **done** | group 0 emulator-proven (airship == map 381, 386/390 cells); `.g1` variants auto-selected; `tools/verify_layout_group.py` |
| **world lava palette cycle** | **done** | `UpdateLavaPal` implemented + `tools/verify_lava_pal.py` |
| world water (overworld) | measured | slow dither re-step (~256 frames); needs the chunk composer -> native renderer |
| small-map centring | done | `tools/verify_submap_view.py`: engine view == PNG composite |
| message box, mosaic transition | done, locked | `verify_messagebox`, `test_dialog` (46 checks) |

## What is still missing (ordered)

1. **World-map water/lava rotation** - mechanism proven
   (`UpdateWaterAnim @8fa1` / `UpdateLavaAnim @8ee3`, `WaterShiftX/Y`,
   8-tile run rotated one position per frame, 16-step cycle; bank
   `0x80..0x9f` + foam `0xac/0xad`, confirmed by the V/B reports).  Implement
   the rotation over a mutable world tilemap.
2. **NPC visibility + scripts**: `CheckNPCSwitch`, the `$090a`/`$0edc` draw
   rule (which is also the "wading in water" look), `npcScript` on talk.
3. **Per-cell priority at runtime** (retire the bake): export a per-cell
   priority layer; draw pass order below-actor / actor / above-actor.
4. **Palette animation** (`UpdateLavaPal`, `TowerAnim*`, `UpdateZoomPal`) -
   CGRAM cycling; the underground lava tiles look flat without it.
5. **Native tile renderer** - draw tiles at runtime instead of compositing
   PNGs: unlocks arbitrary resolutions (the "modern features" part), true
   per-cell transparency, and makes 1-5 fall out naturally.

## The architecture decision (read this before extending the renderer)

`docs/ARCHITECTURE.md` records the pivot decided after the bridge z-order
bug: the PNG bakery (`.over/.f1-3/.g1` variants) is to be replaced by a
schema-driven, hardware-shaped tile renderer (per-cell priority passes),
with a deterministic core behind a `tick(input)` API - the zelda3 /
Balthasar / decomp pattern.  New features should land as schema entries +
renderer rules, not as new file variants.

## Checking in

`./tools/verify_all.sh` runs every guard (~35 s, or full mode with the
~10-minute sprite sweep).  All green = the field scene is intact.  Guards:

| guard | covers |
|---|---|
| `unit_tests` / `ctest` | dialog geometry, collision maths, warp data |
| `palette_tables` / `window_font` / `anim_gfx_data` | ROM table decodes |
| `message_box` | text box geometry vs the reference |
| `layouts_from_rom` | the ROM layout decode == the rip everywhere |
| `warp_audit` | triggers/warps byte-level + reachability |
| `field_animation` | per-phase animation == composites |
| `submap_view` | engine view == PNG composite (centring, overlays) |
| `world_tile_report` | the V-key world reporter + your recorded cells |
| `sprite_sweep` | 488/488 NPC sprites, 369/383 actors, 0 mismatches |

## Verification discipline (unchanged, restated)

* every engine run in scripts is bounded (`timeout`) and ends via
  screenshot/quit - no open-ended loops;
* a feature without a guard is not done; a guard that cannot fail is not a
  guard;
* the emulator arbitrates every disagreement between our code, the rip, and
  FF4KSTER.
