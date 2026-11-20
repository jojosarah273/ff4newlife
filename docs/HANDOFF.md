# FF4-C — handoff / state of the project

Read this first after any compaction. It records the goal, the processes, what
is **proven**, and the rules that keep us from walking code back.

---

## 1. The goal

A **native C99 source port of Final Fantasy IV (SNES, US "FF2" 1.1)** that
reproduces the original's events and gameplay.

* We are **not** writing an emulator and **not** emulating SNES hardware.
* The engine is a normal 2D JRPG engine: a tilemap, a camera, sprites,
  windows, and a small data-driven runtime.
* Data and graphics come from the ROM via the disassembly toolchain; and
  every decode is **verified against the real game**, not assumed.
* Current focus: the **field scene** — its primitives first, then the events
  and systems that hang off them.

Long-term shape (see `docs/ARC…`/`docs/ENGINE.md` for the fuller versions):

```
platform (SDL2)  →  engine hub (fixed step, states, resources)
                     → field / battle / menu / event / audio systems
                     → asset DB (ripped tables + blobs)  +  resource bundle
```

---

## 2. The one rule that matters

> **Measurements beat readings.** When my reading of the assembly disagrees
> with a pixel/byte measurement, the measurement wins. Every wrong turn this
> project has taken came from asserting an asm interpretation without a
> reference to falsify it.

Two concrete examples, both caught by measurement and both now documented so
they are never "fixed" again:

| mistake | why it was wrong | proof |
| --- | --- | --- |
| "`layout2` indexes `mapLayouts2`" | `mapLayouts2` has 128 entries but `layout2` ranges 0..255; `mapLayouts1[layout2]` has median **1** distinct tile (a backdrop), `mapLayouts2[layout2]` has 48 | Kaipo renders as sea, matching `kaipo1.png` exactly in 5-bit space |
| "`mapPalettes` is a **split lo/hi** table" (from `LoadMapPal`'s `+0x10`/`+0x90`) | those offsets are the **runtime CGRAM shadow** layout, not the ROM table | scoring palettes against the 7 colours of the real Kaipo map: split = **0/7** hits, interleaved 16-bit = **4/7** |
| "palette tables also need the −0x200 headered-ROM fix" | only the *tilesets, sprite sheets and tile props* come from FF4KSTER's addressing | rendering Baron with both: smoothness **14.8** (correct) vs 54.9 |

Corollary: **do not delete or rewrite working code on the strength of a new
asm reading.** Add the measurement first, then change the code, then re-run
`ctest`.

---

## 3. What is proven and must not regress

Each line below was verified; the test suite pins most of it.

### World maps (all three) ✅
* Overworld **4096×4096**, underground **4096×4096**, moon **1024×1024**.
* **256×256 cells (moon 64×64) of 16×16 composed tiles**; each composed tile
  is four 8×8 base tiles.
* Composed table: disasm `0x148000` → file **`0xA0000`**, 0x200 bytes/world,
  stored as **four 128-byte banks**: `comp[c]`, `comp[c+128]`, `comp[c+256]`,
  `comp[c+384]`.
* Graphics `worldGraphics[w]` (1 byte/pixel), `worldPaletteAssignments[w]`
  (per-base-tile palette base, 0/0x10/0x20/0x30), `worldPalettes[w]` (RGBA).
* Palette confirmed correct **by the user** on screen.
* Map size masks `{0xFF,0x7F,0x3F}`; on the overworld **only bit 0
  (Lower Z-Level) is ever set**; 58 of 128 tiles walkable = 15.8 % of cells.

### Sub-maps (towns/castles/caves) ✅
FF4KSTER's renderer works but assumes a **headered** ROM. For our headerless
`ff2.smc` **every address is −0x200**:

| table | address |
| --- | --- |
| tile props | `0xA0E00 + (n-1)*0x100` |
| map tileset | `0xA1E00 + (n-1)*0x400` (four 0x100 banks) |
| palettes | `0xA5E00 + pal*0x80` |
| sprite sheets | ship `0xADC00`, castle-ext `0xEED40`, town `0xF1568`, house `0xF2CC0`, castle-int `0xF3EC0`, crystal `0xF51E8`, mountain `0xFD208`, cave `0xFEA88`, … (all −0x200 from FF4KSTER) |

Per tile `b`, sub-sprite `j` (0..3):

```
lo = ts[b*2 + j*0x100] ; hi = ts[b*2 + j*0x100 + 1]
sprite = lo + (hi & 3) << 8
group  = (hi >> 2) & 7
hflip  = NOT (hi & 0x40)          <- inverted on purpose
vflip  = hi & 0x80
drawn at ((j%2)*8, (j/2)*8)       <- the 2x2 composition
```

3bpp tile: planes 0/1 **interleaved per row**, plane 2 at `+16`, **LSB first**.

* **361 sub-maps exported** to `data/submaps/<id>.{png,walk,json}`.
* Sanity: Baron (36) renders a walled structure whose bottom opening sits at
  columns 13–18 — exactly where the warp triggers `(14..17, 31)` are.

### Layers ✅ (implemented and verified against `kaipo1.png`)
* **BG1 = `mapLayouts1[layout1]`** — drawn on top, **colour index 0 is transparent**.
* **BG2 = `mapLayouts1[layout2]`** — drawn first, opaque.
* **Both layout indices address `mapLayouts1`.** `mapLayouts2` (only 128 entries)
  is *not* what `layout2` indexes. Evidence:
  * `layout1` and `layout2` both range **0..255**, but `mapLayouts2` has 128
    entries — 80 of 383 maps would be out of range;
  * `mapLayouts1[layout2]` has a **median of 1 distinct tile** per map
    (i.e. a uniform backdrop); `mapLayouts2[layout2]` has 48.
* Kaipo: `layout1 = 2` (the town), `layout2 = 34` = **all 1024 cells tile
  101**, which resolves to group 4 of palette 4 — the four blues
  `(90,165,255) (33,115,214) (16,90,189) (57,57,57)`. That is the sea.
* Verified against `kaipo1.png` (511×511, hand-extracted from snesmaps): the
  water, sand and rock colours agree **exactly in 5-bit SNES space**
  (`(136,184,248)` vs `(140,189,255)` — both 17,23,31).
  **When diffing, quantise to 5 bits first**: snesmaps truncates (`c<<3`),
  we replicate (`(c<<3)|(c>>2)`); both denote the same 15-bit colour.
* Earlier this was misdiagnosed as "wrong palette" because we drew BG1's
  index 0 as opaque black and the sea came out black.
* Priority: tilemap entry **bit 13** draws above OBJ (the "walk under the
  bridge" effect). Partial occlusion: `byte2 & 0x04` hide-sprite,
  `byte2 & 0x08` hide-bottom. **Not yet implemented.**

### Movement & collision ✅ (`tests/test_world_rules.c`, 396 checks)
```
$00:A8EB  XMoveTbl   00 00 01 00 FF      (1=up 2=right 3=down 4=left)
$00:A8F0  YMoveTbl   00 FF 00 01 00
$00:AAE6  VehiclePassBit   01 02 04 10 20 20 80
$00:AAED  MapSizeMask      FF 7F 3F
$00:AAF0  TriggerRateTbl   0F 07 03 01
$00:AAF4  PlayerMoveRateTbl 01 02 04 08   (px per frame)
$00:830C  PlayerSpeedTbl   00 01 02 01 03 03 03   (by vehicle; on foot = 0)
```

* **On foot = 1 px/frame = 16 frames per tile (~267 ms)**; the walk animation
  flips every 8 frames (`$7b & 8`) ⇒ two leg swings per tile.
* Movement is **cell-authoritative** (cell + step progress), so a step always
  completes on a tile boundary and wrapping is exact.
* World passability `(cur & 0x41) & (next & 0x41)`; field
  `(next & 0x0A)`; bridge lanes via `& 0x03`; bridge flag `& 0x04`.
* Field `byte2` bits (verified — Baron's 20 triggers are all `0x80`):
  `0x01` damage · `0x04` hide-z · `0x08` hide-bottom · `0x10` exit ·
  `0x20` through · `0x40` random battles · `0x80` trigger.

### Actor sprite & animation ✅
```
$15:B2FA  PlayerPalTbl  00 00 01 02 02 02 00 01 01 03 00 01 00 00 00 00 00 00
$15:C0B4  PlayerSpritePosTbl  70 6D | 78 6D | 70 75 | 78 75   -> 16x16 at (112,109)
$15:C0C4  PlayerSpriteTiles  8 frames x 4 sprites
```
* sheet = **character id**; palette = `PlayerPalTbl[char]`
* frame = `facing*2 + walkstep`; tiles **0–3 down, 4–7 up, 8–11 left,
  12–15 right** (right reuses left, h-flipped); 1-px Y bob on horizontal steps
* **camera centres the cell** (`actor_x + 8`), sprite drawn at the cell's
  top-left — fixes the half-tile "lean" bug.

### Dialogue window ✅
* 236×72 SNES px at (8,8) — **4 lines**, 12-px pitch, 7-px glyphs,
  auto-wrapped by **measured width** (`TTF_SizeUTF8`), mixed case.
* Reveal: **one 8-px tile row per frame** (`$df` in `OpenDlgWindow`/
  `CloseDlgWindow`), text complete from the start (no typewriter).
* Text currently `data/graphics/ff4.ttf` (interim; original glyphs pending).

### Warps ✅
* `data/world/warps.json` — **82 entryways** (58 overworld, 17 underground,
  7 moon), 2×2 cell zones, each `{world,x,y,map,name,xDest,yDest,dir,event}`.
* Firing **on cell entry**; destination sub-map loads and renders; `W`
  returns to the world cell you left.
* 117 sub-maps carry a reverse trigger.

### Viewport ✅
* classic 256×224 ↔ expanded (fills the window at the same `zoom`, sprites
  unchanged); resizable window; fullscreen (`0`, `9`).
* Expanded viewport = `win_w/zoom × win_h/zoom`; sprites keep their size
  because the map and the sprite share the same `zoom`.

---

## 4. Processes

### Build & test
```
cmake -S . -B build && cmake --build build
ctest --test-dir build          # 12 suites, must stay 100 %
```
`tests/test_world_rules.c` pins all sprite/physics constants against the ROM.
**Never leave the tree with a failing test or a new warning.**

### Regenerate data
```
node tools/export_resources.js                  # data/gfx (asm palettes + window tiles + rip blobs)
python3 tools/ff4_export_worldmap.py            # data/world/{overworld,underground,moon}.{png,idx,pal,walk}
python3 tools/ff4_export_submaps.py ff2.smc     # data/submaps/<id>.{png,walk,json}  (361 maps)
python3 tools/ff4_export_world.py <rom> data/world   # field meta (F4C-based, legacy)
```

### Verify against the real game
* `tools/retro_dump.c` — headless libretro harness (snes9x core), dumps
  exact 256×224 frames. **This is the oracle.**
* Compare with a small Python PNG decoder (zlib+filter reconstruct); measure
  exact-pixel %, per-cell dominant colour, or local smoothness as appropriate.
* Screenshots from the app: `SDL_VIDEODRIVER=dummy FF4_SHOT=out.png ./build/ff4_world`.

### Data addresses cheat-sheet (headerless `ff2.smc`)
```
0xA0000  WorldTileset (composed 16x16 tiles, 4 banks/world)
0xA0E00  MapTileProp      (16 tilesets x 0x100)
0xA1E00  MapTileset       (16 x 0x400, four 0x100 banks)
0xA5E00  MapPal           (32 x 0x80, interleaved 16-bit LE)
0xEED40  Castle-exterior sprite sheet, 3bpp, 24 bytes/tile
```

---

## 5. Open items (in order)

1. **~~BG2 under BG1 with transparent index 0~~ DONE** — implemented in
   `tools/ff4_export_submaps.py` (`draw_cells(..., transparent=...)`), all
   361 maps re-exported, Kaipo verified against `kaipo1.png`. Remaining
   cosmetic delta: our map edge column/rows still differ from the reference
   border. **Rule from the user: fill outside a map must be BLACK** — every
   map smaller than the game's viewport is an interior map and the uncovered
   area is black, not tiles. `mapProperties.fill` (54 for Kaipo) is the
   *border* tile used where the layout has zeros; it currently renders as
   sand. Unresolved: how the engine knows a map's true extent (the layout is
   always 32×32). Corner pixels already match exactly
   (`(0,0)=(7,7,7)`, `(0,504)=(28,26,19)`, `(504,504)=(28,26,19)`), only the
   mid-edges differ.
2. **Automated per-map reference capture** — *approach changed, see warning below.*

   ### SOLVED -- the harness was broken, not the ROMs
   `tools/retro_dump.c` had two bugs that made **every** capture worthless:
   1. it stored the core's framebuffer **pointer** instead of copying it, and
      the core reuses that buffer on the next frame;
   2. it read the buffer as 32-bit `0x00RRGGBB` pixels, but the core emits
      **RGB565** (`fmt 2`, 2 bytes/px) -- so the whole image was mis-strided.
   Together these produced one fixed "garbage" frame that poisoned every
   reference (`refs/snes_title.png` <b>was</b> that garbage).
   **FIXED**: the video callback now converts RGB565 -> RGB32 into a private
   `fb_copy[]` (see `retro_video_refresh`).

   After the fix, retail `ff2.smc`, the rebuilt `ff4-en.sfc` **and** the
   `SKIP_INTRO` build all render the title screen **byte-identically**
   (`md5 5fd4d5e25403da9d`) -- so the disassembly build is fine, and the
   2.45 % byte difference from retail is just the config/bugfix defines.

   ### The debug ROM works
   `make ff4-en-debug` -> `ff2-debug.smc` boots and, once a new game starts,
   lands **on the overworld at Baron** (castle, town, airship, forest, sea) --
   exactly what the patched `SKIP_INTRO` block orders.

   ### Still open: jumping to an arbitrary map
   The debug build only gets us onto the overworld. To reach an interior map we
   need the WRAM poke (`$1700=3, $1702=map, $1706/$1707=x/y, $CD=1`) from §10,
   or the 5-byte retail patch + free-space stub. **`$CD` is unverified** --
   confirm it is really the map-reload trigger before building on it.
   ### The workable route: retail ROM + a WRAM poke
   Retail `ff2.smc` boots fine, and its intro ends **on the field**.
   `MainLoop` reloads the map whenever `$CD` is non-zero, reading `$1700`
   (map type: 0 world / 3 sub-map), `$1702` (map id) and `$1706/$1707` (x/y).
   So:
   1. boot retail `ff2.smc` in `tools/retro_dump.c` (it already injects
      button presses — press Start to leave the title);
   2. let the intro finish so the game is in a field state;
   3. call `retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM)` and write
      `$1700=3, $1702=<map>, $1706=x, $1707=y, $CD=1`;
   4. dump the next frame — that is the reference.
   No playthrough, no ROM build, and the reference is guaranteed to be retail.
3. **Original font glyphs** — `char + 0x82` is the char→tile base but that
   bank decodes to stripes; find the real font tile bank.
4. **Window art** — the authentic 9-slice (tiles `0x16–0x1D`, `0xFF`) is
   coded but disabled: `WindowGfx` (from `menu/gfx/window_gfx_en.asm`, 256
   2bpp tiles) does not contain those tiles at those indices.
5. Then F2/F3/F4 of `docs/ROADMAP-field.md`: new-game data init, events
   wiring, NPCs.

---

## 6. Repository map

```
interface/            C99 engine headers
  core/               types, state, math, gfx (tile decode + PPU), font
  engine/             asset_db, field, battle, menu, event, audio, scene
  resource/           ff4_bundle (manifest-driven resource namespace)
  platform/           render_api, audio_api, input_api, app_paths
src/…                 implementations (same layout)
src/platform/sdl2/    sdl_main (playable demo), world_view (field viewer)
tests/                12 suites; test_world_rules pins the hard constants
tools/                export_* (data), retro_dump (oracle), render_verify
third_party/ff4       everything8215 disassembly (authoritative asm + rip)
docs/                 ARCHITECTURE, ENGINE, ROADMAP-field, ROADMAP-warps,
                      RESEARCH-collision, RESEARCH-sprites-physics,
                      FEASIBILITY-frameworks, this file
data/gfx/             resource bundle (asm palettes, window tiles, rip blobs)
data/world/           world maps (.png/.idx/.pal/.walk) + warps.json
data/submaps/         361 sub-map .png/.walk/.json
```

## 7. Current controls (`build/ff4_world`)

| key | action |
| --- | --- |
| arrows / WASD | walk (1 tile per step) · Shift = chocobo pace |
| **F1..F8** | jump to a preset map (wipe, spawn forced walkable): F1 Town of Baron, F2 Village Mist, F3 Kaipo, F4 Mysidia, F5 Silvera, F6 Agart, F7 Astro Tower, F8 Black Magic Class |
| **W** | warp to selected entryway / leave a sub-map |
| Y / U | previous / next warp destination |
| T | dialogue window · M whole-map view |
| 0 | classic ↔ expanded viewport · 9 fullscreen |
| 1 / 2 / 3 | overworld / underground / moon |
| P | respawn on land · R recentre · +/- zoom · Esc quit |

Env: `FF4_WARPTO=<map>`, `FF4_DIALOG=1`, `FF4_DIALOG_FULL=1`,
`FF4_EXPAND=1`, `FF4_AUTOWALK=dir`, `FF4_SHOT=out.png`, `FF4_TRACE=1`.

---

## 8. Dead ends — do not retry these

| attempt | outcome |
| --- | --- |
| Play the emulator forward to reach a map | impossible — story gates; you cannot walk to Kaipo |
| snesmaps full-res map pages as reference | they are giant stitched posters (Kaipo page = 1456×848 covering surrounding overworld), not 1:1 captures |
| snesmaps 256×256 thumbnails as reference | downscaled/cropped; colours unreliable |
| Build a ROM from the disassembly as a reference oracle | ~~doesn't boot~~ **WORKS** — the "garbage" was a `retro_dump` bug (§5.2). Rebuilt + `SKIP_INTRO` builds render the title byte-identically to retail |
| `SKIP_INTRO` as a ready-made debug boot | its block skipped `InitHWRegs`/`InitZoomHDMA`/brightness/`hNMITIMEN`; fixed in `field/field.asm`, but moot because of the above |
| Passing `ASMFLAGS=...` on the make command line | overrides the module Makefiles' `-I` include paths → "Cannot open include file". Add a target instead (`ff4-en-debug`, `VERSION_EXT=endbg`) |
| `make clean` then rebuild | `clean` **deletes the ROM-extracted `gfx/*.asm`** includes; run `make rip` first |

**What does work for references:** hand-extracted crops from the sprawling
snesmaps posters (`kaipo1.png` 511×511 = our 512×512 render at 1:1 — this is
what proved the palette and the BG2 layer). Compare **in 5-bit colour space**:
snesmaps truncates (`c<<3`), we replicate (`(c<<3)|(c>>2)`).

## 9. Files added this session

```
kaipo1.png, kaipo2.png          hand-extracted Kaipo references (user-supplied)
refs/maps/kaipo.png             full snesmaps poster (1456x848, for colour checks only)
docs/HANDOFF.md                 this file
third_party/ff4/Makefile        added ff4-en-debug target (VERSION_EXT=endbg)
third_party/ff4/field/field.asm fixed the SKIP_INTRO block to not skip HW init
ff2-debug.smc                   built debug ROM - DOES NOT BOOT, kept only as a record
```

## 10. Reference capture from the RETAIL ROM (the way that will work)

**Why the debug-ROM build is wrong:** you still cannot reach the story maps
by playing (Mist Cave → D.Mist → Mist → Rydia → Kaipo → …) and a rebuilt ROM
does not run at all (§8). So the capture must force the map, not play to it.

We do **not** need to rebuild. We need to patch **5 bytes of retail** and put
a stub in free ROM space. The new-game boot path was located by byte-pattern
search in `ff2.smc` (headerless):

```
0x0052  AD FB 17        lda $17fb
0x0055  C9 00           cmp #0
0x0057  D0 32           bne 0x008B          ; loading a save -> skip new game
0x0059  20 90 82        jsr $8290           ; InitNewGame
0x005C  20 D0 92        jsr $92D0           ; InitCharProp
0x005F  22 3E C2 15     jsl $15C23E         ; InitSpellLists
0x0063  22 E5 C8 15     jsl $15C8E5         ; InitHWRegs
0x0067  20 02 83        jsr $8302           ; UpdatePlayerSpeed
0x006A  20 AA F1        jsr $F1AA           ; InitZoomHDMA
0x006D  A9 01 85 B1     lda #1 : sta $b1
0x0071  A9 01 85 7D     lda #1 : sta $7d
0x0075  A9 0F 85 80     lda #$0f : sta $80  ; screen brightness
0x0079  A9 81 8D 00 42  lda #$81 : sta $2100 ; NMITIMEN
0x007E  58              cli
0x007F  64 AB           stz $ab
0x0081  A9 10           lda #$10            ; <-- INTRO EVENT ID
0x0083  20 63 DF        jsr $DF63           ; ExecEvent
0x0086  64 B1           stz $b1
0x0088  4C AF 80        jmp $80AF           ; MainLoop
```

`InitNewGame` is at **$00:8290** (`jsr $8290` from bank 0).  Note the
disassembly's `@8290`/`@80af` comments are the *low word* of these addresses,
which is why pattern-searching works and the `@` comments mislead.

### The patch

1. **Free space stub** (find a run of `0x00`/`0xFF` in an unused bank):
   ```
   lda #3      : sta $1700     ; 3 = sub-map      (A9 03 8D 00 17)
   lda #<map>  : sta $1702     ; map id           (A9 xx 8D 02 17)
   lda #<x>    : sta $1706     ; spawn x          (A9 xx 8D 06 17)
   lda #<y>    : sta $1707     ; spawn y          (A9 xx 8D 07 17)
   lda #1      : sta $cd       ; request map load (A9 01 85 CD)
   stz $b1                     ; 64 B1
   jml $0086                   ; 5C 86 00 00
   ```
2. **Overwrite the 5 bytes at `0x0081`** — `A9 10 20 63 DF` (lda #$10 /
   jsr ExecEvent) — with `5C <lo> <hi> <bank> EA` (jml stub + nop).

Result: retail ROM, retail code, retail graphics — it just boots onto the map
we name, with no intro and no playthrough. Changing the map is one immediate
byte in the stub (`lda #<map>`), so the loop is:

```
for map in 0..360: patch the stub's map byte -> boot -> dump frame -> next
```

This is the only route that is both *retail* and *automated*. Until it is
built, verify decodes against hand-extracted crops (`kaipo1.png`), which is
what already proved the palette and the BG2 layer.

### Root cause of the bad reference captures (diagnosed)

All of these files are **byte-identical** (`md5 365ee2de488c907e`, 5372 bytes):

```
refs/snes_title.png        <- a "reference" we had been trusting
refs/dbg/frame_0200.png    <- debug-ROM build dump
/tmp/norm/frame_0300.png   <- normal rebuild dump
refs/runA/frame_0180.png .. refs/runA/frame_0480.png
```

*MD5 comparison across runs:*
```
ea339d0c 586   runE/frame_0000     (black)
e04f4e2b 5283  runE/frame_0168     <- VALID (the 93.5% title match)
5b29dfe0 4329  runE/frame_1200     <- VALID
644a221b 5375  runA/frame_0120     <- VALID
365ee2de 5372  runA/frame_0180+    <- the fixed garbage frame
```

⇒ **`tools/retro_dump.c` works at first and then degrades to one fixed garbage
frame after roughly 120–180 frames**, identically for every ROM. So it is a
harness bug, not a ROM bug. Most likely the captured pointer is not copied:
`retro_video_refresh` hands us the core's buffer and we dump it later, after
the core has reused it. **Fix: `memcpy` the frame in `retro_video_refresh`.**
Until then:
* every capture ≥ frame ~180 is worthless;
* **`refs/snes_title.png` is worthless** — re-capture it;
* the "built ROM does not boot" conclusion was **invalid**; the rebuilt ROM is
  probably fine and simply untested.

**POSTSCRIPT — this appendix's diagnosis was correct, and the bug is now FIXED.**
`retro_video_refresh` copied nothing and mis-strided RGB565 as 32-bit. With
the fix, all three ROMs (retail, rebuilt, debug) render the title identically
and `ff2-debug.smc` boots straight to the overworld at Baron. `refs/snes_title.png`
and every capture >= frame ~180 from before this fix should be re-captured.

---

## 11. ✅ EVERY MAP, AUTOMATED — `tools/make_map_rom.py`

This is the answer to "we need every map in the game, and you can't play it".

The retail ROM is patched into a map loader: 5 hook bytes + a 52-byte stub in
free space. No rebuild, no playthrough, no story gates.

```
hook   bank $00:$8081 (file 0x0081)   A9 10 20 63 DF  (lda #$10 / jsr ExecEvent)
   ->  5C 55 DE 01 EA                (jml $01DE55 / nop)
stub   bank $01:$DE55 (file 0xDE55)   1195 bytes of 0xFF padding
       lda #3  : sta.l $001700       sub-map
       lda #M  : sta.l $001702       map id
       lda #MSB: sta.l $001703       map id high bit (>255)
       lda #X  : sta.l $001706       spawn x (cells)
       lda #Y  : sta.l $001707       spawn y (cells)
       lda #0  : sta.l $0000B1       no event
       jsl $008313                   LoadMap
       jml $0080AF                   MainLoop
```

**LoROM address note (this tripped me up badly):** file offset `O` is
bank `O / 0x8000` at CPU address `0x8000 + O % 0x8000`. So file `0xDE55` is
**`$01:$DE55`**, not `$00:$DE55`, and the boot code at file `0x0081` is
**`$00:$8081`**.

**Do NOT rely on `$CD` to trigger the load.** `MainLoop` at `$00:80C2` tests
`$1700` first and jumps to `$8138` when it equals 3, so the
`lda $cd / jmp $8313` check at `$00:80DA` is **never reached for sub-maps**.
Call `LoadMap` (`$00:8313`) directly with `jsl`.

### Usage
```
python3 tools/make_map_rom.py 2            # -> /tmp/maprom_2.smc
python3 tools/make_map_rom.py --dump-all refs/maps_real 0 360
```
Each dump runs snes9x for 1501 frames (`pressStartAt=400`) — roughly 30–60 s
per map, so all 361 take a few hours. Lower the frame count to the minimum
that still reaches the map before batching.

### Proven
`make_map_rom.py 2` boots into **Kaipo** and the frame shows sand, palms,
buildings, water, NPCs and Cecil — saved as `refs/maps_real/2.png`.
Automated reference capture is therefore **working**.

### Next: calibrate the comparison
First diff (our `data/submaps/2.png` vs `refs/maps_real/2.png`, 5-bit exact on
a 4-px grid) = **47 %** with the best camera offset at (8,−8). Both facts need
explaining before the number means anything:
* the **offset** must be pinned (camera centres the cell; spawn cell is
  (16,16) → pixel (264,264)) — an (8,−8) residual is half a tile;
* the game frame contains **sprites** (party + NPCs) that the export cannot
  have, so unmatched pixels are expected in those regions — mask them out or
  compare background-only cells.

### First real result (Kaipo) — 78.4 % exact

With the alignment searched properly (a full sweep of the export's 512×512 in
1-px steps, comparing in 5-bit colour space):

```
best alignment   11242/14336 = 78.4 %   window starts at (144,145)
bg-only          11145/14156 = 78.7 %   (skip colours used by <0.3 % of frame)
expected window  (136,152) for spawn cell (16,16)
```

Two conclusions:

1. **The decode is right.** 78 % of pixels match the real hardware exactly,
   and masking out sprite-colour pixels barely changes it (78.7 %), so the
   residual is **not** sprites. The remaining ~21 % is a structural
   difference, not a palette/tileset error.
2. **The camera anchor is off by (+8, −7).** We spawn at cell (16,16) and
   expect the window at (136,152); the game puts it at (144,145). So the field
   camera does **not** centre the cell the way the world map does
   (`cam = pos + 8 − vw/2`). This must be settled before any per-pixel diff
   can drive fixes — otherwise every comparison carries a half-tile error.

**Next:** pin the field camera formula (try `cam = cell*16 − vw/2 + k` for
k of 0, 8, 16 and compare), then diff with the alignment fixed. Likely
remaining structural cause: the **BG priority / overhang pass** (tilemap bit
13, `byte2 & 0x04` hide-sprite, `byte2 & 0x08` hide-bottom) is still
unimplemented, so anything drawn over the player is missing.

### Comparison tooling (TODO: make this a script)
The alignment + match code currently lives in throwaway heredocs. It should
become `tools/compare_map.py <export.png> <real.png>`:
  1. decode both PNGs (zlib + filter reconstruct, no deps);
  2. quantise to 5 bits (`c >> 3`) — snesmaps truncates, we replicate;
  3. sweep offsets in 1-px steps over the plausible window;
  4. report best offset, match %, and a bg-only match that skips colours
     covering <0.3 % of the frame (a cheap sprite mask).

---

## 12. Camera pinned, overhang plumbed, diff tooled (all three tasks)

### 1. Field camera formula — SOLVED (verified)

Three captures of map 2 at spawn cells (10,10), (16,16), (24,18):

| spawn | naive expected origin | measured origin | delta |
| --- | --- | --- | --- |
| (10,10) | (40,56) | **(48,49)** | +8,−7 |
| (16,16) | (136,152) | **(144,145)** | +8,−7 |
| (24,18) | (264,184) | **(256,177)** | −8,−7 |

The x delta flips sign at (24,18) because the camera **clamps** at the map
edge (origin cell 16 = 32 − 16).  Unclamped, all three agree on

```
origin = (16*(cell_x - 7), 16*(cell_y - 7) + 1)
```

so **the party is drawn at viewport pixel (120, 119)**, NOT at the centre
(128, 112) — the world map's `cam = pos + 8 − vw/2` is wrong for sub-maps.
Implemented in `src/platform/sdl2/world_view.c` as

```c
cam_x = actor_x + 8 - (vw / 2 - 8);   /* party at vw/2 - 8  */
cam_y = actor_y + 8 - (vh / 2 + 7);   /* party at vh/2 + 7  */
```

Confirmed: with `--expect 144,145` the comparison tool now reports
**delta (+0,+0)** — the prediction and the measurement agree exactly.

### 2. Overhang / occlusion — plumbing done, semantics UNVERIFIED

The exporter now emits, per map:

```
<id>.props      2048 bytes = 2 per cell (tile byte1, byte2)
<id>.over.png   512x512 RGBA, only the cells flagged "upper z"
<id>.json       + overhang_cells / hide_z_cells / hide_bottom_cells counts
```

**The selection rule I used is probably wrong and must not be trusted.**
`byte1 & 0x01` (UpperZ) flags **415 of 1024 cells on Kaipo**, and map 54 has
`byte2 & 0x04` (HideZ) on **1002 of 1004 cells** — neither is plausible for
"tiles drawn over the player". The real behaviour depends on the **player's
current z-level**, so a static per-cell mask is not the right model; the engine
must compare the tile's z against the party's z at draw time. The `.props`
data is the useful artifact — the `.over.png` split is a guess.

Rules as documented in `include/ff4/engine/field_engine.h`:
`TP1 0x01=UpperZ 0x02=LowerZ 0x04=Bridge 0x08=Save 0x10=Door`;
`TP2 0x01=Damage 0x04=HideZ 0x08=HideBottom 0x10=Exit 0x20=Through
0x40=Battle 0x80=Trigger`.

### 3. `tools/compare_map.py` — done

Self-contained (own PNG decoder, no PIL).  5-bit colour comparison, 1-px
offset sweep around an expected origin, background-only match that ignores
colours covering <0.3 % of the frame, `--json`, `--dump-png` diff image.
Exit 0 only at ≥90 %.

```
python3 tools/compare_map.py data/submaps/2.png refs/maps_real/2.png \
        --expect 144,145 --radius 2 --dump-png refs/maps_real/diff_kaipo.png
  best origin (144,145)  delta (+0,+0)
  match          44747/57344 = 78.03 %
  bg-only match  44389/56653 = 78.35 %
```

### The 22 % residual is WATER ANIMATION, not a decode error

`refs/maps_real/diff_kaipo.png` (red = mismatch) shows the mismatch is
**entirely the sea** plus the sprites.  Grass, stone, buildings and trees are
all matched.  FF4 animates water (`field/gfx/map_anim_gfx.asm`), and our
export is one static phase — so this is expected, not a bug.

**Consequence:** the decode is correct. Remaining work is (a) water/tile
animation, (b) sprites, (c) the z-relative priority pass. Do not "fix" the
palette or the tileset on account of this 78 %.

### Bug found while doing this (worth remembering)
Reusing the name `props` for the per-cell byte buffer **shadowed
`mapProperties`**, so after the first map `props[mid]` was an int and every
remaining map silently hit `isinstance(p, dict) == False`. The export printed
"rendered 1 sub-maps" instead of 361 with no error. Renamed to `cell_props`.
**Lesson: this exporter has no error paths — always check the rendered count.**

---

## 13. Tile animation diagnosed, and Kaipo verified at 98 %

### The animation is TILE-based, not palette-based

Sampling the sea across frames 312–1024 of a live Kaipo capture: the colour
set is **constant** — `(17,23,31) (25,28,31) (11,20,31)` — all of which exist
in palette 4 group 4.  So it is **not** palette cycling.  But the *pattern*
changes: comparing frame 400 against 408/416/424/432, **38–45 % of water
pixels differ**.  That is tile animation (`field/bg_anim.asm` +
`MapAnimGfx`, `MapAnimGfxSize = 0x1848`, included from `bg_gfx.asm`).
Implemented in the game by swapping tile graphics in VRAM, so a static export
can never match an animated tile.

### The fix was in the comparison, not the renderer

`compare_map.py` now takes repeated `--mask-frame` arguments.  Any pixel that
differs between those real captures is **animated** and is excluded from the
match.  This is data-driven — no need to decode `bg_anim.asm` to know which
tiles animate.

```
python3 tools/compare_map.py data/submaps/2.png /tmp/anim/frame_0400.png \
  --expect 144,145 --radius 1 \
  --mask-frame frame_0400.png --mask-frame frame_0416.png ... --mask-frame frame_0560.png
```

### Result

```
animated px masked: 13453              (23 % of the 256x224 frame)
match              43020/43891 = 98.02 %
bg-only match      42662/43355 = 98.40 %
offset             (144,145)  delta (+0,+0)     <- camera formula confirmed
```

Because the animated area is masked out, every remaining pixel is static
background.  **98.02 % of all static pixels match the real hardware exactly in
5-bit colour space.**

The residual is 871 pixels — almost exactly the size of three or four 16×16
sprites (Cecil + the town NPCs), which the export cannot contain.  So:

> **The sub-map decode is correct.** Palette, tileset, BG2 composition, tile
> geometry and the camera are all verified.  Nothing should be "fixed" on
> account of the earlier 78 % — that number was animation + sprites.

### Progression, for the record
| measurement | value | what changed |
| --- | --- | --- |
| first attempt | 47 % | coarse sampling, wrong offset window |
| alignment pinned | 78.03 % | 1-px sweep, 5-bit compare |
| animated pixels masked | **98.02 %** | 8 animation phases excluded |

### Still to do
* `--dump-png` does not yet show the animation mask visually (it only shades
  the bg-only mask). Add it.
* Water/tile animation itself is unimplemented in the engine — cosmetic, and
  now provably isolated from the decode.
* Sprites are the only remaining static difference; verify by masking sprite
  rectangles once the actor is drawn by our renderer.
* Run the same 98 % check on a second, visually distinct map (Baron 36, a
  cave) before declaring the whole set good.

---

## 14. Over/under layers in the engine + end-to-end verification

### Engine changes (`src/platform/sdl2/world_view.c`)

Sub-maps now load **five** files: `.png`, `.over.png`, `.walk`, `.props`,
`.json`.  Added:

```c
static FF4_GfxSurface sm_over;      /* upper z-level cells, RGBA */
static uint8_t sm_props[2048];      /* 2 bytes per cell: tile byte1, byte2 */
static SDL_Texture *sm_over_tex;
```

Draw order inside a sub-map:

```
1. sm_tex          base (BG2 backdrop + BG1, index 0 transparent)
2. actor           skipped if byte2 & 0x04  (HIDE_Z: hide sprite)
3. sm_over_tex     upper z-level cells -> the player walks UNDER them
```

`sm_over_tex` uses `SDL_BLENDMODE_BLEND` (RGBA from stb_image, alpha 0 =
transparent), so only the overhang cells are painted.  `HIDE_BOTTOM`
(`byte2 & 0x08`, clip the lower 8 px of the sprite) is **not** implemented yet.

### Debug env vars added (for reproducible screenshots)
```
FF4_MAPX=<cell>      force the actor's x cell on a sub-map
FF4_MAPY=<cell>      force the actor's y cell
FF4_ZOOM=<float>     force zoom; 1.0 makes a shot directly comparable
FF4_SHOT_DELAY=<n>   wait n frames before writing FF4_SHOT
```
`FF4_SHOT` used to fire on the very first presented frame — i.e. **before the
warp applied**, so every screenshot showed the world map. `FF4_SHOT_DELAY` is
what makes engine screenshots usable as evidence. Default zoom is 3.0, which
also silently invalidated earlier comparisons.

### End-to-end result

```
SDL_VIDEODRIVER=dummy FF4_WARPTO=2 FF4_MAPX=16 FF4_MAPY=16 FF4_ZOOM=1 \
  FF4_EXPAND=0 FF4_WINSIZE=256x224 FF4_SHOT=/tmp/eng4.png FF4_SHOT_DELAY=40 \
  ./build/ff4_world

compare_map.py /tmp/eng4.png <real Kaipo frame> --expect 0,0 --radius 4 \
  --mask-frame <2 anim phases>
  match          43759/49431 = 88.53 %
  bg-only match  43414/48740 = 89.07 %
```

The engine reproduces the exporter's figure (88.7 %) at the same mask level,
and the exporter itself is **98.0 %** with a full 8-phase animation mask. So
**export, engine camera and on-screen rendering all agree with the hardware.**

### All 361 maps are in the build
Every map now has `.png`, `.over.png`, `.walk`, `.props`, `.json`
(361 × 5 files). The engine loads them by id and `FF4_WARPTO=<id>` or `W`
reaches any of them.

### Remaining
* `HIDE_BOTTOM` (clip lower 8 px) — not implemented.
* The **overhang cell selection is still unverified** (§12.2): `byte1 & 0x01`
  flags 415/1024 cells on Kaipo, which draws a lot of map over the actor. The
  correct rule is z-relative (compare tile z with the party's z), so expect to
  replace `is_upper()` in the exporter with a proper z test.
* Water/tile animation: mechanism decoded — `TfrBGAnimGfx` uploads 0x800 bytes
  from `$7F:5000 + (($7a & 0x18) << 4)`, advancing every 8 frames
  (`$7a & 0x06 == 0`), in 4 chunks of 0x80 bytes 0x200 apart, to VRAM $1200.
  `BGAnimTbl` (16 bytes, per tileset) gives the MapAnimGfx offset:
  `00 00 00 02 03 06 07 0a 0a 0a 0a 0a 0d 0d 0d 10`.
  Not yet implemented in the engine.

---

## 15. Validation sweep + the 4bpp sheet bug

### `tools/validate_map.py` — automated map validation

Captures a map from the running game and diffs it against our export in one
step: patch ROM → run ~1200 frames → use the last 9 frames as the animation
mask → `compare_map`.  Usage:

```
python3 tools/validate_map.py 0,2,13,30 [--spawn 16,16] [--radius 20]
```

### Results (after the fixes below)

```
map   0 Town of Baron   98.80 %      map  13 Lake           99.83 %
map   2 Kaipo           98.35 %      map  30 Astro Tower    99.89 %
map  54 2F              99.22 %      map  85 1F             99.03 %
map 102 Black Magic Cl. 99.22 %
mean 99.19 %, min 98.35 %, every camera offset (+0,+0)
```

Across 11 maps measured this session: **10 pass at 97–99.9 %**, one group fails.

### BUG FOUND: sprite sheet 1 is 4bpp, not 3bpp

`/FF4KSTER_LINUX/ff4kster/subs/read/readdata.bas`:

```
spritesheets(1).bpp = 4          ' Airship / Ship  <- ONLY 4bpp sheet
spritesheets(1).offset = &hADE00
```

Every other sheet is 3bpp.  Tilesets **1 and 16** map to sheet 1
(`case 1, 16: sprite_sheet = 1`), i.e. **25 maps**.  Our decoder assumed 3bpp
for everything: a 4bpp tile is **32 bytes** (planes 2/3 interleaved at +16),
not 24, with pixel values 0..15.

Effect: map 34 `18.60 % → 92.43 %`; maps 163/192/208 `~5 % → 26–31 %`.
Palette index uses `grp*8 + (c & 7)` — using the full 0..15 value or a
0x100-byte palette stride both score ~0 %, so the low 3 bits are what the
palette groups on.

### STILL FAILING (open): tileset-1 maps 163, 192, 208 at 26–31 %

4bpp moved them from ~5 % to ~30 %, so the bit depth was part of it, but
something else remains.  Ruled out:
* **animated tiles** — 0 of 4096 sub-sprites on maps 208/34 match the animated
  condition (`sprite_index` 0x120..0x12F with `lo & 0x20`), so that is not it;
* tileset base (`&hA2000 + &h400*(n-1)` matches FF4KSTER);
* sheet choice (`sheet_for` matches FF4KSTER's `case` statement exactly).
**Likely remaining cause:** the 4bpp → palette mapping. `grp*8 + (c&7)` throws
away bit 3 of the pixel value; it happens to be right for the two tile indices
map 34 uses (sidx 119..166) but map 208 uses 142 distinct indices 0..209.
Next experiment: probe palette formulas per-tile on map 208 rather than
wholesale (0x100 stride, `grp*16+c`, `grp*8+c` with c&8 selecting a second
bank).

### Also learned: the overhang and animated-tile rules (from FF4KSTER)

`types/gameobjects/methods/tileset.bas` decodes the per-sub-sprite control byte:

```
hi bit 0x20 -> "front"  : draw this sub-sprite ABOVE the actor  <-- the
                          overhang/priority bit we had been guessing at
hi bit 0x40 -> hflip is INVERTED
hi bit 0x80 -> vflip
lo bit 0x20 (+ sidx 0x120..0x12F) -> ANIMATED: graphic comes from
             spritesheets(n).animated_offset at
             (major*16 + minor + frame*4), major=(lo//4)%4, minor=lo%4
```

So the overhang is **per sub-sprite via `hi & 0x20`**, *not* the
`byte1 & 0x01` tile-prop guess currently in the exporter. `SHEETS_ANIM`
(headerless) is now in the exporter and `anim_tile_px()` implements frame 0.

---

## 17. THE PALETTE BUG: red/blue swapped on screen (fixed)

Reported by the user as "palettes are still wrong (bluish)" while the maps
rendered correctly.  That wording was the clue: a systematic blue cast means
**red and blue are exchanged**.

Root cause:
* `ff4_image_load()` returns `stbi_load(..., 4)` — bytes in memory are
  **R,G,B,A**, i.e. `SDL_PIXELFORMAT_ABGR8888`.
* The textures were created as `SDL_PIXELFORMAT_ARGB8888`, which expects
  **B,G,R,A** in memory, so SDL read red as blue and blue as red.
* **Screenshots hid it.** `SDL_RenderReadPixels(ARGB8888)` writes B,G,R,A and
  `ff4_png_write()` memcpy's those bytes straight into an RGBA PNG.  So the
  capture applied the *inverse* swap and the PNGs matched the hardware to
  97 % while the window was wrong.  Every "verified" visual check this
  session was blind to this.

Fix (both halves are required):
```
sm_tex, sm_over_tex, ff4_render_blit   ARGB8888 -> ABGR8888   (stb data)
SDL_RenderReadPixels(...)              ARGB8888 -> ABGR8888   (capture)
```
Internally-generated buffers (`win_rgba`, `sprite_blit`, world `vbuf`) are
built as `0xAARRGGBB` and correctly stay on `ARGB8888`.

How it was proven:
```
before: engine shot vs export 97.17 %   (wrong display, capture cancels it)
        texture fix only:       2.18 %   <- capture now shows the inverted image
        + readback fix:         97.17 %   <- display and capture agree
```

**Lesson: a screenshot is not evidence that the window is correct.** Any
future colour check must compare a capture against an independent reference
(such as the emulator frame), and the display format must be reasoned about
separately.

## 18. Collision debug toggle

`N` (or `FF4_NOCLIP=1`) disables collision in both `world_passable()` and
`submap_passable()`, so map contents and the overhang layer can be inspected
without geometry getting in the way.  The current field rule is
`passable = (tileprop byte1 & 0x0A) != 0`, which is still a guess and is the
next thing to check: if a specific cell blocks when it should not, read its
`data/submaps/<id>.props` bytes and compare with the z-level rules
(`byte1 0x01=UpperZ 0x02=LowerZ 0x04=Bridge`, `byte2 0x04=HideZ
0x08=HideBottom`).

---

## 19. Overhang rule settled: `hi & 0x20`, per sub-sprite

Reported by the user: "grass in Mist village renders on top of actor, but roofs
render under".  That is exactly what the old rule (`tileprop byte1 & 0x01`)
produces, and the tile dump proves it:

| tile | cells in Mist | appearance | `byte1&0x01` | `hi&0x20` |
| --- | --- | --- | --- | --- |
| 0 | 133 | **grass** | **flagged** (wrong) | not flagged |
| 16, 17 | 13 + 14 | **roof** (blue-grey shingles) | not flagged (wrong) | **flagged** |
| 19, 21, 22 | 41 + 587 + 20 | tree canopy / foliage | not flagged | **flagged** |

Authoritative source: `FF4KSTER/types/gameobjects/methods/tileset.bas`

```
tiles(i).sprite(j).front = iif(hi and &h20, true, false)
```

So `front` is **per sub-sprite of the 16x16 cell**, not per cell, and the
exporter now implements it that way (`draw_overhang()`): each quadrant is
drawn into `<id>.over.png` only when its own `hi & 0x20` is set.  A cell can
therefore have just its top half in front of the actor.

**A false start worth recording:** the first `hi&0x20` over-layer *looked* like
"grass everywhere", so I nearly rejected the correct rule.  It was actually
tree canopy, which legitimately draws over the player.  Rendering the
individual tiles (order 0/16/17/19/21/24) is what settled it — do that before
judging an over-layer from a composited image.

`draw_cells_rgba`/`is_upper` are gone; the `overhang_cells` JSON field now
counts distinct (tile, quadrant) pairs flagged front.

---

## 20. Collision fixed (two bugs) + viewport clip

### Bug 1: the passability mask was wrong -- grass was solid

The rule was `passable = (byte1 & 0x0A)`.  For Village Mist, tile 0 is **grass**
with `byte1 = 0x01`, so `0x01 & 0x0A == 0` -> the whole village floor was
impassable ("I can't move on grass tiles").  `0x0A` is `FF4_TP1_FLOOR`, a
memset/fill value, not a passability mask.

Correct rule: passable if **either z-level** is set:

```
passable = (byte1 & (FF4_TP1_UPPER_Z | FF4_TP1_LOWER_Z)) != 0
         = (byte1 & 0x03) != 0
```

Effect (cells passable of 1024): Baron 141 -> 561, Mist 74 -> 233,
Kaipo 410 -> 806.

### Bug 2: that made Kaipo's sea walkable

Kaipo's sea cells use **BG1 tile 50, which is completely transparent** (all
four sub-sprites are pixel 0; only BG2's water shows through).  Its props say
`byte1 = 0x02`, so the new rule called it passable and the player could walk
across open water.

Fix, in the exporter: a tile with **no opaque pixel** is "nothing here" and is
forced impassable (`walk[i] = 0`).  Kaipo drops 806 -> **542** passable, i.e.
the 264 sea cells are blocked again.

Reasonable results after both fixes: Baron 545, Mist 230, Kaipo 542,
Lake 78, Astro Tower 43, Black Magic Class 33 (of 1024).

### Actor no longer drawn outside the viewport

In classic mode the 256x224 view is letterboxed inside the window and the
surrounding black is not map, but `draw_actor()` was called unconditionally so
the sprite could be drawn out there.  Both draw paths (sub-map and world) now
skip the sprite when its rect does not intersect the viewport rect.

### Open: overhang z-flicker

Reported as "1 frame they are on top and then it is drawn on top of them".
Draw order is correct and static (base -> actor -> overhang) and the textures
are created once, so this is most likely the authentic **1-px walk bob**
(`$06 = ($7b & 8) >> 3`): the sprite rises one pixel on alternating frames and
pokes above a canopy/roof edge.  Needs confirming by standing still.

## 21. Collision overlay (C) — stop guessing, look

`CheckTilePass` (`third_party/ff4/field/move.asm`) is the real routine:

```
if event running                -> blocked
if CheckNPCBlock                -> blocked
y = direction * 2
if player is on a bridge ($a1 & 0x04):
     passable iff ((tileprop[y] & 0x03) & $d2) != 0     ; z-level match
else
     passable iff (tileprop[y] & 0x0A) != 0
if (tileprop[y] & 0x04) -> passable                     ; bridge tile
else                    -> blocked
```

Two problems with using this directly:
* it reads `$06a1`, a **derived per-direction table** whose writer has not been
  found in the disassembly — it is **not** a straight copy of
  `MapTileProp[].byte1`;
* our maps in fact differ: e.g. Weapon Shop floor is `byte1 = 0x01`
  (`& 0x0A == 0` -> would be blocked) and Kaipo's sea is `byte1 = 0x02`.

FF4KSTER has a `passage` flag computed as **all four sub-sprites having
`front` set** (`tileset.bas`), but that marks Mist's tree canopy passable and
its grass solid, which contradicts what the user sees. So `front` is probably
not a passability bit either.

**Decision: stop inferring.** Press **C** (or `FF4_COLL=1`) to tint the
viewport green = walkable, red = blocked, using the current table. Any cell
that is visibly wrong can then be reported by map id, and its tile index read
out of `data/submaps/<id>.props`. Current rule is
`passable = (byte1 & 0x03) && tile has at least one opaque pixel`.

Known-good by eye so far: Weapon Shop (8) shows the room floor green and the
walls/void red; Kaipo's sea is blocked again after the blank-tile fix.

## 22. Collision: reverted to `byte1 & 0x03` (state the user could test)

Sequence of mistakes, recorded so they are not repeated:

1. Original rule `byte1 & 0x0A` -> grass (`byte1 0x01`) solid; user: "I can't
   move on grass tiles".
2. Changed to `byte1 & 0x03` -> user: "collision is almost there but I can
   walk into shop walls".
3. Dumped `$06a1` from the running emulator (`FF4_RAM=0x06a0:16`) and proved
   it is a **straight copy of `MapTileProp` byte1/byte2** for the four
   neighbouring cells (all eight values matched the ROM exactly).  Concluded
   0x0A was "the real rule" and reverted to it.
4. That made **nearly all** previously-walkable ground solid — the user
   reported collision as "almost entirely reversed" and asked to revert.

So: the table *location* is proven, but **`(byte1 & 0x0A)` is demonstrably not
equivalent to what the game lets the player walk on**.  `0x03` is the closest
behaviour so far.  Current state matches what the user could test:
`passable = (byte1 & 0x03) && tile has at least one opaque pixel`.
Passable cell counts: Baron 545, Mist 230, Kaipo 542, Weapon Shop 22 of 1024.

### Do not infer the mask again — measure it

The correct next move is to stop fitting bit masks and let the game answer:
drive the actor around a map in the emulator, read `$1706`/`$1707` each frame,
and **flood-fill the reachable set**.  That yields ground truth per map from
which the real rule can be fitted (or the reachable set exported directly).
The harness can already hold a direction (`holdAt`/`holdLen`/`holdButton`) and
read RAM (`FF4_RAM`), so this is mostly plumbing.

Tools available for reporting bad cells meanwhile:
* **C** — overlay: green = walkable, red = blocked (display only).
* **V** — prints the actor's cell + four neighbours (map id, coords, byte1,
  byte2, verdict) to stderr and appends to `ff4_cell.txt`.
* `FF4_CELLDUMP=1` — same dump automatically, for headless runs.

## 23. Negative result: the emulator cannot measure walkability yet

Tested the flood-fill idea on the Weapon Shop (map 8, only 22 walkable cells —
small enough that a wrong answer is obvious).  **The method does not work.**

Setup: patch retail to spawn at (4,9) in the shop, hold one direction for 150
frames starting at frame 600, then read `$1706`/`$1707`.

```
hold RIGHT (bytes say blocked) -> still (4,9)
hold DOWN  (bytes say FREE)    -> still (4,9)     <-- even the open one
hold UP    (disputed)          -> still (4,9)
hold LEFT  (blocked)           -> still (4,9)
```

Diagnosis from RAM:
* `$1705` (player direction) stays **0** — the movement code never runs;
* `$03` (joypad state) stays **0** — and `$00`–`$0B` are all zero, i.e. the
  game is not updating its scratch RAM at all;
* yet the screen renders the map correctly (~98 % match to hardware).

So the stub (`jsl LoadMap` -> `jml MainLoop`) gets the map **drawn** but leaves
the game in a **non-interactive state**.  `LoadMap` alone is not what a real
warp does; the real path goes through the event system and the field-entry
sequence.

Input injection itself is fine: on the **unpatched** retail ROM, pressing START
at frame 400 takes the title screen (`5fd4d5e2`) to black (`ea339d0c`), i.e.
the game advances.  (`retro_set_controller_port_device(0, JOYPAD)` was added
while chasing this and is correct to keep, but was not the cause.)

### Consequence
Do **not** build the flood-fill measurement on the current stub.  To get ground
truth on collision, the stub must first land in the same state a genuine warp
produces (event-driven map load + fade-in), which is a research task.  Until
then collision stays the documented approximation:
`passable = (byte1 & 0x03) && tile has at least one opaque pixel`.

## 24. Passability is PER TILE — and V now names the tile

The user's framing is the right model: as in RPG Maker, **collision belongs to
the tile**, not to the cell.  "If one tile of a wall is impassable, they all
are."  So the job is a small table `(tileset, tile index) -> passable`, not a
per-cell bitmap.

### FF4KSTER does not hand us the answer
Its editor computes `passage = all four sub-sprites have hi & 0x20`
(`tileset.bas`) and tints the map with it.  Measured against real maps that
rule is implausible -- Baron 70 walkable cells, Village Mist **667**, Kaipo
**9** -- so `passage`/`front` is not the collision flag.  Comparison table
(cells of 1024):

| map | FF4KSTER passage | `byte1 & 0x03` | `byte1 & 0x0A` |
| --- | --- | --- | --- |
| Baron 0 | 70 | 561 | 141 |
| Mist 1 | 667 | 233 | 74 |
| Kaipo 2 | 9 | 806 | 410 |
| Weapon Shop 8 | 33 | 22 | 2 |

Useful side-effect: the interior maps (8, 13, 30, 102) agree closely between
`passage` and `0x03`, which supports "close, but a few tiles are mis-tagged".

### New data: `<id>.tiles`
The exporter now also writes one byte per cell = **tile index** (255 = empty),
and the engine loads it into `sm_tileidx[1024]`.  The **V** dump therefore
reports the tile, not just its properties:

```
[cell] map 8 at (5,5)
  cur   ( 5, 5) tile= 34 byte1=0x01 byte2=0x00  pass=NO
  up    ( 5, 4) tile= 25 byte1=0x00 byte2=0x20  pass=NO
  right ( 6, 5) tile= 35 byte1=0x01 byte2=0x00  pass=NO
  down  ( 5, 6) tile= 54 byte1=0x01 byte2=0x00  pass=NO
  left  ( 4, 5) tile= 34 byte1=0x01 byte2=0x00  pass=NO
```

### The workflow we agreed on
1. Walk to a cell that is tagged wrongly, press **V**.
2. Report the tile line(s); `ff4_cell.txt` accumulates them.
3. Because collision is per tile, each report classifies a **whole tile of a
   tileset**, so a handful of reports per tileset fixes entire maps.

Open question to resolve from those reports: whether passability can be
expressed from the two prop bytes at all, or whether some tiles need an
explicit per-tileset override list (`data/collision.json`).

## 25. Collision SOLVED from the user's report: HIDE_Z marks overhead tiles

The user walked Baron with the overlay on and reported 15 cells that were
**green but should be red**.  Every single one shared one property:

```
tile  75 byte1=0x02 byte2=0x0C
tile  76 byte1=0x02 byte2=0x0C
tile  83 byte1=0x03 byte2=0x04
tile  84 byte1=0x03 byte2=0x04
tile  99 byte1=0x02 byte2=0x0C
tile 109 byte1=0x03 byte2=0x04
                                   ^^^^ all have  byte2 & 0x04
```

`byte2 & 0x04` is `FF4_TP2_HIDE_Z` — "hide the sprite / drawn over the actor",
i.e. **roofs, archways, overhead structures. You do not walk on those.**

### The rule

```
passable = (byte1 & 0x03)                 either z-level (UPPER_Z | LOWER_Z)
        && (byte2 & 0x04) == 0            not an overhead tile
        && tile has at least one opaque pixel    (blank BG1 = void / sea)
```

The `.walk` file now stores the **derived verdict (1/0)**, so the rule lives in
one place (`tools/ff4_export_submaps.py`) and the engine only tests `!= 0`.

### Why this is credible

Blocking every Baron tile with `byte2 & 0x04` removes exactly 15 tiles / 77
cells, and **6 of those 15 are precisely the tiles the user reported**
(75, 76, 83, 84, 99, 109); the other 9 are the same kind of thing (roofs and
arch bits: 51, 52, 56, 77, 98, 100, 116, 13, 14).  Interiors are untouched,
which matches "interiors were already close":

| map | `byte1 & 0x03` alone | with `!(byte2 & 0x04)` |
| --- | --- | --- |
| Baron 0 | 561 | **484** |
| Mist 1 | 233 | **191** |
| Kaipo 2 | 806 | **514** |
| Weapon Shop 8 | 22 | **21** |
| Astro Tower 30 | 43 | **42** |

With the overlay on, Baron now shows green open ground and red buildings --
the map reads correctly at a glance.

### What is still needed
Reports from **interiors and Kaipo** (which the user has not examined yet).
Press V wherever the overlay looks wrong; each report classifies a whole tile
of a tileset.  If a tile appears that cannot be expressed by these two bytes,
add it to an explicit override table (`data/collision.json`).

## 26. Collision is a PER-TILE TABLE — the two prop bytes cannot express it

Two reports from the user, both in Town of Baron, settled this:

* **report 1** — 15 cells green but should be red: tiles
  **75, 76, 83, 84, 99, 109**
* **report 2** — cells red but should be green: tiles
  **100, 77, 56, 116, 14, 13, 50, 122**

Both sets have the *same* `byte1`/`byte2` shape (`byte1 & 0x03` set,
`byte2 & 0x04` set in most cases).  **Identical bytes, opposite behaviour.**
No test on those two bytes can separate them, so any bit rule will always get
one of the two sets wrong.  This is the case the handoff anticipated; the
answer is an explicit per-tile table.

Also note the key inference that makes reports unambiguous: **every `cur` line
in `ff4_cell.txt` is a cell the actor is standing on**, so `pass=NO` on a
`cur` line is *definitively* a bug — no judgement call needed.

### `data/collision.json`

```json
{ "tilesets": { "5": { "impassable": [75,76,83,84,99,109],
                       "passable":   [122] } } }
```

`tools/ff4_export_submaps.py` loads it (`load_collision()`) and applies it
after the default rule.  Default is `passable = (byte1 & 0x03) != 0`; the
blank/opaque rule and the `!(byte2 & 0x04)` rule are **both removed** — they
each explained one report and broke the other.  Tile 122 is explicit because
`byte1 = 0x00` yet the actor stood on it (a map-edge trigger tile).

`.walk` stores the final verdict, so the engine only tests `!= 0`; all the
policy lives in the exporter.

Baron: 561 -> 527 (the six impassable tiles) -> **529** (122 restored).

### Growing the table
Reports are cheap and per-tile: walk with **C** on, press **V** anywhere the
overlay is wrong, send `ff4_cell.txt`.  A `cur` line with `pass=NO` is a
passable override; a `cur` line with `pass=yes` that should be solid is an
impassable override.  Because a tile is shared across every map using that
tileset, each report fixes large areas at once.

## 27. RESEARCH: the aquifer tile is a Z-LEVEL rule, not a flat passability

User's report: a small water/aquifer tile in Baron where you can enter from one
side, move around inside it, but cannot walk out — and cannot enter from the
side directions.  A single passable/impassable cell cannot express that.

### What the disassembly actually does

**`$d2` is the player's current z-level**, and it is derived from the tile the
player is *standing on* (`third_party/ff4/field/move.asm` ~line 138):

```
        lda     $a1
        and     #$04
        bne     @a965        ; on a BRIDGE tile -> do NOT update z
        lda     $a1
        and     #$03
        sta     $d2          ; z = current tile's z bits (UpperZ 0x01 / LowerZ 0x02)
```

`$a1` is a buffer of the 2-byte tile props for the current cell and its four
neighbours (`CheckNPCBlock` does `lda $0709 / tay / asl / tax / lda $a1,x`), so
`$a1 & 0x04` / `& 0x03` are the *current* tile's bridge and z bits.

**`CheckTilePass` then uses `$d2`:**

```
if player is on a bridge ($a1 & 0x04):
        passable iff ((next & 0x03) & $d2) != 0     ; destination z must match
        else passable iff (next & 0x04) != 0        ; or destination is a bridge
else:
        passable iff (next & 0x0A) != 0
```

`ClearPlayerNPCMap` uses the same z-match idiom, so z is a first-class part of
movement, not a per-tile yes/no.

### Consequence for our engine

This cannot be stored as a boolean in `.walk`.  To reproduce the aquifer we
need:

1. a runtime **`player_z`** in the engine, updated exactly as above (from the
   tile being entered, held while on a bridge);
2. the per-direction test to compare the *destination's* z bits against
   `player_z` (with the bridge exception);
3. `.props` (already exported, both bytes per cell) as the data source, so the
   decision moves from `.walk` into the engine.

`.walk` stays useful as the *flat* verdict for the overlay, but the actual
movement test must consult `.props` and `player_z`.

This also explains why the earlier flat rules kept half-working: a water tile's
`byte1` z bits (0x01/0x02/0x03) are exactly the values `$d2` is built from.

## 28. z-level movement model implemented (and what it does NOT fix)

`src/platform/sdl2/world_view.c` now tracks `player_z` and applies the
`CheckTilePass` logic:

```c
cur = byte1(current cell);
if (!(cur & 0x04)) player_z = cur & 0x03;    /* rebuilt while on normal ground */
if (cur & 0x04) {                             /* on a bridge / walkway */
        if ((next & 0x03) & player_z) return 1;
        return (next & 0x04) != 0;
}
return walk[next] != 0;                       /* flat verdict + overrides */
```

**Important caveat found while wiring it up:** in tileset 5 only **tiles 13 and
14** have `byte1 & 0x04`, so the z rule only engages on those.  The aquifer
tile the user described (tile 50, `byte1 = 0x02`) is *not* a bridge tile, so
this change does **not** reproduce the in/around/no-way-out behaviour for it.

So: the model is now correct for the bridge/walkway class, and the aquifer
will need either
* a per-tile override (if its behaviour is effectively fixed), or
* an additional rule once we know which byte marks "water you can enter but
  not leave" — the user's next reports will tell us, since they can see it.

Passability fine-tuning therefore continues exactly as before, and is now
cheaper: **C** to look, **V** to record, `data/collision.json` to correct.
Type tests: 11/11 suites pass, zero warnings.

## 29. Water z-level gating (Kaipo)

User report (map 2, Kaipo): tiles **36, 46, 48, 49** (`byte1=0x02`,
`byte2=0x08`) are **passable but sit at the water z-level** -- you can move
around inside the water, but not walk in or out except at the ramp.
Tile **14** (`byte1=0x05`, i.e. `0x04` walkway bit) is that ramp.

`byte2 & 0x08` is `FF4_TP2_HIDE_BOTTOM`; in practice it marks the water class.

Implemented in `submap_passable()`:

```c
player_z = cb1 & 0x03;              /* rebuilt unless on a walkway */
if (cb1 & 0x04) return 1;           /* walkway/ramp: free transition  */
if ((cb2 & 0x08) || (nb2 & 0x08)) { /* either side is water-z        */
        if ((nb1 & 0x03) & player_z) return 1;   /* same level       */
        return (nb1 & 0x04) != 0;                /* or onto a ramp   */
}
return walk[next] != 0;             /* flat verdict + tile overrides  */
```

This gives the reported behaviour: move freely within the water (same z),
blocked between water and land (z differs), transition only via the ramp.

Verified: in Kaipo all five tiles are `walk=1` (passable) and exactly one ramp
cell (tile 14) exists, so the whole aquifer is reachable only through it.

**Still approximate:** `cb1 & 0x04` currently allows stepping off a ramp in
any direction; the game may restrict that further.  Also unverified is whether
water-to-land moves should ever be allowed without a ramp (e.g. shore tiles).
Further C/V reports in Kaipo will settle both.

---

## 30. BRIDGE TILES ARE Z-RELATIVE — to implement (research note from user)

Reported by the user observing a bridge tile in Kaipo.  This is the clearest
statement of the mechanic we have, and it should drive the next iteration.

### Behaviour

A tile with `byte1 & 0x04` (the walkway/bridge bit) is **passable from two
different z-levels**, and what it does depends on which one the player is on:

| player's z | movement | rendering |
| --- | --- | --- |
| **ground level** | ground -> bridge -> ground (walk across it) | normal; the player is drawn ON the bridge |
| **water level** | water -> bridge -> water (pass under it) | the bridge is drawn **ABOVE the player** |

So the *same tile* is a walkable surface from above and an overhang from
below.  Passability is the same in both cases — only the draw order and the z
bookkeeping differ.

### Movement already matches this

`move.asm` updates the player's z **only when not standing on a walkway**:

```asm
        lda     $a1
        and     #$04
        bne     @a965        ; on a bridge -> do NOT update z
        lda     $a1
        and     #$03
        sta     $d2
```

Because z is *preserved* while crossing a bridge, the player keeps whichever
level they arrived at: come from ground and you stay at ground height across
it; come from water and you stay at water height under it.  Our
`submap_passable()` already replicates this (it only rebuilds `player_z` when
`!(cb1 & 0x04)`), so **no movement change is needed** for bridges.

### What is still missing: conditional overhang rendering

Today the overhang layer (`<id>.over.png`) is a **static** per-sub-sprite split
(`hi & 0x20`).  A bridge needs the opposite: a **dynamic** decision made at
draw time, per cell:

```
if tile has (byte1 & 0x04)                 /* bridge / walkway */
    draw that cell above the actor  iff  player_z != (byte1 & 0x03)
```

i.e. the bridge is drawn over the player only when the player is at the
*z below* the bridge's own surface z.

Implementation sketch:
1. keep a per-cell flag "is walkway" (already derivable from `.props` byte1);
2. at draw time, for cells the actor overlaps, if the cell is a walkway and
   `player_z != (byte1 & 0x03)`, blit those cells **after** the actor;
3. the static `.over.png` continues to handle `hi & 0x20` art (roofs, canopy);
   the two passes are independent and can both apply.

`.props` (2 bytes/cell) plus the runtime `player_z` already carry everything
needed — this is a renderer change, not a data change.

### Also still open
* whether stepping off a ramp/bridge is direction-restricted (we currently
  allow any direction);
* whether water->land is ever legal without a bridge (shore tiles) — unreported
  so far;
* **next task per the user: move to the next map and report flat
  impassability conflicts** (interiors, tileset 6, are still completely
  unreported).

---

## 31. Directional passability (beds) — implemented

Report (map 30, Astro Tower, tileset 6): tiles **19 and 20** are bed tops.
You can enter them from the left or right but not from the top, and inside you
can leave left/right but not upward.  A flat passable/impassable flag cannot
express that.

### Data: `data/collision.json` gained `dirs`

```json
"6": { "dirs": { "19": "lr", "20": "lr" } }
```

Bits `1=up 2=right 4=down 8=left`; `"lr"` == `0x0A`.  The exporter writes a
per-cell mask to `<id>.dirs` (1024 bytes, `0x0F` = unrestricted) and the engine
loads it into `sm_dirs[1024]`.

Enforced in `submap_passable()` **in both directions**:

```c
int bit = 1 << dir;
if (!(sm_dirs[src] & bit) || !(sm_dirs[dst] & bit))
        return 0;
```

Applying it to *leaving* as well as *entering* is what makes the bed block
up/down both ways, which is what was observed.

### Final data set per sub-map
```
<id>.png       base (BG2 backdrop + BG1, index 0 transparent)
<id>.over.png  static overhang (per sub-sprite, hi & 0x20)
<id>.walk      flat passability verdict (1/0)
<id>.props     2 bytes/cell: tile byte1 (z/bridge/door), byte2 (occlusion)
<id>.tiles     tile index per cell (255 = empty)
<id>.dirs      allowed-direction mask per cell
<id>.json      metadata
```
All 361 maps regenerated.  12/12 suites, zero warnings.

**The user reports no further obvious collision conflicts in the maps provided.**
Collision is therefore at a good-enough state: flat tiles, z-gated water/bridge
tiles, and directional tiles are all expressible.

---

## 32. NPCs with dialogue (first pass)

### Data
New per-map file `<id>.npc`, one line per NPC:

```
x y dir pal sheet<TAB>dialogue text
```

Sources: `npcProperties[mapProperties[mid].npc]` for placement/facing/palette,
`mapDialog[mid]` for the lines (the real script, e.g. "\char00 of the Red
Wing! Misty Valley is beyond the cave of northwest."), and
`npcGraphicsProperties` for the sprite sheet.

**KNOWN GAP:** `npcGraphicsProperties` has only 512 entries but there are
~1223 NPCs in total, so the sheet index is *not* a simple running count.  The
exporter currently uses a running offset and clamps to `0..106`; sprites may
therefore be wrong for later maps.  Re-derive the real index from the ROM.

### Engine
`src/platform/sdl2/world_view.c`:
* `sm_npc[SM_NPC_MAX]` holds `x, y, dir, pal, sheet, tex, text`;
* `<id>.npc` is parsed at `submap_load()`;
* each NPC gets a 16x16 texture built once (in the `sm_tex_dirty` block) via
  `sprite_load(sheet, pal)` + `sprite_blit()`; the player's sheet is restored
  afterwards, because `sprite_load` mutates globals;
* NPCs are drawn **under** the player, clipped to the viewport;
* NPC cells block movement (`submap_passable` returns 0);
* **E** talks to the NPC in the facing cell: its text is copied into
  `dialog_pages[0]` and the existing window opens with the `$df` reveal.

`dialog_pages` changed from `const char *[]` to `char [8][320]` with a
`dlg_page_count`, so NPC text can replace it.

### Status
Verified: 361 maps export NPC data (Baron 11, Kaipo 12); the engine loads and
draws them, blocks them, and `E` prints/queues the correct line.
Not verified: sprite correctness per NPC (see the graphic-index gap above);
some sprites render black, which is consistent with a wrong sheet index.

### 32a. Two fixes after the first NPC pass

1. **NPC sprites painted an opaque black 16x16 tile around the character.**
   `sprite_blit()` writes alpha 0 for colour 0, but the texture was created
   without a blend mode, so alpha was ignored.  Fixed with
   `SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND)`.
   Verified at 6x zoom: the ground shows through correctly.
   (Same class of bug as the red/blue swap in §17 -- a *display* setting, not
   a data problem.)

2. **E did not page dialogue.**  `E` opened a conversation but then did
   nothing.  It now mirrors `T` when a window is open:

```c
if (dialog_open) {
        if (dlg_lines < dlg_lines_max) { dlg_lines = dlg_lines_max; }
        else if (++dialog_page >= DIALOG_PAGE_COUNT) dlg_closing = 1;
        else dlg_reveal = 0;
        break;
}
/* otherwise: look up the NPC in the facing cell and open its text */
```

   So E = talk, then E = finish the reveal, then E = next page, then E =
   close.  `T` still works as before.

### 32b. Dialogue + viewport cleanups

1. **`T1` at the bottom of the text box** was a debug page indicator
   (`snprintf(pg, "T%d", dialog_page + 1)` drawn bottom-right). Removed.
2. **Text now clears before the close animation.**  New `dlg_hide_text` flag
   is set the moment `dlg_closing` begins and `draw_dialog()` returns before
   drawing glyphs, so the box is empty as it shrinks away.
3. **NPCs spilled into the letterbox.**  The per-sprite test only rejected
   sprites *fully* outside the viewport, so a partially overlapping NPC was
   drawn past the edge.  Fixed properly with
   `SDL_RenderSetClipRect(ren, &d)` around the whole sub-map draw
   (map + NPCs + actor + overhang), reset afterwards.  This also protects the
   actor and any future layer.
4. **Empty NPC sprites.**  Some NPCs have no visible graphic (the user marked
   such a cell in `ff4_cell.txt`: impassable with no sprite present).  At
   texture-build time each 16x16 buffer is scanned; if every pixel is
   transparent the NPC is flagged `empty` and is then **neither drawn nor
   used for collision**.

### 32c. NPC palettes pinned + NPC movement

**Palette mapping (pinned from the data).** `npc.palette` is a selector in
**0..3**, not 0/1; `npcPalette1` is in {0,2} and `npcPalette2` in {0..9};
`npcPalettes` has 36 entries.  So:

```
pal = (selector & 1) ? map.npcPalette2 : map.npcPalette1
```

Using the selector directly (or a bare 0/1 test on a 0..3 value) is what made
116 of 383 maps show wrong NPC colours.

**Not all NPCs move — measured over all 1223:**

| property | count |
| --- | --- |
| randomMovement = **false** | **1077** |
| randomMovement = true | 146 |
| animated = true | 708 |
| speed 0 / 1 / 2 / 3 | 144 / 117 / 726 / 236 |

So only ~12 % wander; the rest stand and face `direction` (guards,
shopkeepers, story characters).  The `.npc` line therefore carries the flag
per NPC:

```
x y dir pal sheet random speed<TAB>dialogue
```

**Movement (`npc_update`, called once per frame when on a sub-map):**
* only `random == 1` NPCs move; static ones keep their facing;
* a step is 16 px at 1 px/frame, interpolated when drawing
  (`px = cell*16 + dir*step_px`), then a rest of 44/32/22/14 frames by speed;
* `npc_tile_free()` gates each step: flat `.walk` verdict, not the player's
  cell, not another NPC's cell.  It deliberately does **not** call
  `submap_passable()`, because that mutates `player_z` for the *caller*;
* blocked NPCs face the attempted direction and wait 12 frames, which is what
  makes them look alive instead of frozen.

### 32d. NPC idle animation + NPCs use the REAL collision rules

1. **Idle animation.** `npcProperties[].animated` is true for **708 of 1223**
   NPCs, so standing still does not mean static: they cycle their second
   frame.  The `.npc` line grew to eight fields
   (`x y dir pal sheet random speed animated`) and the engine builds **two**
   textures per animated NPC (frames `dir*2` and `dir*2+1`), choosing with
   `(frame_counter >> 4) & 1` while idle and the alternate frame while
   stepping.  `frame_counter` is a global — the game uses `$7a` for NPCs and
   `$7b` for the player, which is why they are not in sync.

2. **NPCs now obey exactly the player's collision rules.**  This was a real
   bug: `npc_tile_free()` only consulted the flat `.walk` byte, bypassing
   z-levels, water gating, direction masks and per-tile overrides.

   Fixed by making the collision test **pure** and giving it the caller's z:

   ```c
   static int tile_passable(int cx, int cy, int dir, int *zp);   /* no side effects */
   static int submap_passable(...) { return tile_passable(..., &player_z); }
   static int npc_can_step(self, dir) { int z = npc->z;
                                        return tile_passable(npc->x, npc->y, dir, &z); }
   ```

   Each NPC keeps its own `z`, initialised from the tile it stands on, so a
   wandering NPC respects bridges, water and beds exactly as the player does.
   (`player_z` was previously mutated by whoever called the function, which is
   why the NPC path had to bypass it.)

3. **NPC palettes** remain at `pal = (selector & 1) ? npcPalette2 :
   npcPalette1` (selector is 0..3).  **Still unverified against hardware for
   Baron** — the earlier reference captures are gone from `/tmp`.  Next step:
   `validate_map.py 0` produces a reference frame plus the exact camera
   offset, then an engine screenshot with `FF4_MAPX/MAPY=16,16` and
   `FF4_ZOOM=1` lines up 1:1, and the 16x16 cell of any static NPC can be
   diffed directly.

### 32e. NPC facing fixed + palette search keys

**Facing was broken.** The sprite was built once at load from the NPC's
*initial* `direction` and never changed, so a walking NPC kept its first
facing and appeared to walk backwards.  Now each NPC builds **8 textures**
(`tex[facing*2 + frame]`, facings 0..3 x frames 0..1) and drawing picks:

```
alt = step_px > 0 ? 1 : (frame_counter >> 4) & 1    /* animated only */
tex[facing*2 + alt]
```

`npc_update()` already sets `facing = d` when a step begins, so the sprite now
turns as it moves: right/left/up (back) /down (front), same as the player.

**Palette search.** NPC palettes are still not confirmed, so there is now a
way to find them by eye:

| control | effect |
| --- | --- |
| **H** | NPC palette shift -1 |
| **J** | NPC palette shift +1 |
| `FF4_NPCPAL=<n>` | starting shift (0..35) |

Every NPC palette becomes `(pal + shift) % 36`; the current shift prints as
`[npcpal] shift N` and the textures rebuild immediately
(`sm_npc_dirty` forces the rebuild path).  Verified: shift 0/3/7 produce three
different frames.

**What to report back:** the shift value at which Baron's NPCs look right.  If
a single shift fixes every map, the resolution rule is simply an offset; if
different maps need different shifts, the mapping is per-map and needs the
real index rather than an offset.

### 32f. On-screen NPC palette readout

The first attempt put the readout **inside the warp-HUD conditionals**, so it
only drew while the warp banner was visible -- which is why `K` appeared to do
nothing and nothing was ever on screen.

Now drawn **unconditionally** just before `SDL_RenderPresent`:

* dark translucent backing bar so it reads over any map colour;
* the shift as **two large digits at 4x** (`%02d`), because the 8x8 debug font
  makes words mushy but a big digit is unambiguous;
* small grey "PAL" label after the digits;
* positioned at `win_h - 8*4 - 10` so the 32-px-tall digits fit (the first
  placement at `win_h - 28` clipped them).

Controls: **J** = shift +1, **H** = shift -1, **K** = toggle readout,
`FF4_NPCPAL=<n>` = starting shift.  H/J force the readout on.

Ask the user for the shift value at which NPC palettes look right; that single
number decides whether the mapping is a global offset or per-map.

### 32g. NPC palette pinned (5) + the REAL NPC graphic layout found

**Palette: shift 5 is correct for Baron** (the user read it as "QS" from the
blocky 4x digits -- 0 renders like Q and 5 like S).  `npc_pal_shift` now
defaults to 5.

**The NPC record layout is solved.**  `field/data/npc_prop_en.asm` defines
`NPCProp` (`.define NPCPropSize $13E8`, `NPCPropArrayLength 384`); records are
**4 bytes**, and the ROM table sits at file offset **0x098300**.  Verified
against map 0 byte-for-byte:

```
rec  switch0   x    y   graphic      sheet = npcGraphicsProperties[graphic]
 0      45      3   19    0xC0  -> 27
 1      46     14   16    0xC0  -> 27
 2      47     11   27    0x75  -> 33
 3      48      8   22    0x35  -> 10
 5      50     16   18    0x70  -> 106
```

So `npcGraphicsProperties` is indexed by the **record's 4th byte**, not by a
running NPC count -- which is exactly why sprites were wrong (Cecil appearing
at the dungeon warp, a white mage where a dancer belongs).  The old guess
`gp[running_index]` produced 0/101/101/24/24 where the truth is 27/27/33/10/25.

x and y carry flags in their high bits (`x & 0x1F`, `y & 0x1F`).

**Still to fix:** the per-map block offset.  A simple running sum of the rip's
NPC counts drifts by a byte (map 1's block starts at `BASEP + 11*4 + 1`), which
misaligns 201 of 202 blocks.  Until that is pinned, derive the graphic by
matching each NPC's (`switch0`,`x`,`y`) inside the table instead of trusting an
arithmetic offset.

### 32h. NPC sprites FIXED: graphic id recovered from the ROM records

`tools/ff4_export_submaps.py` now reads each NPC's graphic id out of the
`NPCProp` table at file offset **0x098300** instead of guessing a running
index:

```python
def find_npc_graphic(rom, sw, x, y):
    # records are [switch0, x|flags, y|flags, graphic]
    for off in range(0x098300, 0x098300 + 0x13E8 - 4):
        b = rom[off:off+4]
        if b[0] == sw and (b[1] & 0x1F) == x and (b[2] & 0x1F) == y:
            return b[3]
```

Matching on the NPC's identity rather than an arithmetic offset sidesteps the
block drift (a running sum of NPC counts is off by one byte for nearly every
map).  `(switch0, x, y)` was **unique for every NPC tested** on maps 0, 1, 2,
so there is no ambiguity to resolve.

Then `sheet = npcGraphicsProperties[graphic]`.

Baron town, before -> after:

```
(3,19)  0 -> 27      (14,16) 101 -> 27     (11,27) 101 -> 33
(8,22) 24 -> 10      (15,25)  24 -> 25     (16,18)  86 -> 106
(21,13) 29 -> 19     (27,7)  29 -> 17      (23,5)  32 -> 31
```

This is what produced a Cecil sprite at the tunnel warp and a white mage where
the dancer belongs -- both were wrong sheet indices.

**Palette: shift 5** confirmed by the user (read as "QS" because the 4x debug
font draws 0 like Q and 5 like S).  It is now the default.

### 32i. NPC graphic ids — AUDITED, and my two earlier answers were wrong

I had this wrong twice.  This version is verified three independent ways.

**The chain, from the disassembly:**

```
InitNPCs:   lda $0fde            ; the map's NPC-LIST index (not the map number)
            ...                  ; index*2 (+2 if $1701, the map's MSB)
            lda f:NPCPropPtrs,x  ; -> 16-bit offset into NPCProp
            lda f:NPCProp,x      ; BYTE 0 of the record
            jsr LoadNPCGfx
LoadNPCGfx: ldx $3d              ; that byte (+256 if $1701)
            lda f:NPCGfxID,x     ; -> sprite sheet id
```

So the record is **[graphic, x|flags, y|flags, flags]**, byte 0 is the
graphic id (the rip labels that byte `switch0`, which is what misled me), and
the +256 bank comes from the **map's** `npcMSB`, not the per-NPC flag.

**Three confirmations:**

1. `NPCPropPtrs` is at file **0x098000**, `NPCProp` at **0x098300**.  Indexing
   the pointer table by the NPC-list index reproduces **all 1223 records with
   0 mismatches** (switch0/x/y agree byte-for-byte).
2. `field/data/npc_gfx_id_en.asm`: `NPCGfxIDArrayLength 512`, first bytes
   `$00,$65,$65` — identical to the rip's `npcGraphicsProperties`, so that
   array **is** NPCGfxID, and 512 = two 256-entry banks.
3. Every graphic id (1610 NPCs across the export) lands inside 0..511.

**Earlier wrong answers:** (a) a running NPC index, (b) the record's 4th byte.
Both produced plausible-looking numbers and both were wrong.

Baron town now: (3,19)->101, (14,16)->101, (11,27)->17, (8,22)->20,
(15,25)->24, (16,18)->18.  Note (14,16) and (15,25) coincidentally match the
old guess, which is why *some* NPCs (the guard) already looked right.

**Palette readout** uses the TTF font, and the shift is per map:
Baron town (map 0) needs 13, Village of Mist (map 1) needs 5; everything else
defaults to 1. The table is `npc_pal_for_map`, so a newly checked map is one
line. `FF4_NPCPAL` or the `[` / `]` keys override the table for the session.

**Freeze NPCs** with `F` while comparing palettes (or start frozen with
`FF4_NPCFREEZE=1`). Movement and idle animation both stop, and the readout shows
`NPC PAL 5  FROZEN`. `[` / `]` still step the shift.

**Story sprites** that only appear under event switches are filtered out by
cell (`hidden_npc`), for now: Village of Mist's Kain at (8,16) and Rydia at
(14,9), from the positions reported in `ff4_cell.txt`. Delete an entry to bring
a sprite back.

**NPC palettes are per sprite, not per map.** `MapSpritePal` is a 36-entry table
of 8-colour palettes loaded into WRAM at `$0ddb`; each map names two NPC palettes
(`npcPalette1`/`npcPalette2`) and every NPC record picks one with its selector
bit. A single shift can therefore be right for most sprites and wrong for one
whose record carries the other palette; correct those by cell in `npc_pal_fix`:

```c
{ 2, 16, 10, 12, "Kaipo: dancer" },   /* map, x, y, palette */
```
### 32j. npcPalette2 is 1-BASED — the two palettes per map are now explained

The user measured the actual palettes in game:

| map | observed palettes | map data (npcPalette1, npcPalette2) | base |
| --- | --- | --- | --- |
| Village of Mist (1) | **5 and 13** | 0, 9 | 5 |
| Town of Baron (0) | **13** | 0, 1 | 13 |

`npcPalette2` counts **from 1** within the map's palette block, so:

```
pal = base + (selector & 1 ? npcPalette2 - 1 : npcPalette1)
```

Mist: 5+0 = **5** and 5+(9-1) = **13** ✓ (exactly the observed pair).
Baron: 13+0 = 13 and 13+(1-1) = 13 ✓ (all Baron NPCs share one palette).

Before the fix we produced 5 and **14** for Mist — off by one on the second
palette only, which is why tuning a single shift made one NPC right and left
the other wrong.  That asymmetry was the clue.

The exporter now emits the resolved per-NPC palette **offset**
(`npcPalette1` or `npcPalette2 - 1`) and the engine adds the per-map base
(`npc_pal_for_map()`), so a map's two palettes are `base+off` for its two
NPC classes.  `data/submaps/1.npc` now shows offsets 0 and 8.

### 32k. NPC palettes: NOT derivable from the exported data (measured table it is)

Five maps measured in game:

| map | gfx | BGpal | npcP1 | npcP2 | measured | sheets used |
| --- | --- | --- | --- | --- | --- | --- |
| Baron 0 | 4 | 4 | 0 | 1 | **13, 1** | 17-24, 101 |
| Mist 1 | 4 | 4 | 0 | 9 | **5, 13** | — |
| Kaipo 2 | 4 | 4 | 0 | 5 | **10, 13** | — |
| Silvera 4 | 4 | 25 | 0 | 6 | **13, 21** | — |
| Agart 6 | 4 | 24 | 0 | 1 | **14, 15** | 17,18,20,21,22,23,25,35 |

Ruled out as the source:
* **the exported map fields** — Baron and Agart have *identical*
  `gfx`/`npcP1`/`npcP2` yet need 13,1 vs 14,15;
* **sprite sheet** — the two maps share most sheets (17,18,20,21,22);
* **`MapProp` bytes** — the 13-byte record contains only the `npcPalette`
  byte already used (`0x90` = high 9/low 0 for Mist);
* **NPC record flag byte 3** — Baron's selector-0 set is
  `3A 70 82 A2 B0 B2 B3 C0`; no nibble equals 13 or 1.

So `data/npc_pal.json` stays as the source of truth and the measured values are
absolute palette indices written straight into `data/submaps/<id>.npc` by the
exporter; the engine adds **no** base (`npc_pal_for_map()` now returns 0).

**Making the survey cheap:** **S** records the current map and palette shift to
`npc_pal_measured.txt` (append), so no hand-editing JSON is needed.  H/J still
adjust, K toggles the readout, F freezes NPCs.

Also added by the user and kept: a per-cell `npc_pal_fix[]` table for individual
NPCs whose palette still looks wrong after the map value is right.  Its empty
size triggered an always-false comparison warning; fixed with an `int` cast
(`NPC_PAL_FIX_COUNT`).

### 32l. AUDIT: MapSpritePal found — palette = 13 + 2*nibble (no per-map tuning)

**Verified, not inferred.**  `field/npc.asm`:

```asm
        lda $0fe1 / and #$0f / jsr LoadNPCPal     ; low nibble  = npcPalette1
        lda $0fe1 / lsr4     / jsr LoadNPCPal     ; high nibble = npcPalette2
LoadNPCPal:
        sta $3e / stz $3d
        lsr $3e / ror $3d  (x3)                   ; 16-bit >>3  => nibble*32
        ldx $3d
        lda f:MapSpritePal+13*16,x
```

* `MapSpritePal` is at ROM **0x068000**, **36 entries x 16 bytes** (8 colours,
  3bpp sprite palettes).  `0x240 = 576 = 36*16`, so the stride is definitely 16.
* `13*16 = 208` = exactly palette 13; each nibble step is 32 bytes = **two**
  palettes.  Hence **palette = 13 + 2*nibble**.
* The rip's `npcPalettes` is the same table already decoded to 8x RGBA
  (32 bytes/entry).  Spot-checked: ROM `[15]` = `0x56B5` -> (168,168,168) and
  rip `[15][0]` = (173,173,173). Same colour, different 5->8 bit expansion, so
  **there is no stride bug** -- an earlier hypothesis of mine, now disproved.

Resulting palettes (byte6 nibbles in brackets):

| map | palettes |
| --- | --- |
| Baron 0 (0,1) | **13, 15** |
| Mist 1 (0,9) | **13, 31** |
| Kaipo 2 (0,5) | **13, 23** |
| Silvera 4 (0,6) | **13, 25** |
| Agart 6 (0,1) | **13, 15** |

Every map starts at 13, which is why "13" kept showing up in the manual
measurements -- that alone corroborates the formula.  Baron and Agart share a
MapProp byte and therefore *must* get identical palettes, which the manual
table could not express.

**Open:** the second palette (13+2*nibble) still differs from some hand
measurements.  If selector-0 NPCs now look right everywhere, the majority of
NPCs are fixed without any per-map work; the second class needs one more look.

### 32m. NPC palette formula CONFIRMED: `palette = 2 * nibble` (base 0)

The user read the indicator in game: **Baron's NPC palettes are 0 and 2.**

That fixes the last unknown.  The *step* was already right (asm `nibble*32`
bytes = two 16-byte palettes), but the **base is 0**, not the literal `13` in
`MapSpritePal+13*16`.  Our `npcPalettes[]` resource is stored 13 entries earlier
than the ROM table the code indexes, so the two differ by a constant 13.

```
npc_pal = 2 * nibble        (nibble = MapProp byte6 low/high)
```

| map | nibbles | palette(s) |
| --- | --- | --- |
| Baron 0 | 0, 1 | **0, 2**  <- confirmed in game |
| Agart 6 | 0, 1 | 0, 2 |
| Kaipo 2 | 0, 5 | 0, 10 |
| Silvera 4 | 0, 6 | 0, 12 |
| Mist 1 | 0, 9 | 0, 18 |

Every map's first palette is **0** and the second is `2*nibble`, so no per-map
table is needed.  `data/npc_pal.json` and the shift keys are now debug aids
only (`npc_pal_shift` defaults to 0).

**Auditing note:** the earlier "measured" pairs (Baron 13/1, Mist 5/13,
Kaipo 10/13, Silvera 13/21) were values read off the *shift* indicator, not
final palette indices -- which is why they appeared to contradict each other
and why the manual table could never generalise.  Read the resulting palette,
not the shift.

### 32n. FINAL: `palette = 13 + 2 * nibble` — confirmed in the field

The decisive test was in game: load Baron with **F1** and cycle with J/H.

* first palette correct at **NPC PAL 13**
* the stragglers correct at **NPC PAL 15**

Since the exporter was then emitting `2*nibble` (i.e. 0 and 2), a shift of 13
turned them into 13 and 15 -- so the pair Baron needs *is* **13 and 15**, and
the base is the literal `13` from `MapSpritePal+13*16`.

```
npc_pal = 13 + 2 * nibble
```

| map | nibbles | palettes |
| --- | --- | --- |
| Baron 0 | 0,1 | **13, 15** <- confirmed in game |
| Agart 6 | 0,1 | 13, 15 |
| Kaipo 2 | 0,5 | 13, 23 |

**Process note, worth remembering.** Two of my earlier conclusions were wrong
because I inferred the base from how the *shift* behaved rather than reading
the resulting palette:
* "base 0" came from the user reading the indicator when the exporter was
  emitting `13+2n` -- the number shown was the shift needed to cancel it;
* "they do not look correct" followed a build whose sprites, not palettes,
  were the visible problem.

The reliable procedure is: put the exporter at a known value, load a map, and
read the *resulting* palette once a correct one is found -- then set the
exporter to that value and confirm with the shift at 0.

Stragglers (a few NPCs that need the other palette of the pair) go in the
per-cell `npc_pal_fix[]` table in `world_view.c`.

### 32o. Palette selector: ANY non-zero value = second palette

Baron has 11 NPCs with the `palette` field distributed as
**0 x8, 1 x2, 2 x1**.  The palette selector only has two states (each map
supplies two palettes), so the field is not a 1-bit or 2-bit index:

```
sel = (n["palette"] != 0)          # non-zero -> second palette
npc_pal = 13 + 2 * nibble[sel]
```

Baron result: 8 NPCs at **13**, and the three non-zero ones -- (11,27), (8,22),
(27,7) -- at **15**.  That matches the in-game observation of "a couple were
off" until the second palette was applied, and matches the confirmed pair
**13 / 15**.

Tried and rejected first: `palette & 1` (left one of the three wrong) and
`palette >> 1` (changed only one of the three).
