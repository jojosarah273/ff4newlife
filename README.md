# FF4: New Life

A modern, portable re-implementation of **Final Fantasy IV** (SNES, 1991) —
built from the original ROM's data and disassembly, verified against the
real game at every step. The goal is a faithful engine that runs natively
on desktop and Android, driven by the original assets.

> **Status:** the field scene is complete and byte-verified — world maps,
> 383 sub-maps with layout groups, tile animation, warps, the message box,
> NPC and actor sprites, and the bridge z-order, all pinned by an automated
> 16-guard verification suite (plus a full 383-map sprite sweep:
> **338/338 NPC sprites pixel-identical to the ROM reference, 0 mismatched**).
> The event VM, battle and menu scenes are next (see `docs/TODO.md`).

## Why this exists

Most FF4 "ports" are emulators. This project instead asks: *can the game
itself be rebuilt as portable C, with the original hardware behaviour —
not an approximation?* Every rule in the engine is derived from the
original 65816 assembly and checked against emulator ground truth:

- **The emulator is the arbiter.** Anything both the exporter and the
  engine agree on can still be wrong the same way — shared-convention
  bugs are only visible against real hardware output (see
  `docs/ACCOMPLISHED.md`, the "torch flip audit").
- **A feature without a guard is not done; a guard that cannot fail is
  not a guard.** `tools/verify_all.sh` runs the whole suite
  (quick mode ≈ 1 min, full ≈ 35 min).
- **Revert rather than ship unverified.**

## The architecture (docs/ARCHITECTURE.md)

One data layer shaped like the hardware, one renderer shaped like the
hardware, no per-map special cases:

```
ROM → ff4-en-data.json ──► tools/ff4data.py ──► data/ packs
      (rip: tables, layouts, props)              ├─ data/maps/<id>.ff4map
                                                 │    cells + BG2 + groups
                                                 ├─ data/world/<w>.ff4world
                                                 └─ data/gfx/{sheets,anim,
                                                      tileflags,palettes}
engine: per-cell renderer = BG2 → BG1 non-front → sprites → BG1 front
        (the priority passes of the real PPU order)
```

Highlights, all traced to the disassembly and measured on snes9x:

| Feature | Source of truth |
|---|---|
| Field tilesets, flags, palettes | `TILESET`/`PALETTE`/`TILEPROP` tables |
| Tile animation (4 phases, 8-frame cadence) | `TfrBGAnimGfx @cb5f`, VRAM-verified |
| Map layouts incl. underground/moon group | `DecodeSubTilemap @feba`, `$1701` |
| Warps and cell triggers | `field/trigger.asm`, curtain at midpoint |
| Actor sprite frames | `PlayerSpriteTiles @c0c4`, `DrawPlayerSub @b284` |
| Bridge z-order (head above, feet behind) | OAM priority split, `byte2 & 0x08` |
| Message box geometry | measured on a real snes9x frame |
| World water/lava shimmer | `UpdateLavaPal @c4cd`, `LavaAnimPal` |
| NPC sprites and palettes | `NPCSpriteTbl @f846`, `MapSpritePal` CGRAM slots |

## Build and run

```sh
cd build && cmake .. && make        # or: cmake --build build
./tools/verify_all.sh quick         # the guard suite

./build/ff4_world                   # world view + sub-maps, interactive
# useful knobs:
FF4_WARPTO=<map>  FF4_MAPX=<cx> FF4_MAPY=<cy>   jump to a sub-map cell
FF4_WORLD=0|1|2                     overworld / underground / moon
FF4_SHOT=out.png FF4_SHOT_DELAY=25  bounded headless screenshot
FF4_TRACE=1 FF4_AUTOWALK=down       headless traversal
FF4_DIALOG=1                        the message box
```

Headless guard runs use `SDL_VIDEODRIVER=dummy`; every engine run in the
tooling is bounded (timeout + screenshot + quit).

## Repository layout

```
docs/        design notes, incident post-mortems, keystone inventory
tools/       exporters + the guard suite (python, ROM-driven)
src/         engine (SDL2 front-end; core split from platform)
include/     core headers incl. the measured dialog geometry
data/        the exported asset packs (derived from the ROM)
tests/       unit tests (dialog geometry, collision masks, bundles)
third_party/ reference disassembly (see its own notes)
```

## Legal

This project contains no ROM images and cannot run without a legally
obtained *Final Fantasy IV* SNES ROM (place `ff2.smc` in the repository
root — it is gitignored). Final Fantasy IV is © Square Enix; this is a
non-commercial fan effort for study and preservation. The derived asset
exports in `data/` are provided in the spirit of other decompilation and
modding projects and will be withdrawn on request.
