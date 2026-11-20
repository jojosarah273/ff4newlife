# Next steps: z-layers, tile animation, NPC movement

Researched from `third_party/ff4` and checked against the ROM tables. Written
so each item has a code address, a data source and an acceptance test.

---

## 1. Z-layers: water, bridges, walking under/over  — **DONE (see docs/FIELD-COLLISION.md)**

### What the code does

Tile properties are two bytes per cell (`MapTileProp`, file
`0xA1000-0x200 + (tileset-1)*0x100`).

**byte 1 — the z/floor byte**

| bit | meaning | header |
|---|---|---|
| 0x01 | passable on the **upper** z | `FF4_TP1_UPPER_Z` |
| 0x02 | passable on the **lower** z | `FF4_TP1_LOWER_Z` |
| 0x03 | z mask (the tile's lane) | `FF4_TP1_Z_MASK` |
| 0x04 | **bridge** tile | `FF4_TP1_BRIDGE` |
| 0x08 | save point | `FF4_TP1_SAVE` |
| 0x10 | door | `FF4_TP1_DOOR` |

**byte 2 — effects**

| bit | meaning |
|---|---|
| 0x01 | damage |
| 0x04 | **hide the sprite** (under a roof / lower z) |
| 0x08 | **hide the bottom half** of the sprite (behind a low wall) |
| 0x10 | exit (pop the map stack) |
| 0x20 | through-tile |
| 0x40 | random battles |
| 0x80 | trigger |

`CheckTilePass` (`field/move.asm @aa58`) is the rule, and it is *not* the
`0x03` rule the engine uses today:

```
if ($b1) return BLOCKED                       ; event running
if (CheckNPCBlock) return BLOCKED             ; an NPC is in the way

if (current tile byte1 & 0x04)                ; standing on a BRIDGE
        passable = ((next byte1 & 0x03) & $d2) != 0
                || (next byte1 & 0x04) != 0   ; or another bridge
else
        passable = (next byte1 & 0x0A) != 0   ; LowerZ | SavePoint
```

`$d2` is the z the party is standing on (`current byte1 & 0x03`).
`CheckNPCBlock` (`@aa94`) additionally refuses to step onto a bridge tile
unless `$d2 == 1` (lower lane), and refuses any cell present in the NPC
occupancy map at `$7F4C00`.

**So**: water/void = byte1 with neither `0x02` nor `0x08`; a bridge is walked
*on* (upper lane) and drawn above the actor because its tilemap entry has
priority bit `0x2000`.

**Drawing order** comes from the tilemap entry's priority bit (`0x2000`), not
from a pre-baked overlay: a priority cell is drawn after the actor, which is why
you can walk under bridges, archways and tree canopy. Partial occlusion is the
two byte-2 bits above (`hide sprite`, `hide bottom half`).

### Engine status

| item | state |
|---|---|
| byte1/byte2 exported per cell (`<id>.props`) | done (2 bytes/cell) |
| per-cell `pri` bit per layer | **not exported** |
| overhang drawn from a pre-baked PNG | works, but not per-cell and not z-aware |
| collision rule | uses `0x03`, **documented as wrong**; `0x0A` makes most town floors solid in our maps |
| actor/NPC z | NPC z comes from a cell lookup; actor z is a constant |
| `hide sprite` (byte2 0x04) | only used for the actor |
| `hide bottom half` (byte2 0x08) | **not implemented** |

### Plan

1. **Z1 — export the real flags.** Add `pri1/pri2` (bit 0x2000 of each layer's
   tilemap entry) to `<id>.props`, or better a new `<id>.z` with
   `{byte1, byte2, z, bridge, pri1, pri2}` per cell. Keep `.props` for compat.
2. **Z2 — port `CheckTilePass` verbatim** into `ff4_field_tile_pass_bytes`
   (it already has the shape) and make the viewer call the same function so the
   two cannot drift. Resolve the `0x0A` vs `0x03` question by dumping
   `$06a1`/`$d2` from the emulator with `FF4_RAM` for a known walk (Baron
   plaza + a bridge map) instead of guessing.
3. **Z3 — z-aware sprite drawing.** Per actor/NPC: `z = props1 & 0x03` of its
   cell; skip drawing if `props2 & 0x04`; clip the lower 8 px if `props2 & 0x08`;
   draw priority cells after sprites instead of the pre-baked overhang.
4. **Z4 — bridges.** Standing z becomes state (`$d2`), updated only when the new
   tile is not a bridge; drawing uses `pri` so the bridge covers the sprite.

**Acceptance** (harness tapes, see `docs/PLAN-field-usable.md` phase 0):
* walk off a bridge tile onto water → blocked;
* walk across a bridge over water → allowed, and the sprite is drawn *under*
  the bridge rails;
* walk into an arch (`props2 & 0x04`) → sprite hidden while inside, visible
  again on the far side;
* walk behind a low wall (`0x08`) → only the top half of the sprite is drawn.

---

## 2. Tile animation: water, waterfalls, lava

### What the code does

`field/bg_anim.asm`

* `BGAnimTbl` (`@cb4f`) maps **tileset → animated-graphic block**:

  ```
  00 00 00 02 03 06 07 0a 0a 0a 0a 0a 0d 0d 0d 10
  ```
  base offset into `MapAnimGfx` = `tbl[ts] * 3 / 2` (`@cb01`).

* `LoadBGAnimGfx` (`@cb01`) expands that into a WRAM staging buffer at `$5000`:
  it copies 16 bytes, then interleaves a zero byte after each following byte,
  until `Y == 0x800`. That is 4 animation phases of `0x80` bytes.

* `TfrBGAnimGfx` (`@cb5f`) uploads one phase to VRAM `$1200`:

  ```
  if (($7a & 0x06) != 0) return;        ; only every 8 frames
  phase = $7a & 0x18;                   ; 0, 8, 16, 24  -> 4 phases
  source = $5000 + phase * 16;          ; 0x00, 0x80, 0x100, 0x180
  ```

  → **4 phases, advancing every 8 frames (32-frame loop, ~0.53 s)**.

* Which *cells* are animated: a tileset sub-sprite whose graphic index is
  `0x120..0x12F` **and** whose low byte has bit `0x20` set. The frame's graphic
  comes from the per-sheet `animated_offset` block at
  `major*16 + minor + frame*4`, `major = (lo/4)%4`, `minor = lo%4`.
  `tools/ff4_export_submaps.py` already detects these tiles and has
  `SHEETS_ANIM`, but renders **frame 0 only**.

* Separate palette animations exist for special maps:
  `TowerAnimBluePal` (`$15:F826`) and `LavaAnimPal` (`$15:FA16`).

### Plan

1. **A1** decode `MapAnimGfx` per tileset into `data/gfx/mapanim/<ts>/<0..3>.bin`,
   verifying the base offsets against `BGAnimTbl` and the emulator.
2. **A2** in the exporter, emit per-tileset `animated[]` = tile → 4 frame
   graphics (instead of baking frame 0 into the map PNG).
3. **A3** runtime: animate on the global frame counter `$7a` with the ASM
   cadence (change every 8 frames, 4 phases), drawing the animated variant for
   those cells.
4. **A4** palette-cycling animations for the Tower/Lava tables.

**Acceptance**: capture 4 snes9x frames of a waterfall/lava map 8 frames apart
(`tools/retro_dump`, `FF4_RAM` for `$7a`) and diff our frames at the same
phases; the animated cells must match, static cells must not change.

---

## 3. NPC movement speed and collision

### What the code does

`field/npc.asm`

* Speed comes from **NPCProp byte 3 bits 6-7** (`InitNPCs`, `and #$c0 / lsr6`)
  → index `0..3`, stored in `$0901`.
* `MoveNPCs` (`@c00c`) moves the NPC's **sub-tile offset** (0..15) by
  `_00c117[speed]`:

  ```
  _00c117:  1, 1, 1, 2        ; pixels per frame within a 16-unit cell
  _00c11b:  1, 2, 4, 8        ; added to the wait/repath accumulator $08ff
  ```

  So speed 0-2 all move 1 unit/frame (16 frames per tile) and speed 3 moves
  2 units/frame (8 frames per tile); the `$08ff` accumulator decides how often
  the NPC re-randomises its direction.
* Direction choice: `Rand()` (the ROM `rngTable`, `$78` index) split at `0x80`,
  then candidates are validated with `CheckNPCMap1` and `_00bfe3`
  (tile props `& 0x83`), falling back to the two perpendicular directions.
* Blocking: `CheckNPCBlock` consults the occupancy map at `$7F4C00`; when an
  NPC is in the player's way, `$0900 != 0` and `MoveNPCs` forces speed index 2
  so it steps aside at 1 unit/frame.
* An NPC's z for drawing comes from the per-cell table `$0edc` (`DrawNPCs` uses
  `$090a`).

