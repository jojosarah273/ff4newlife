# Architecture: the uniform path to a modern FF4

Written after the bridge z-order fix (the `byte2 & 0x08` OAM split + the
lane-aware hide).  That bug is the canary: it existed *because* our renderer
bakes per-cell rules into static PNGs.  This document is the rethink the
project needs, and the plan that follows from it.

## What the exemplar projects teach

| project | the lesson we take |
|---|---|
| **zelda3** (snesrev) | THE model for SNES reimplementation: portable C that loads the original ROM and *interprets* its data at runtime; a hardware-shaped renderer (BG layers with per-tile priority bits, OAM sprites with priority) so z-order/priority/transparency are **structural, not special-cased**; deterministic sim validated by input replays; one SDL frontend, trivially ported (Emscripten, Switch, ...).  ROM hacks "just work" because data is read, not baked. |
| **Balthasar** (Chrono Trigger) | same shape: assets lifted from the ROM into a uniform intermediate form, engine rebuilt natively around them.  The intermediate form is the contract between "ripping" and "engine". |
| **pokered / pokecrystal + the gen-1 modern ports** | the *decompilation* discipline: every table named, every structure typed, the disassembly IS the documentation.  Any port consumes the same named data; nothing is hand-transcribed. |
| **the Recomp movement** (N64-era, and PortO2-style shims) | hard **core/platform separation**: a deterministic `core_tick(input) -> frame` with zero I/O, and thin platform shells (SDL2, Android, Emscripten).  Save states and replay tests fall out of determinism. |

The common thread: **one data layer shaped like the hardware, one renderer
shaped like the hardware, one deterministic core** — features become data,
not code paths.

## Honest audit of where we are

What we did right (and must keep):
- **Verification-first development.**  Every mechanism was found in the ASM,
  reproduced byte-exactly, arbitrated by the emulator, then locked with a
  guard.  14 guards, 488/488 sprites, emulator-proven trigger groups, layout
  groups, animation clock, lava palette, bridge z-order.  No exemplar project
  has our emulator-arbitration discipline; this is our crown jewel.
- Named-table ROM reading (`MapAnimGfx`, `LavaAnimPal`, `MapTriggers1/2`,
  `SubTilemap1/2`) — already the decomp discipline, scattered.

