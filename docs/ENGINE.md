# FF4-C Engine — design

We are **not** building a SNES. We are building a small, modern 2D JRPG engine
that plays Final Fantasy IV's content, and we want the things Gen1Recomp has:
**maps that meld into each other, widescreen, and mods.**

The engine is deliberately simple. No SNES PPU model, no 8×8 planar tiles, no
palette archaeology at runtime. The unit of everything is a **16×16 tile on a
grid**.

---

## 1. The world model

```
World
 └─ Map (id, name, w×h tiles, tileset, palette, music)
     ├─ tiles[]        one 16x16 image per cell (from the tileset atlas)
     ├─ collision[]    per tile: blocked / z-level / bridge
     ├─ triggers[]     events, treasures, teleports
     └─ links[]        edge connections to neighbouring maps   ← "melding"
```

- **Tile** = 16×16 px. A map is a grid of tiles.
- **Map** = one playable room/area (FF4 ships 383; 251 have geometry).
- **Links** = how maps meld: when the player walks to an edge with a link, the
  engine slides into the neighbour map and keeps the player's position
  continuous — no fade, no load screen. FF4's own `TELEPORT` triggers give us
  the target map and coordinates; we lift them into `links`.
- **Widescreen** = the camera is a rectangle we choose, not a fixed 256×224.
  The renderer draws every map tile inside the camera rect, pulling from the
  neighbour map when the camera overlaps a link. A wider window simply shows
  more world — nothing else changes.

## 2. Rendering

One pass, painter's order:

```
for each visible tile:  draw tileset atlas quad at (tile.x*16 - cam.x, ...)
for each sprite:        draw 16x24 (or 16x16) sprite at world - cam
for each window:        draw the FF4 window (9-slice from window tiles) + text
```

- **Tileset atlas**: one PNG per tileset, 16 tiles per row, 16×16 each. A map
  stores tile indices; mods can swap the atlas to re-skin a whole area.
- **Layers**: an optional second grid (`bg`) draws behind the first for
  parallax/overlays. FF4's `bg_grid` field gives us the source.
- **Camera**: floats, clamped to the *linked cluster* bounds, not the map
  bounds — this is what makes melding seamless.
- **Widescreen**: the logical canvas is configurable (e.g. 320×224, 384×216,
  480×270); the window scales it by an integer factor. A "survey zoom" (like
  Gen1Recomp) can shrink tiles to show more world.

## 3. Runtime architecture

```
platform/     SDL3 (or SDL2) — window, input, audio device, present
  └ engine/   the hub: fixed-step loop, state stack, resources, save
      ├ world/    map load, links, camera, collision, encounters
      ├ entity/   player + NPCs: grid movement, animation, wander
      ├ script/   event VM (already ported in C) + dialogue windows
      ├ battle/   ATB battle (already ported in C)
      ├ menu/     menus (already ported in C)
      └ mod/      layered data: replace maps, tilesets, sprites, scripts
```

Everything above `platform/` is portable C99. The existing `ff4_core` logic
(event VM, battle, math, menus) drops in unchanged; the SNES-fidelity
renderer is replaced by this engine's simple one.

## 4. Data format (what the exporter writes)

`tools/ff4_export_world.py` (uses the FreeEnterprise **F4C** library —
`ff4struct`/`ff4bin` — for authoritative ROM decode):

```
data/world/
  manifest.json            list of map ids + world constants
  maps/<id>.png            512x512 composed background (32x32 tiles of 16x16)
  maps/<id>.json           { id, name, w, h, cell, tileset, palette, music,
                             collision[1024], triggers[], links[] }
  tilesets/<n>.png         tile atlas (16 tiles/row, 16x16 each)
```

Everything is plain PNG + JSON: a mod is a folder that overrides files.

## 5. Mods (Gen1Recomp-style, without the weight)

A mod = a directory with a `mod.json` and any of the same files, loaded over
the base data:

```json
{ "id": "my-town", "name": "My Town", "version": "1.0",
  "replaces": { "maps/36.png": "art/town.png",
                "maps/36.json": "data/town.json" } }
```

Layers, in order: base → mods (ordered) → save-specific overrides. Because
the engine only knows PNG + JSON, a mod needs no code — and a scripted mod
(dialogue, events) later uses the existing C event VM's data format.

## 6. Milestones

| # | Deliverable |
| --- | --- |
| E0 | world data exporter (done: 251 maps, collision, triggers, links) |
| E1 | engine shell: fixed-step loop, state stack, 16×16 tile renderer, camera |
| E2 | player: grid movement, animation, collision, widescreen camera |
| E3 | map melding: links resolved at edges, seamless camera handoff |
| E4 | sprites + NPCs from the ROM (map sprite graphics) |
| E5 | dialogue windows (FF4 window 9-slice + game font) driven by the event VM |
| E6 | battle scene (ATB from ff4_core) |
| E7 | mod loader (data layering) + example mod |

## 7. Field tile decode — authoritative spec (from FF4KSTER)

The FreeBASIC FF4KSTER editor (`DEV/FF4KSTER_LINUX`) contains a working field
renderer; its source is the reference for our decoder:

