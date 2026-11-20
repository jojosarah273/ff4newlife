# Tile animation — what is special about the animated tiles

Tool: `python3 tools/ff4_anim_analysis.py` (regenerates everything below).
Evidence sheets: `/tmp/animtiles_<tileset>.png` (4 phases of every animated
tile, drawn with the real map palette), `/tmp/z7.png`, `/tmp/z6.png`
(10× zooms).

---

## 1. The answer: graphic index `0x120..0x12F`

Animation is **not** a property of the map, the cell, or the "kind" of tile. It
is a property of the **tile number inside the tileset flag table**:

```
tilemap blob (1024 B, 512 cells)   cell -> tile number 0..127
tileset flag table (0x400 B = 128 tiles x 4 sub-tiles x 2 B)
    lo = rom[ts + tile*2 + sub*0x100]
    hi = rom[ts + tile*2 + sub*0x100 + 1]
    graphic index = lo + (hi & 3) * 0x100       # 0..0x3FF
    palette group = (hi >> 2) & 7
    hflip = !(hi & 0x40)   vflip = hi & 0x80

    ANIMATED  <=>  0x120 <= graphic index <= 0x12F
```

That range is **VRAM tiles `$120..$12F`** — precisely the 16 tiles
`TfrBGAnimGfx` (`field/bg_anim.asm @cb5f`) re-uploads every 8 frames
(`$2116 = $1200` is a *word* address, so byte `$2400..$25FF` = 16 tiles × 32 B).

So the artists did not mark "water" — they pointed the water/lava/flame tiles
at the 16 shared animated slots. Identical geometry and palette rules apply to
every other tile; only the graphic source differs.

**Consequence:** 25 tile numbers in the whole game are animated (out of
16 × 128 = 2048 tile slots), and 181 of 383 maps use at least one of them. A
map either inherits animation from its tileset or has none — no per-cell data.

### Per-tileset table (from the tool)

| tileset | BGAnimTbl | animated block | animated tile numbers |
|---|---|---|---|
| 1, 2 | 0x00 | +0x0000 | *none* (world map, crystal) |
| 3 | 0x00 | +0x0000 | 69, 85 |
| 4 | 0x02 | +0x0300 | 95 |
| 5 | 0x03 | +0x0480 | **51, 52, 53, 101** (water, sea) |
| 6 | 0x06 | +0x0900 | 23, 67, 83, 85 |
| 7 | 0x07 | +0x0a80 | **23, 67, 127** (flames/torches) |
| 8–11 | 0x0a | +0x0f00 | *none* used (palette animation instead) |
| 12 | 0x0a | +0x0f00 | 16, 17, 18 |
| 13, 14 | 0x0d | +0x1380 | *none* |
| 15 | 0x0d | +0x1380 | 15, 35, 36, 93, 121 |
| 16 | 0x10 | +0x1800 | 81, 118, 119 (4bpp sheet — see §6) |

Slots are reused per tileset, so the *same* tile number can be water in one
tileset and a flame in another. `/tmp/z7.png` shows tileset 7's slots: two of
its three animated tiles are unmistakably **candle/torch flames flickering
through 4 phases**.

---

## 2. The graphics: `MapAnimGfx`, 3bpp, strided phases

* Block: `MapAnimGfx @ 0xFB9C0` (0x1848 B), 64 tiles × 24 B (3bpp).
* Base per tileset: `BGAnimTbl[tileset-1] * 384` bytes (16 tiles per step).
* `LoadBGAnimGfx @cb01` stages 64 tiles into `$5000`, expanding 3bpp→4bpp:
  copy 16 bytes, then 8 × (byte, 0x00).
* `TfrBGAnimGfx` uploads 16 tiles per phase with stride 0x200:
  `staged tile = 16*(k//4) + 4*phase + (k%4)`, `k = graphic_index - 0x120`.
* 4 phases, advance every 8 frames (`$7a & 0x06 == 0`, phase `$7a & 0x18`).

### Emulator proof (this is what makes the above safe to use)

`tools/retro_dump.c` run for 2500/2508 frames (intro → ship → Baron interior),
`FF4_VRAM=0x0:0x10000`:

```
changed tiles between two frames 8 apart: $120..$12F  (all 16)
staged block match, frame 2500: 16/16 tiles identical (tilesets with base 0x0f00)
staged block match, frame 2508: 16/16 tiles identical
all other tilesets: 0/16
```

`0x0f00 = BGAnimTbl[11] * 384`, i.e. `tileset-1` indexing, and the scene's
tilemaps indeed reference exactly tiles `$120..$12F` — the consistency between
"which base reproduces VRAM" and "which tileset's flags use the animated slots"
is what pins the indexing.

**The exporter now uses this source** (`anim_phase_px()`), replacing the earlier
FF4KSTER per-sheet offsets (`SHEETS_ANIM`) which disagree for tilesets 4, 8–12
and 16. 14 maps changed appearance; 462/462 NPC sprites and 350/361 actors are
still pixel-identical.

---

## 3. Categories the user asked about

| category | mechanism | where |
|---|---|---|
| standing water / canal, sea | tile animation, slots 0x120..0x12F | tileset 5 tiles 51/52/53/101 (Kaipo!); tileset 3/4/6/15 tiles |
| waterfalls | tile animation | one of the slots above, per tileset |
| lava (field tiles) | tile animation **and** palette | tiles + `UpdateLavaPal` |
| candles / lights / torches | tile animation *and* palette | tileset 7 tiles 23/67/127 (`/tmp/z7.png`) + `TowerAnim*Light` |
| overworld ocean | **different VRAM pool** ($221..$23C) + palette | see §4 |
| glowing caves, tower lights, whales | **palette animation only** | see §5 |