Where we diverged (the user's word: haphazard):
- The render path is a **bakery**: per-map 512² PNG composites, `.over.png`,
  `.f1..3.png`, `.fover1..3.png`, `.g1.png` variants of all of those,
  `.walk/.props/.dirs/.tiles` sidecars — each new mechanism (groups, phases,
  splits) multiplied file variants, and per-cell runtime rules (bridge
  split, lane-aware hide) had to be bolted on with clipping tricks.
- Two renderers (sub-map PNG blit + world mode-7 PNG) with separate bugs.
- The "engine" and the "viewer" are the same binary; logic and presentation
  are interleaved in `world_view.c` (~4000 lines).

The bridge bug is the proof: SNES priority is a *per-cell property of the
tilemap*, and our bake turned it into a whole-map overlay that then needed
surgical fixes.  The uniform fix is structural.

## The target architecture

### 1. `ff4data` — one ROM reader, one schema (no per-map files)

A single library/tool that loads the ROM and exposes typed, named data:

```
World{id, tiles[256x256 cells], tileset, palette, warps, animated bank...}
Map{id, tileset, palette, layouts[2 groups][1024 cells], props[2 bytes/cell],
    npcs[], triggers[2 groups], treasure records, script hooks...}
Tileset{id, flagTable[128x4 sub-tiles], sheet, animatedSlots,
        animatedPhases[4][16 tiles]}
Anim{field: MapAnimGfx phases + $7a clock;  world: WaterShiftX/Y rotation;
     palettes: LavaAnimPal, TowerAnim*, WhalePal, ZoomPal}
```

Every table located once, by byte pattern, with the ASM symbol name
preserved.  ROM hacks and retranslations then "just work" — the same reader
consumes a different ROM.  The current Python exporters become *debug dumps*
of this schema (and stay as verification artifacts), not the engine's food.

### 2. A hardware-shaped field renderer (per-cell, not baked)

Draw order mirrors the SNES, per frame, from the schema:

```
BG2  (backdrop, per-cell transparent)
BG1  sub-tiles with priority 0
sprites with OAM priority low      (actor halves per byte2&0x08, NPCs)
BG1  sub-tiles with priority 1    (front tiles - roofs, bridge edges)
sprites with OAM priority high     (actor top pair on 0x08 cells)
HDMA-ish effects                   (mosaic warp, dialog window)
```

With this, everything the last weeks special-cased becomes structural:
bridge split, lane-aware hide, per-cell transparency, animated tiles (just
swap the tile's phase graphic — no `.f1..3.png`), layout groups (pick
`layouts[group]` per cell draw — no `.g1.png`), world water (mutable
tilemap + rotation), arbitrary resolutions (tile-scaled, not one texture).
`world_view.c`'s clip-rect surgery gets deleted, not extended.

### 3. Deterministic core + platform shell

```
ff4_core_tick(&state, &input) -> frame   /* portable C, no I/O */
```

Field logic (lanes `$d2`, counters `$7a/$7b`, trigger groups `$1701`, the
event VM we still need to write) lives in the core; SDL2 is one shell among
future shells (Android, Emscripten).  Determinism gives replay tests and
save states — and a new class of verification: record an emulator input
sequence, replay it in the core, compare state hashes per frame.

### 4. Verification, upgraded but unchanged in spirit

- Pixel guards keep locking regressions (they compare engine frames).
- Structural guards get stronger: with the tile renderer we can compare
  against emulator VRAM *structurally* — cell ids, tile graphics, palette
  words — the way the layout-group proof (airship == map 381, 386/390 cells)
  and the animation-clock proof (4 successive phases) already did.

## Migration plan (guards stay green at every step)

**Status: Phase A DONE, Phase B DONE for sub-maps** (this session).

* `tools/ff4data.py` is the schema reader + pack emitter
  (`ff4data --verify`: packs reproduce the reference PNGs, 0 px, both
  layout groups and all four phases).  Along the way it exposed a real
  exporter bug: every `.g1.png` had been rendered with the group-0
  backdrop (`export_one` rebound its `bg2` parameter) - fixed and
  re-exported.
* The engine now renders sub-maps per cell from the packs:
  BG2 pass (opaque atlas) -> BG1 non-front pass -> sprites ->
  BG1 front pass -> high-priority sprite halves, exactly the SNES
  order.  Animated slots come from atlas row 16 (4 phases);
  layout groups come from the pack's group-1 cells.  The baked PNGs
  are no longer loaded by the engine - they remain the guards'
  references.  `FF4_NOPACK=1` forces the old PNG path for A/B.
* Verified: all 14 guards green on the pack renderer, a 5-map A/B is
  byte-identical to the PNG renderer, and the full sprite sweep stays
  488/488 + 0 mismatches.

Remaining on this plan: retire the PNG fallback once the world map
adopts the same packs; move field logic behind `ff4_core_tick`; the
event VM; battle/menu on the schema.

- **A. Schema first.**  Fold the Python tools' decoders into one
  `ff4data` module (Python for now; port to C when the core splits).  Emit
  the existing PNGs as *derived debug output*.  Guards unchanged.
- **B. Renderer swap, one layer at a time.**  Sub-map BG1 priority-1 pass
  replaces `.over.png` (drawn from the tileset flag table per cell);
  then the BG passes replace the 512² composite; then the world renderer
  adopts the same tile blitter (mode-7 chunking can wait).  After each
  swap, the pixel guards must still pass — they are the migration's
  contract.
- **C. Core split.**  Move the field state machine out of `world_view.c`
  into `ff4_core` with the tick API; SDL becomes a shell; add replay
  tests against recorded emulator input.
- **D. Scenes.**  Battle and menu reuse the schema (their tables are
  already in the rip and the disassembly) and the renderer's sprite
  machinery.

## Why this is the FF4 answer, not a detour

Every mechanism we bled for — trigger groups, layout groups, lane hides,
OAM splits, phase clocks — is a *per-cell runtime rule* in the original.
The SNES hardware expressed them uniformly; our bakery expressed them as
file variants and patches.  Making the engine data-shaped and
hardware-shaped is not a rewrite of what we learned: it is the *container*
for everything we proved, and it is what makes "modern FF4 from ripped
assets, portable everywhere" literal rather than aspirational.
