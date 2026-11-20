# Reporting world-map tiles with the V key

## How to use it

1. Run `./build/ff4_world` (a normal window; keys 1 / 2 / 3 switch Overworld /
   Underground / Moon).
2. Press **V**.  A yellow box appears where the party is and the cell under it
   is reported immediately.  Report mode is now on:
   * **arrow keys / WASD** move the yellow box one cell (you can put it on
     water, which you cannot walk on), `PageUp/PageDown/Home/End` jump 8 cells;
   * **V** (or Enter) reports the cell under the box;
   * **Esc** leaves report mode (the party walks again).
   * **B** reports every *distinct* water cell type currently on screen in one
     go (deduplicated by composed tile) — the fast way to mark a whole region.
3. Every report is printed in the terminal *and* appended to **`ff4_cell.txt`**
   in the directory you launch from — the same file the sub-map `[cell]`
   reports use (override the path with `FF4_CELLFILE=/some/path`). The path is
   printed when report mode starts, so there is no guessing.

Each entry in that file is a readable block plus a compact line:

```
[wtile] world=0 (Overworld) at (128,128) composed=25 (0x19)
  base=128(0x80W),129(0x81W),130(0x82W),131(0x83W) water=4/4 fp=0676DA49
  neighbours: 9 9 25 25 25 25 25 12
  w0 128 128 0x19 128 129 130 131 0676DA49
```

On a sub-map, **V** keeps its old behaviour: it reports the cell the party is
standing on, the four neighbours, tile numbers, prop bytes and passability.

## What a line means

```
[wtile] world=0 (Overworld) cell=(128,128) composed=25 (0x19) base=128(0x80W),129(0x81W),130(0x82W),131(0x83W) water=4/4 fp=0676DA49
[wtile]   -> all four sub-tiles are in the animated water bank: this is an OCEAN cell
[wtile] neighbours: 9 9 25 25 25 25 25 12
```

* `world` — 0 overworld, 1 underground, 2 moon.
* `cell` — 16×16-tile cell coordinates on that world map (world cells are
  16×16 px; the overworld is 256×256 cells, the moon 64×64).
* `composed` — the map's 16×16 tile number from `worldLayout{1,2,3}`, decimal
  and hex. This is the identity the game uses.
* `base` — the four 8×8 base tiles that composed tile is built from (ROM table
  at file offset `0xA0000`, 0x200 bytes per world: `comp[c]`, `comp[c+128]`,
  `comp[c+256]`, `comp[c+384]`). A `W` suffix marks a sub-tile inside the
  animated water bank (see below).
* `water=N/4` — how many of the four sub-tiles are in that bank. `4/4` is open
  ocean, `1..3` is a coastline, `0` is land.
* `fp` — FNV-1a of the 16×16 palette indices of the cell. Identical cells print
  identical fingerprints, so duplicate reports can be recognised offline even
  without tile numbers.
* `neighbours` — the 8 surrounding cells' composed tiles, clockwise from
  north-west. A coast report is unambiguous with this.

The last line of each block is the compact form for scripts:

```
w<world> <cx> <cy> <composed> <base0> <base1> <base2> <base3> <fp>
```

`tools/verify_world_report.py` runs the same code headlessly through
`FF4_REPORT_CELL="<world> <cx>,<cy> [<cx>,<cy> ...]"` and checks a known ocean
cell and a known land cell.

## Result of the first real session

17 cells around (120..133, 122..129) on the overworld, and they agree with the
emulator exactly:

| composed tiles | sub-tiles | meaning |
|---|---|---|
| `0x08 0x09 0x0a 0x0b 0x0c 0x18 0x19 0x1c 0x2a` | all four water | **ocean** |
| `0x02 0x04 0x22 0x24 0x52 0x54` | one or two water | **coastline** |

Base tiles seen: `80..87 8a..8e 94..97 9a 9b 9e ac ad`. Rendering every candidate
settled the bank:

* **0x80..0x9f** — ocean and shore (blue, plus the green land edge in the
  upper half of the range),
* **0xac, 0xad** — the white wave crest (foam),
* 0xa0..0xab, 0xae, 0xaf — mountains, rock and forest (land).

Recorded in `data/world/water_tiles.txt`; the reporter now marks exactly these
as `W`, and replaying all 17 cells reproduces the same `water=N/4` values.

## What we already know (emulator-verified)

Field maps animate by re-uploading 16 tiles to **VRAM `$120..$12F`** every
8 frames.  The world map does something different: dumping VRAM on the
overworld 8 frames apart shows the **BG tilemap entries** being rewritten —
the same screen cell cycles through different tile numbers.  The low bytes that
changed on one screen were:

```
80 81 82 83 84 85 86 87  8c 8d 8e 8f  94 95 96 97 98  9b 9e 9f  ac ad
```

so the world's animated bank is base tiles **0x80..0x9f plus the foam
0xac/0xad**, confirmed by the tile report (`data/world/water_tiles.txt`).  The
ocean composed tile `0x19`, used by 45 316 overworld cells, is exactly base
tiles `0x80,0x81,0x82,0x83`.

Sampling VRAM every 2 frames over frames 1100..1220 (`FF4_VRAM_EVERY=2`)
shows the cadence: about **15 tilemap entries change per frame** while a wave
sweeps across the map, and no single cell changed more than **once in 120
frames** — it is a slow travelling wave, not a 4-phase cycle.

## The mechanism (found in `field/field.asm` after the reports)

`WaitFrame @914b` runs, every frame:

```
$1700 == 0 (overworld)  ->  UpdateWaterAnim @8fa1 + TfrWaterGfx @8e67
$1700 == 1 (underground) ->  UpdateLavaAnim  @8ee3 + TfrLavaGfx  @8f34
                             + UpdateLavaPal @c4cd
always                   ->  TfrBGAnimGfx, UpdateWhalePal, UpdateBabilPal
```

`UpdateWaterAnim` **rotates an 8-tile run of the world tilemap by one
position**, walking a 16-step cycle:

```
WaterShiftX @90aa: 00 88 10 98 20 a8 30 b8 80 08 90 18 a0 28 b0 38
WaterShiftY @909a: 00 41 02 43 04 45 06 47 40 01 42 03 44 05 46 07
```

the horizontal pass uses `($7c >> 1) & 15` and is skipped on odd `$7c`, the
vertical pass uses `$7c & 15` every frame.  `UpdateLavaAnim` returns early on
odd `$7c`, so lava rotates every second frame.  That matches the emulator
(~15 tilemap entries change per frame, no cell changes twice in 120 frames).

So world water/lava is a **tilemap rotation**, not a 4-phase graphics upload
like the field's `$120..$12F`.  Implementation plan in `docs/TODO.md` item 0.

## Underground: lava, same band

A second session (7 cells, underground) reported `composed 0x00` as fully
animated (base `0x9c,0x9c,0x9c,0x9c`) and `0x5f 0x65 0x75 0x76` as edges, with
sub-tiles `0x88 0x89 0x98 0x99 0x9a 0x9b 0x9c` — all inside the same
`0x80..0x9f` band.  So the band is shared; only its meaning changes (water vs
lava).  The moon has no seas.

Note: do not try to identify lava by colour.  The underground's band colours
come from the palette animation (`UpdateLavaPal @c4cd`), so with the static
palette they look dull grey/brown.
