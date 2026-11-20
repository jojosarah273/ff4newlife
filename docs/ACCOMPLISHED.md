# What is accomplished (and how to prove it)

Every row has a command that fails if the behaviour regresses.

## 1. Warps — maps are connected

* Cell triggers from `MapTriggers1/2` (`field/trigger.asm`) exported per map to
  `data/submaps/<id>.trig`; the party warps when it **enters** a trigger cell.
* Exit tiles (`props2 & 0x10`) pop a small map stack; targets `>= 0xFB` map to
  world 0/1/2 (251/252/253); 254/255 are classified as spare-world/event
  records and skipped.
* Arrival direction is applied; the arrival `event` id is logged (wiring it to
  the VM is TODO #2).

```
python3 tools/ff4_warp_graph.py         # 979 triggers → 450 real warps
FF4_TRACE=1 FF4_WARPTO=0 FF4_MAPX=16 FF4_MAPY=29 FF4_AUTOWALK=down \
  FF4_SHOT=/tmp/w.png ./build/ff4_world
#  [trig] event 102 -> map 251 at (102,158) dir 1
```

## 2. Map transitions — SNES mosaic

`EventCmd_d2` / `EventMosaicTbl`: `$2106` ramps the block size `0 → 15 → 0` over
64 frames, two frames per step, with the map swapped at the peak. Implemented as
a post pass on the SNES viewport; `FF4_CURTAIN=1` restores the old wipe.

```
FF4_SHOT_IN_TRANSITION=1 FF4_WARPTO=0 FF4_SHOT_DELAY=22 FF4_SHOT=/tmp/t.png \
  ./build/ff4_world        # world map pixelated to 16x16 blocks at the peak
```

## 3. Text boxes — real window, real font

* `WindowGfx` @ ROM **0x57000** (frame + ASCII font, 256 × 2bpp tiles),
  `WindowPal` @ **0x687D0**, dialogue strips @ **0xA76D8**.
* 9-slice: top `16/17/18`, body `19/FF/1A`, bottom `1B/1C/1D`; box at `(8,12)`,
  30 tiles wide, `$df` body rows (max 8), text `26` columns at `(24, 27+16i)`.
* Sequence: window draws in top→bottom → the whole page appears at once →
  closing hides the text → the box shrinks. No typewriter.
* Glyph tile == FF4 text code (`include/ff4/core/ff4_window_text.h`, generated
  from the rip's `charTable`), with DTE expansion.

```
python3 tools/verify_messagebox.py     # borders y=17/90 x=9/246, text 27/43/59
cd build && ./test_dialog ../data/gfx/window     # 46 checks
```

## 4. NPC and actor palettes — verified pixel-exact

* `MapSpritePal` @ **0x68000**; `LoadPlayerPal` fills CGRAM slots 0–3 from
  entries 0–3; `LoadNPCPal` fills slots 4–7 from `13 + 2*(npcPal & 7)` (+1 for
  the second slot of the pair).
* Characters pick `PlayerPalTbl[char]`; NPCs pick `((byte3 & 0x0C) >> 2) + 4`.
* `data/map_sprite_slots.bin` (384 × 8) drives the actor; each `.npc` line
  carries its absolute palette entry.

```
python3 tools/ff4_palette_truth.py --verify
python3 tools/verify_all_maps.py 0 383
#  462/462 NPC sprites, 350/361 actors pixel-identical, 0 mismatched
```

## 5. Sprite sheets and animation frames

* Sheets: `LoadSpriteGfx` band table (sheet `<0x11` → 32 tiles, `0x11..0x2F` → 16,
  `0x30..0x45` → 8, `>=0x46` → 4), `mapSpriteGraphics[<sheet>].bin` from the rip.
* Actor geometry `PlayerSpriteTiles`, NPC geometry `NPCSpriteTbl` (TL, BL, TR, BR
  with a 1 px row shift). NPCs whose sheet holds only one pose render that pose.

## 6. NPC movement speed and step-aside

* Speed = NPCProp byte 3 bits 6–7; `_00c117 = {1,1,1,2}` px/frame;
  `_00c11b = {1,2,4,8}` repath accumulator (`$08ff & 0x3F`), so a new direction
  is chosen every 64/32/16/8 frames.
* A blocking NPC is forced to speed index 2 and steps aside.
* NPCs respect walls, water and each other.

```
FF4_TRACE=1 FF4_WARPTO=2 FF4_SHOT=/tmp/k.png ./build/ff4_world   # watch [actor]/NPCs
```

## 7. Collision — the real `CheckTilePass`

Every earlier doc read `and $0a` as an immediate. The listing shows
`25 0A` = **`AND $0A`** (direct page) and `move.asm @a965` builds that variable
as `(lane & 3) + 4`:

```c
if (current & 0x04)  passable = ((next & 3) & lane) || (next & 0x04);  // bridge
else                 passable = next & ((lane & 3) + 4);
```

Standing lane (`$d2`) is tracked per step and **not** reset on bridge tiles.
`byte2 & 0x04` hides the actor sprite; `byte2 & 0x08` is OAM priority, not a
clip.

```
cd build && ctest        # test_field pins the mask maths
FF4_TRACE=1 FF4_WARPTO=0 FF4_MAPX=18 FF4_MAPY=14 FF4_AUTOWALK=up \
  FF4_SHOT=/tmp/b.png ./build/ff4_world     # crosses the bridge, lane kept
```

## 7b. Tile-animation data (identification done, runtime pending)

The rule is settled and emulator-verified: a tile animates iff its graphic index
is `0x120..0x12F` (VRAM tiles `$120..$12F`). It is a property of the tile in the
tileset flag table, so a map inherits it from its tileset — 25 animated tile
numbers across tilesets 3,4,5,6,7,12,15,16, used by 181 of 383 maps.

```
python3 tools/ff4_anim_analysis.py     # catalogue + phase sheets
python3 tools/verify_animation.py      # 82 slots animate, Kaipo 512 cells
```

**Runtime animation is now ON by default**: each map with animated tiles is
exported as four full composites (`<id>.png`, `<id>.f1..f3.png`) and the engine
swaps them every 8 frames — the same cadence as `TfrBGAnimGfx`. Animated
*front* tiles (waterfalls) get phase-matched overhangs. `FF4_NOANIM=1` pins
phase 0.

```
python3 tools/verify_animation.py
#  natural cadence: 2468 px differ between frames 8 apart
#  phase 0..3: engine vs composite -> 0 px differ
#  phase 1..3: all changed px inside animated cells
#  control (FF4_NOANIM): 0 px differ
```

## 7c. World-map tile reporting (V key)

Press **V** on a world map for a movable report cursor (arrows), **V** again to
report the cell, **Esc** to leave.  Each report gives the composed tile, its
four base tiles (water-bank ones marked `W`), a `water=N/4` count, a cell
fingerprint and the eight neighbours, printed and appended to
`data/world/tile_report.txt`.

```
python3 tools/verify_world_report.py
#  ocean cell: composed 25 (0x19) base 0x80..0x83 water=4/4
#  land cell:  composed 0x46 water=0/4
#  world tile report OK
```

Batch/headless form: `FF4_REPORT_CELL="0 128,128 60,90" ./build/ff4_world`
(with `SDL_VIDEODRIVER=dummy`).  See `docs/WORLD-TILE-REPORT.md`.

## 7d. Sub-map rendering: centring + no duplicated overlays

* Small rooms are centred on their content box with black around them (classic
  and expanded view).
* The overhang / animation overlays are clipped exactly like the map, so a
  centred (negative-camera) room no longer shows its `front` sub-tiles painted
  twice.

```
python3 tools/verify_submap_view.py
#  map 8 at (1,3): OK   ... 16 views ...
#  sub-map views match their PNG exactly (only sprite cells differ)
```

## 7e. Layouts decoded straight from the ROM

The rip's mapLayouts1/2 are empty for every layout whose compressed stream
crosses a ROM bank boundary - 63 of 384 ids, 27 maps (including map 27, a live
warp target).  `tools/ff4_layout_rom.py` decodes `SubTilemapPtrs` @17/8000 with
the game's own RLE (value = byte & 0x7f, count = next + 1), reproduces the rip
byte-for-byte on all 308 non-empty entries, and fills every gap into
`data/layout_rom.json`, which the exporter uses automatically.  It also proved
`mapLayouts2` is the *group-1* layout table (ids 0..127, base 18/8000) - the
same underground/moon rule as the trigger groups.

```
python3 tools/ff4_layout_rom.py    # 308 matched, 75 gaps filled
```

## 7f. World-map animation, measured against the emulator

* **Underground lava glow** (`UpdateLavaPal @c4cd`): the 8 colors at
  underground-palette entries 18-25 cycle through `LavaAnimPal` (located in
  the ROM by byte pattern at 0xA7A16, validated against the static palette
  within BGR555 rounding), stepped by `LavaPalTbl = 0,1,2,3,3,2,1,0` every
  16 frames - a pulsing glow.  On by default; `FF4_LAVASTEP` pins a step.
```
python3 tools/verify_lava_pal.py
#  underground step 0 vs 1: 1761 px differ, all changed colours are
#  LavaAnimPal entries; cycle palindromic; +128 frames = 0 px; overworld 0
```
* **Overworld water**: dense VRAM sampling while standing proved the wave
  is a slow dither re-step (one visible rotation per ~256 frames); the
  earlier "sweeping wave" was flyover scrolling.  Byte-exact reproduction
  needs the mode-7 chunk composer -> native tile renderer milestone.

## 7g. Layout groups (the $1701 mechanism, emulator-proven)

* **Group 0 proven against the emulator**: the intro airship scene's BG1
  tilemap (VRAM 0x6000) decodes to composed cells that match map 381's
  group-0 layout at 386/390 cells - the rip's `mapLayouts1[layout1]`
  convention is the game's.
* **Group 1 identified**: `mapLayouts2` = the underground/moon layout
  table (`SubTilemap2`, pointer word 256+id, base 18/8000), selected by
  `layoutMSB | $1701` - the same `$1701` as the trigger groups.  127 maps
  have a different underground rendering; the engine picks `<id>.g1.*`
  automatically when entering from the underground/moon.
```
python3 tools/verify_layout_group.py
#  map 7 auto (underground entry): engine vs .g1 composite -> 0 px differ
#  map 7 forced group 0:          engine vs group-0  -> 0 px differ
```
* Along the way: the rip's layout arrays are EMPTY for every bank-crossing
  stream (63 ids); `tools/ff4_layout_rom.py` fills them from the ROM
  (308/308 match where the rip has data).

## 8. Ground-truth tooling built along the way

| tool | purpose |
|---|---|
| `tools/ff4_palette_truth.py` | `MapSpritePal`/`MapProp`/`NPCProp` decode + `--verify` |
| `tools/ff4_window_font.py` | window tiles, font, strips, DTE |
| `tools/ff4_bg_anim.py` | `MapAnimGfx` staging (used to cross-check VRAM) |
| `tools/ff4_warp_graph.py` | warp graph + classification |
| `tools/verify_messagebox.py` | message-box pixel geometry |
| `tools/verify_engine_sprites.py` | per-map sprite pixel diff |
| `tools/verify_all_maps.py` | the whole sweep |
| `tools/verify_animation.py` | phase-change check (animation still opt-in) |
| `tools/retro_dump.c` | headless libretro runner, RAM + VRAM dumps |

## 9. Documentation

`FIELD-COLLISION.md`, `FIELD-MESSAGEBOX.md`, `FIELD-WARPS.md`, `FIELD-ANIM.md`,
`PALETTE_GROUND_TRUTH.md`, `PLAN-field-usable.md`, `TODO.md`, `STATUS.md`.