```
palette   : 32 palettes x 8 groups x 8 colors, 0x80 bytes each
            file 0xA6000 + palette*0x80 ; group i, color j at i*16 + j*2
            RGB = (w&31)*8, ((w>>5)&31)*8, ((w>>10)&31)*8
tileprops : 128 tiles x 2 bytes (layer1/layer2/bridge/save/... + damage/warp/
            encounter/trigger), file 0xA1000 + (tileset-1)*0x100
tileset   : 128 tiles x 4 SUB-SPRITES in four 0x100-byte banks
            file 0xA2000 + (tileset-1)*0x400
            for tile b, sub-sprite j (0..3):
              lo = ts[b*2 + j*0x100], hi = ts[b*2 + j*0x100 + 1]
              sprite = lo + (hi & 3)*0x100      (8x8 tile in the sheet)
              group  = (hi >> 2) & 7
              hflip  = NOT (hi & 0x40)          <- inverted on purpose
              vflip  = hi & 0x80
            sub-sprite j draws at ((j%2)*8, (j/2)*8) inside the 16x16 tile
tiles     : 3bpp, planes 0/1 interleaved per row, plane 2 at +16, LSB-first
```

**Map geometry**: 32x32 tiles of **16x16 px** = 512x512 px. The player moves
on the 16x16 grid; FF4's own start (event 16) warps to map 36 at tile (16,30)
— the bottom edge, where the teleport triggers lead to the world map (251).

**Collision**: `(prop1 & 0x0A) == 0` = floor (empirically correct for Baron:
66/256 blocked = open ground with buildings). `prop1 & 0x03` = z-level,
`0x04` = bridge. Exported raw so it can be re-derived.

## 8. Open items

1. **Offset/index alignment**: the rip's `mapGraphics` ranges use the
   disassembly's bank:addr convention (`0x1DED40`), FF4KSTER uses file
   offsets (`0xEEF40`, +0x200). The tileset index is also off by one or two
   between F4C (0-based) and FF4KSTER (1-based). This alignment is the last
   step to a verified field render.
2. **A trustworthy emulator reference**: our captures came from a long
   scripted run whose game state was unknown — the "Baron" reference frame
   turned out to be some other scene, which invalidated earlier diffs. Need a
   deterministic capture (fresh game, known map, known camera) or a reference
   image exported from FF4KSTER itself.
3. **Links**: teleport triggers are exported; mapping them onto map edges for
   seamless melding is engine work.

---

## 9. World maps (implemented)

The three world maps are single large images, not tiled rooms:

| world | size | cells | composed tiles |
| --- | --- | --- | --- |
| overworld | 4096×4096 | 256×256 | 128 |
| underground | 4096×4096 | 256×256 | 128 |
| moon | 1024×1024 | 64×64 | 128 (158 base tiles) |

Each map cell is a **16×16 composed tile** built from four 8×8 base
tiles — this is why a naive one-tile-per-cell render looks half-scale and
"simplified".

Decode (all from the disassembly's data files, no emulation needed):

```
tilemap      worldLayout{1,2,3}[y][x]   cell -> composed tile (0..127)
             (already RLE-decoded in the rip JSON)
composed     ROM disasm 0x148000 (file 0xA0000), 0x200 bytes per world,
             stored as FOUR 128-byte banks: composed tile c's four base
             tiles are comp[c], comp[c+128], comp[c+256], comp[c+384]
gfx          worldGraphics[w]   256 base tiles x 64 px, 1 byte/pixel
tilepal      worldPaletteAssignments[w]  per-base-tile palette base
palette      worldPalettes[w]   64 colors, RGBA

for k in 0..3:                       # 2x2 arrangement
    bt   = comp[c + k*128]           # 8x8 base tile
    base = tilepal[bt]
    pixel = gfx[bt*64 + py*8 + px] + base      (mod 64)
    color = palette[pixel]
    draw at (x*16 + (k%2)*8, y*16 + (k/2)*8)
```

(The rip JSON stores the composed table in the tool's own display form,
so the exporter reads that one table from the ROM at its documented
address.)

`tools/ff4_export_worldmap.py` renders all three to `data/world/*.png`.
Data comes from the rip JSON (tilemap rows, graphics, palettes, per-tile
palette base) **plus one table read from the ROM**: the world tileset
lookup at disasm `0x148000` (file `0xA0000`, 0x200 bytes per world).  The
rip JSON stores that table in the tool's own display form, which produces
wrong tiles (and therefore wrong colours) - the ROM table is the game's.

`build/ff4_world` is the first engine piece and implements the **viewport**:

```
world      2048x2048 px (overworld/underground), 512x512 (moon)
viewport   256x224 logical px (the SNES screen), integer-scaled
actor      a world-pixel position; arrow keys move it
camera     actor - viewport/2, clamped to the world bounds
```

Controls: arrows/WASD move the actor (camera follows) - `T` toggles the
whole-map view - `+`/`-`/wheel zoom - `1`/`2`/`3` pick world - `R`
recentre - Esc/Q quit.  `FF4_SHOT=out.png` renders one frame and exits.

Next: the real actor sprite (worldSpriteGraphics) instead of the marker,
and terrain-aware movement (worldTileProp).

## 10. Actor sprite (added)

The player sprite comes from `mapSpriteGraphics[sheet]` (the rip's decoded
form: 1 byte/pixel, 32 8x8 tiles in a 16x2 sheet, `spriteSheet.tilemap`
order) with a colour set from `npcPalettes[pal]` (8 RGBA colours).

The game derives both from the party leader: the sheet index is the
character id (`field/sprite.asm`, `$1001 & 0x1f`) and the palette is
`PlayerPalTbl[char] = {0,0,1,2,2,2,0,1,1,3,...}`.

In `ff4_world`:
  `,` / `.`   cycle sprite sheet (0..106)
  `;` / `'`   cycle sprite palette (0..35)
  `FF4_SHEET=1 FF4_SPRITEPAL=0 ./build/ff4_world`