### Engine status

| item | state |
|---|---|
| speed read from the record | stored (`.npc` field) but ignored |
| sub-tile movement | implemented at a fixed rate |
| direction re-randomisation | timer-based, **not** the RNG table / 0x80 split |
| NPC blocks the player | **not implemented** (walk through NPCs) |
| step aside when blocking | **not implemented** |
| face player on talk | not implemented |
| visibility switches (`CheckNPCSwitch`) | not implemented (map-1 special case only) |

### Plan

1. **N1** use `_00c117`/`_00c11b` exactly: speed index from the record,
   1/1/1/2 px per frame, wait accumulator 1/2/4/8.
2. **N2** port `CheckNPCBlock` + the `$7F4C00` occupancy map; make the player
   collide with NPCs and make the blocking NPC step aside.
3. **N3** direction choice from `rngTable` with the `0x80` split and the same
   validation order (forward, then the two perpendiculars, then blocked).
4. **N4** `CheckNPCSwitch` visibility (`switch0`/`switch1`), face-player on
   talk, and `npcScript` instead of static text.

**Acceptance**: harness run on Kaipo (12 NPCs) with a fixed RNG seed for 600
frames — NPC cell positions must equal the golden list; a tape that walks into
an NPC must be blocked, and the NPC must move away within N frames.

