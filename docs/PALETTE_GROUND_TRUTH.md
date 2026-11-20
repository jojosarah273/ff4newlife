# FF4 sprite palettes — ground truth from the code

Everything in this document is traced to a specific routine in
`third_party/ff4/` and verified byte-for-byte against `ff2.smc`.

```
python3 tools/ff4_palette_truth.py --verify
```
must print `all checks passed` before anything else is trusted.

---

## 1. What was actually wrong (three separate bugs)

| # | Bug | Effect |
|---|-----|--------|
| 1 | `MapSpritePal` was read from ROM **0xA8700** | wrong table → every exported palette was garbage. The real table is at **0x68000** |
| 2 | `data/gfx/gfx/npcPalettes[n].bin` were written as **16-byte BGR555** | `world_view.c` reads them as **32-byte RGBA** (8 colours × 4 bytes), so 4 colours and the wrong byte order |
| 3 | NPCs were rendered with the **player** animation table | NPC OAM layout is different (`NPCSpriteTbl`), and NPCs whose sheet has only 4 tiles were marked "empty" and never drawn |

Bug 1 also explains why the palette *index* formula kept looking wrong: the
indices were fine, the colours behind them were noise.

## 2. The code (source of truth)

### 2.1 Table locations — found by byte-pattern search, not guesswork

| table | file offset | contents |
|---|---|---|
| `MapSpritePal` | **0x68000** | 36 entries × 16 B, 8 colours BGR555 each |
| `MapProp` | **0xA9C84** | 13 B/map; byte 6 = `npcPalette1 \| npcPalette2<<4` |
| `NPCPropPtrs` | **0x98000** | 384 × u16, offset from `NPCProp` |
| `NPCProp` | **0x98300** | 4 B/NPC; byte0 `gfx_id`, byte1 `x`, byte2 `y`, byte3 flags |
| `NPCGfxID` | **0x97000** | 512 B, gfx id → map sprite sheet |
| `PlayerPalTbl` | **0xAB2FA** | 18 B |
| `mapSpriteGfx` | **0xD8000** | bank $1B/$8000 |

`data/gfx/pal/map_sprite_pal.bin` (576 B, produced by the disassembly tool)
is **byte-identical** to ROM 0x68000 — that is the authoritative copy.

### 2.2 `LoadPlayerPal` (field/player.asm @c827)

Copies `MapSpritePal[0..]` into `$0DDB`, 16 bytes of colour then 16 bytes of
zero, until `Y == $0080`. So the first **4 CGRAM sprite slots** (32 B each)
receive entries **0,1,2,3**.

### 2.3 `LoadNPCPal` (field/npc.asm @c2bc)

```
sta $3e / stz $3d            ; 16-bit value 0x00:A
lsr $3e / ror $3d   (×3)     ; >>3  =>  X = (A & 7) * 32
lda f:MapSpritePal+13*16,x   ; base byte = 208 + (A&7)*32
copy 16 B, zero 16 B, repeat while Y & $3F != 0
```

→ **2 entries per call**, first entry = `13 + 2*(A & 7)`, second = `+1`.
Because of `(A & 7)`, nibbles 8..15 alias onto 0..7.

`InitNPCs` calls it twice:

| call | A | Y | destination |
|---|---|---|---|
| 1 | `$0fe1 & $0F` = `npcPalette1` | `$0000` | `$0E5B` |
| 2 | `$0fe1 >> 4` = `npcPalette2` | `$0040` | `$0E5B+$40` |

`$0E5B == $0DDB + $80`, i.e. the **next** 4 CGRAM sprite slots. So a map's
eight sprite slots are:

| CGRAM slot | MapSpritePal entry |
|---|---|
| 0,1,2,3 | 0,1,2,3 (characters) |
| 4 | `13 + 2*(npcPalette1 & 7)` |
| 5 | `14 + 2*(npcPalette1 & 7)` |
| 6 | `13 + 2*(npcPalette2 & 7)` |
| 7 | `14 + 2*(npcPalette2 & 7)` |

### 2.4 Which slot an entity uses (`InitNPCs` @c198)

* character (`sheet < 0x0E`): `PlayerPalTbl[sheet]` → slots 0..3
* NPC: `((NPCProp_byte3 & 0x0C) >> 2) + 4` → slots 4..7

`DrawNPCs` turns that into the OAM attribute with `(attr & $F1) + slot*2`.

### 2.5 NPCProp byte 3 layout

```
bit 7,6  speed (0..3)
bit 5    moving
bit 4    turn to face the player
bit 3,2  palette selector (0..3) -> CGRAM slot 4..7
bit 1,0  facing (0 up, 1 right, 2 down, 3 left)
```

### 2.6 Sprite sheets (`LoadSpriteGfx` @c38b)

address = `$8000 + X + A*96` in bank $1B, where

| sheet | X | A | bytes/tile count |
|---|---|---|---|
| `< 0x11` | 0x0000 | `sheet*8` | 32 tiles (768 B) |
| `0x11..0x2F` | 0x3300 | `(sheet-0x11)*4` | 16 tiles (384 B) |
| `0x30..0x45` | 0x6180 | `(sheet-0x30)*2` | 8 tiles (192 B) |
| `>= 0x46` | 0x7200 | `sheet-0x46` | 4 tiles (96 B) |