The important consequence for the port: because it is one shared mechanism, a
single correct implementation animates *all* of them, including ones we have
not catalogued.

---

## 4. The world map is different (open item)

The world-map tilemaps (maps 250–255) use **tileset 1, whose flag table
contains no `0x120..0x12F` tile at all**. Yet the emulator shows VRAM tiles
`$221..$23C` changing every 8 frames on the overworld:

```
world map (frames 1150 vs 1158): 15 changed 32B tiles ->
    $221,$222,$223,$224,$229,$22a,$22b,$22c,$231..$234,$239,$23a,$23c
```

So the overworld has its own animation pool and its own uploader
(`field/gfx/world_bg_gfx.asm`, `TfrWorldGfx @b181`). That is the answer to
"water on the world map also needs to animate": it is a *separate* range and
needs its own table + routine, not the field one. Next step is to dump
`$4420..$47A0` on two frames 8 apart and diff against candidate tables the same
way the field block was verified.

---

## 5. Palette animation (the part that makes flames glow)

`field/pal_anim.asm` — separate from tile animation, writes CGRAM bytes:

| routine | tileset | writes | source table |
|---|---|---|---|
| `UpdateAnimPal @c41a` | 8, 10, 11 (whale, Zot/Babil, giant) | `$0cfd` blue light, `$0d1d` red light, `$0d5d` yellow light, `$0dbd` whole palette (16 c) | `TowerAnimBlueLight`, `TowerAnimRedLight`, `TowerAnimYellowLight`, `TowerAnimBluePal` |
| `UpdateAnimPal` | 9 (underground cave; +0x40 for Sylvan) | `$0cfd`, `$0cff`, `$0d7f`, `$0d01`, `$0d7d` | `MonsterCaveAnimPal` |
| `UpdateLavaPal @c4cd` | underground only | `$0cff` (bg palette 2, colours 1..8) | `LavaAnimPal` via `LavaPalTbl = 00,10,20,30,30,20,10,00` |
| `UpdateBabilPal @c3ea` | world 0/1 | `$0cdb`+`$0cdc` flash, `$0cdb,x` | inline |
| `UpdateWhalePal @c3bd` | big whale | `$0ec7/$0ec8` | `WhalePal` (8 words) |
| `UpdateZoomPal @c505` | world 0/1/2 | ocean colours | `ZoomPalTbl` (17 entries × 2 B per palette) |

`UpdateZoomPal` is the overworld ocean shimmer while the airship zooms.

---

## 6. Known unknowns

1. **Tileset 16 uses sheet 1, which is 4bpp.** The staged expander assumes 3bpp
   (24 B tiles). No map references tileset 16's animated tiles today, so it is
   inert, but it must be resolved before enabling that tileset.
2. **The intro airship's `$120..$12F` tiles are not from `MapAnimGfx`** — they
   are a verbatim copy of ROM `0xAEC00` (512 B per frame). Special scenes have
   their own uploaders; catalogue them before claiming "all animation works".
3. World-map pool (§4).

## 7. Implementation (per `docs/TODO.md`)

Draw-time substitution, not overlays: the previous overlay approach painted
animated **BG2** water over the composed BG1 town, which is why "all tiles
became water". Rendering each layer in order and substituting the phase tile for
`0x120..0x12F` cells inside `draw_cells()` gives layer-correct animation for
free (the exporter already works this way; the runtime just needs the same
per-phase render, e.g. `<id>.f0..f3.png`).


---

## The torch audit (2025-11): animated art was mirrored

**Symptom (user):** "torches on walls have their tiles split, with the right
side on the left and left on the right."

**Root cause.**  Our two decode paths used opposite bit orders:

* static tiles (`tile_px`): LSB-left art + flip when `hi` bit 6 CLEAR
  (the FF4KSTER convention) - algebraically EQUAL to hardware,
* animated tiles (`anim_tile_px4`): MSB-left art + the same flip rule -
  a DOUBLE inversion, i.e. mirrored versus hardware.

**Proof (from the game's own code).**  `CopyTilemapToBuffer @f872`
(tilemap.asm) copies the tileset flag pair `(lo,hi)` **verbatim** as the BG1
tilemap word.  So hardware renders the ROM bytes MSB-left and flips iff
word bit 14/15 (= `hi` bit 6/7) is SET.  `LSB-left + flip-when-clear` is
equivalent to that; `MSB-left + flip-when-clear` is its mirror.

**Where it showed.**  Mixed cells - one 16x16 cell with some animated and
some static sub-tiles (the wall torch: animated flame + static bracket).
Tileset 15 tiles 15 and 93 (maps 59-62, 111-118, 131, 160, 200-205), and
every fully-animated cell (water, waterfalls, candles) rendered mirrored
inside its 8x8s - invisible on symmetric wave art, obvious on a torch.

**Fix.**  `anim_tile_px4` now decodes LSB-left (bit x -> column x), matching
`tile_px`; one convention everywhere.  Regenerated: the animated reference
PNGs (205 maps), `ff4data` anim `.idx` blocks (the engine's atlas row 16),
and the phase composites.  `verify_animation.py` gained a regression check
(`bit_order_check`) locking anim == static bit order, with this proof in its
docstring.  All guards green; sprite sweep 488/488, 0 mismatches.

**Lesson (recorded in docs/KEYSTONES.md):** FF4KSTER's *flip* convention and
its *bit order* cancel each other; copying one without the other inverts
art.  Every convention must be proven against the game's own word-builder
(`CopyTilemapToBuffer`) or the emulator - self-consistency between our
exporter and our engine cannot catch a shared inversion (both guards and
references moved together).