---

## Order

| # | item | state |
|---|---|---|
| 1 | Z2 collision (`CheckTilePass` + the `AND $0A` mask) | ✅ done, tested |
| 2 | Z3 z-aware drawing (`byte2 & 0x04` hide, `0x08` clip) for actor + NPCs | ✅ done |
| 3 | N1 NPC speed tables `_00c117`/`_00c11b` + step-aside | ✅ done |
| 4 | Z1 export per-cell `pri` bits (still uses the pre-baked overhang PNG) | ☐ next |
| 5 | A1 `MapAnimGfx` decode + `--verify` (`tools/ff4_bg_anim.py`) | ✅ data done |
| 6 | A2/A3 animate the 8 tiles per phase at runtime (4 phases, 8-frame step) | ☐ next |
| 7 | N2/N3/N4 occupancy map, RNG wander, switches, `npcScript` | ☐ next |
| 8 | A4 palette animation (LavaAnimPal / TowerAnimBluePal) | ☐ pending |
| 9 | Warps: `.trig` export + cell triggers + map stack (`docs/FIELD-WARPS.md`) | ✅ done |
| 10 | Arrival `event` id → event VM; exit-tile verification | ☐ next |

Everything above is testable with the phase-0 harness: scripted tape, fixed
seed, state hash + screenshots, compared to snes9x frames captured with
`tools/retro_dump`.
