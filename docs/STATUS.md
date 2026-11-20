# Status — what is verified and what is next

* **Full evaluation + roadmap:** `docs/EVALUATION.md`
* **One-command self-test:** `./tools/verify_all.sh [quick]`
* **What is done and how to prove it:** `docs/ACCOMPLISHED.md`
* **What is next, in order:** `docs/TODO.md` (starts with animated tiles)

Last pass: collision + z + NPC cadence + warps. Every guard below is green.

## Verified, with the tool that proves it

| area | verification |
|---|---|
| NPC/actor sprites + palettes | `python3 tools/verify_all_maps.py 0 383` → **462/462 sprites, 350/361 actors pixel-identical**; 0 mismatched |
| palette tables | `tools/ff4_palette_truth.py --verify` (MapSpritePal @0x68000 etc.) |
| window + font | `tools/ff4_window_font.py --verify`, `build/test_dialog` (46 checks), `tools/verify_messagebox.py` |
| animation data | `tools/ff4_bg_anim.py --verify` (MapAnimGfx @0xFB9C0, staging, 16/16 tilesets) |
| tile animation identification | `tools/ff4_anim_analysis.py` — every animated tile in the game (graphic index 0x120..0x12F), emulator-verified source (`docs/FIELD-ANIM-ANALYSIS.md`) |
| tile animation runtime | **ON by default** — per-phase composites, 8-frame cadence (`TfrBGAnimGfx`); `tools/verify_animation.py`: engine == composite per phase, 0 px |
| collision rule | `tests/test_field.c` pins `passable = next & ((lane & 3) + 4)` (the `AND $0A` DP variable — see `docs/FIELD-COLLISION.md`) |
| warps | `tools/ff4_warp_graph.py` (979 triggers → 450 real warps) + a walked traversal |
| map transition | mosaic ramp verified frame-by-frame (`FIELD-WARPS.md`) |
| unit tests | `cd build && ctest` → 13/13 |

## Done in this pass

1. **Collision was wrong in every previous doc.** `CheckTilePass` uses the
   direct-page variable `$0A = (lane & 3) + 4`, not the immediate `#$0A`.
   Fixed in `ff4_field_tile_pass_bytes()` and in the viewer, with tests.
2. **Standing lane (`$d2`)** tracked per step and *not* reset on bridge tiles;
   initialised from the spawn cell. Bridge crossing verified by tape
   (`(18,14) z=2 → (18,13) bridge → (18,9) z=2`).
3. **`byte2 & 0x04`** hides the *actor* sprite (verified from `DrawPlayerSub`);
   **`byte2 & 0x08` is OAM priority, not a clip** — the pixel verifier rejected
   my first attempt, so it is documented and reverted.
4. **NPC movement cadence** now uses the ROM tables `_00c117 = {1,1,1,2}` px/frame
   and `_00c11b = {1,2,4,8}` repath accumulator (`$08ff & 0x3F`), and a blocking
   NPC switches to speed index 2 so it steps aside.
5. **Warps**: `.trig` export, cell triggers, map stack for exit tiles, world
   mapping 251/252/253; 254/255 classified as spare-world/event records and
   skipped. Verified: Baron town `(16,31) → map 251 (102,158)`.

## Next, in order

1. ~~Tile animation~~ **done** — FF4KSTER's decoder is authoritative:
   animated iff graphic index `0x120..0x12F`, phase tile
   `animated_offset + (major*16+minor+frame*4)` (3bpp), phase every 8 frames.
   Static map = frame 0, overlays `<id>.anim0..3.png`, verified by
   `tools/verify_animation.py`.
2. **Arrival events** — the trigger's `event` id should run through the event VM
   (gates first-visit scenes and map changes).
3. **Per-cell priority (`pri`)** — replace the pre-baked overhang PNG so
   bridges/archways occlude per cell.
4. **NPC visibility** — `$090a` / `$0edc` decode, `CheckNPCSwitch` gating, then
   `npcScript` on talk instead of static text.
5. **Palette animation** (`LavaAnimPal`, `TowerAnimBluePal`).

## Screenshot helpers

```bash
FF4_TRACE=1            # [actor] cell/z, [trig], [vp] geometry
FF4_TRACE2=1           # [land] cell + trigger id + props2
FF4_DEBUG=1            # debug-font banners (their 8x8 atlas is a wrong bank)
FF4_ANIM=1             # experimental tile animation (unverified)
```