`data/gfx/gfx/mapSpriteGraphics[n].bin` is the rip's decoded 1-byte/pixel form
(32/16/8/4 tiles × 64 px), which matches these sizes.

A 4-tile sheet only carries the down-facing pose; such NPCs are static.

### 2.7 Sprite geometry

* `PlayerSpriteTiles` @c0c4 — 8 frames × 4 sprites `(tile, attr)`; `attr & 0x40` = h-flip
* `NPCSpriteTbl` @$15:F846 — 21 frames × 4 sprites `(x, y, tile, attr)`;
  order is TL, BL, TR, BR and the top row sits 1 px lower than the actor's

## 3. The tool

`tools/ff4_palette_truth.py` reimplements the routines above and is the single
place palettes are decided.

```
python3 tools/ff4_palette_truth.py --verify              # prove the addresses
python3 tools/ff4_palette_truth.py --maps 0,1,2          # dump slots + npc→entry
python3 tools/ff4_palette_truth.py --sheet 36            # /tmp/map36_npcs.png
python3 tools/ff4_palette_truth.py --export-palettes     # npcPalettes[n].bin (32B RGBA)
python3 tools/ff4_palette_truth.py --export-slots        # data/map_sprite_slots.bin
python3 tools/ff4_palette_truth.py --export-npc          # data/submaps/*.npc
```

`--export-slots` writes 384 × 8 bytes: the entry number in each CGRAM slot.
`world_view.c` loads it and picks the actor palette as
`slots[map][PlayerPalTbl[char_id]]`, and stores each NPC's absolute entry in
its `.npc` line.

`tools/ff4_export_submaps.py` no longer computes NPC palettes; it re-runs the
truth exporter at the end so the two can never disagree.

## 4. Verification

`tools/verify_engine_sprites.py <map>` renders the map in `ff4_world`
headlessly (`FF4_ZOOM=1`, NPCs frozen, noclip) and compares every
fully-visible NPC *and the actor* pixel-for-pixel against the ROM reference.

`tools/verify_all_maps.py 0 383` sweeps every map:

```
maps checked:                 361
NPC sprites verified:         462
  pixel-identical:            462  (100.00%)
  mismatched:                 0
  skipped (blank sheet):      70
  occluded (actor/overhang):  127
actor pixel-identical maps:   350  (+11 hidden by the map's own roof/arch rule, 0 mismatched)
```

Sprites are excluded from the count only when the game itself hides them:

* `sheet` is all-zero in the ROM (70 cases — event/switch-gated records)
* another NPC occupies the same cell (draw order hides it)
* the actor stands on the cell
* the overhang layer (roofs/tree canopy) covers it
* the cell's prop byte2 has `0x04` — the game's own "hide the player" rule

## 5. Demo

```
python3 tools/palette_demo.py 0 1 2 36 3 38
```
writes to `/tmp/ff4_palette_demo/`:

* `rom_<map>.png` — every NPC + the actor with the code's palettes
* `cmp_<map>.png` — ROM truth on the left, `ff4_world` on the right
* `sprites_<map>.png` — each NPC: truth above, engine below, labelled `OK`
* `eng_<map>.png` — raw engine frame

## 6. Engine controls

```
F1..F8   warp to Baron / Mist / Kaipo / Mysidia / Silvera / Agart / Astro / B.Magic
I        palette inspector (TAB select, H/J cycle, P record)
K        palette readout
F        freeze NPC animation
```

---

# Dialogue window + font (settled the same way)

| table | ROM | contents |
|---|---|---|
| `WindowGfx` | **0x57000** | 4096 B = 256 × 8×8 **2bpp** tiles: frame pieces **and** the ASCII font |
| `WindowPal` | **0x687D0** | 32 B = 16 colours BGR555 |
| `DlgTiles` strips | **0xA76D8** | top, +0x40 mid, +0x80 bottom (0x40 B each) |

`field/field.asm ReloadSubMap @8452` uploads `WindowGfx` to VRAM $2000, i.e.
tile numbers 0x200..0x2FF; the BG3 tilemap therefore stores plain small
indices.

`field/window.asm TfrDlgWindow @b75f` DMAs three 32-entry strips:

```
top    16  [17 x28]  18   00 00
mid    1B  [1C x28]  1D   00 00
btm    19  [FF x28]  1A
```
(30 visible tiles per row, then padding; every entry is `{tile, $20}`.)

`field/window.asm DecodeDlgText @b21f` stores the decoded byte straight into
the tilemap (`sta $0774,y`), so **the font tile number equals the text code**,
and the code→glyph mapping is the rip's `charTable`:

```
'A'=0x42 ... 'Z'=0x5B     'a'=0x5C ... 'z'=0x75
'0'=0x80 '1'=0x81 ...     space=0xFF
DTE 0x8A.. expands to two letters (DecodeDTE + DTETbl)
```

Verified: `python3 tools/ff4_window_font.py --verify` (4/4 checks) and
`--demo` renders `CECIL of the Red Wing!` in the real box
(`/tmp/window_reference.png`). Exported to `data/gfx/window/`.
