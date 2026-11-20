# Tile animation — field maps (DONE) + world maps (mechanism proven)

## Field maps: identification, data, runtime — all verified

**Identification** (tileset flag table at ROM `0xA2000`, 2 bytes per sub-tile):
```
graphic index = lo + (hi & 3) * 0x100
ANIMATED  <=>  0x120 <= graphic index <= 0x12F     (16 shared slots)
```
Those are VRAM tiles `$120..$12F` — exactly what `TfrBGAnimGfx` re-uploads
every 8 frames.  It is a property of the *tile number*, not the cell or the
map; 25 animated tile numbers exist, used by 181 of 383 maps.
Catalogue: `python3 tools/ff4_anim_analysis.py`.

**Graphics** (emulator-verified: snes9x VRAM `$2400..$25FF` on a castle-interior
scene reproduced all 16 tiles for two different phases, 16/16, no other
tileset matched):
```
block  = MapAnimGfx @0xFB9C0, 64 3bpp tiles
base   = BGAnimTbl[tileset-1] * 384        (BGAnimTbl @cb4f)
phase  = 16*(k//4) + 4*phase + (k%4)      (k = graphic index - 0x120)
cadence: 4 phases, advance every 8 frames  (TfrBGAnimGfx, $7a & 0x18)
```

**Runtime**: `<id>.png` is phase 0; `<id>.f1..f3.png` are full per-phase
composites (BG2 then BG1) for the 185 maps with animated tiles, and the engine
swaps the base texture every 8 frames.  Compositing per phase keeps BG2 under
BG1 by construction — the earlier overlay approach painted an animated BG2 sea
over the BG1 town and is gone.  Animated *front* tiles (waterfalls: tileset 3
tile 69, tileset 5 tiles 51/52) draw above the actor and have phase-matched
overhang images `<id>.fover1..3.png`.  `FF4_NOANIM=1` pins phase 0.

Guards: `python3 tools/verify_animation.py` — engine view equals each phase
composite (0 px), every changed pixel lies inside an animated cell, the
natural cadence changes the view, and `FF4_NOANIM` freezes it.

Full evidence chain: `docs/FIELD-ANIM-ANALYSIS.md`.

## World maps: a different mechanism (TODO item 1a)

The world maps do **not** re-upload graphics.  `WaitFrame @914b` calls
`UpdateWaterAnim @8fa1` (overworld, every frame) or `UpdateLavaAnim @8ee3`
(underground, even frames) + `UpdateLavaPal @c4cd`: both **rotate an 8-tile
run of the world tilemap by one position**, walking a 16-step cycle
(`WaterShiftX @90aa`, `WaterShiftY @909a`), then `TfrWaterGfx @8e67` /
`TfrLavaGfx @8f34` re-upload the touched columns.  The animated bank is base
tiles `0x80..0x9f` + foam `0xac/0xad` in BOTH worlds (water overworld, lava
underground), confirmed by in-game V/B reports
(`data/world/water_tiles.txt`, `docs/WORLD-TILE-REPORT.md`).

## Palette animation (separate mechanism, TODO item 5)

`field/pal_anim.asm`: `UpdateAnimPal @c41a` (tower lights, whale),
`UpdateLavaPal @c4cd` (lava), `UpdateBabilPal @c3ea`, `UpdateWhalePal @c3bd`,
`UpdateZoomPal @c505` (overworld ocean shimmer while zooming).  These cycle
CGRAM entries; without them the underground's lava tiles are flat.
