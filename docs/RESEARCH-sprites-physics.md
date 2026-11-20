# Research: sprites & physics (references + validation)

Goal: pin down **which sprite belongs where** (actor, NPCs, events) and the
exact **movement/collision rules**, with every claim tied to a code address,
and validated against the ROM data *before* any engine code changes.

Sources consulted
-----------------
| Source | What it gives |
| --- | --- |
| `third_party/ff4` (everything8215 disassembly) | **authoritative code** — the readable logic and tables |
| `third_party/ff4-documentation` | data-structure notes |
| `DEV/FF4KSTER_LINUX` | a *working* FreeBASIC editor: its table addresses + decode are a cross-check |
| `DEV/FE_GOLBEZ` (F4C) | authoritative typed decode of ROM tables (`ff4struct`) |
| `DEV/FF4_FROM_SOURCE/ff4recomp` | full **SNES→C recompilation**, but it is an *LLE interpreter* (dispatches into ROM bytes), so it is an **oracle**, not readable logic. 1623 functions are address-mapped in `config/bank*.cfg` — a complete symbol map we can use for navigation |
| `DEV/FREEENTERPRISE_APP` | Kotlin app; FF4 window/font palette work |
| snesmaps.com `FinalFantasyIIMaps.html` | full map images + per-character sprites (`sprites/CecilR.png`, `sprites/KainL.png`) for visual verification |
| this project | world map render (scope+palette confirmed correct by the user) |

Everything below is read out of the disassembly; the validation test checks
the derived constants against the ROM.

---

## 1. Direction encoding

```
XMoveTbl  (move.asm @ab09):  00 00 01 00 FF     ; dx for dir 0..4
YMoveTbl  (move.asm @ab0e):  00 FF 00 01 00     ; dy for dir 0..4
```

| index | meaning | dx,dy |
| --- | --- | --- |
| 0 | none | 0,0 |
| 1 | up | 0,-1 |
| 2 | right | +1,0 |
| 3 | down | 0,+1 |
| 4 | left | -1,0 |

Two encodings exist and both are used:

* `$1705` **facing** = 0 up, 1 right, 2 down, 3 left (0-based)
* `$0709` **move dir** = facing + 1 (1-based; 0 = not moving), i.e. the
  `XMoveTbl` index.

---

## 2. Physics

### 2.1 Speeds (move.asm @ad12)

```
$00:AAE6  VehiclePassBit:     01 02 04 10 20 20 80
$00:AAED  MapSizeMask:        FF 7F 3F     ; world wrap: 256 / 128 / 64 cells
$00:AAF0  TriggerRateTbl:     0F 07 03 01  ; check triggers every N frames
$00:AAF4  PlayerMoveRateTbl:  01 02 04 08  ; pixels per frame
```

`$ac` = movement speed index (0..3) → 1, 2, 4 or 8 px per frame.

### 2.2 Movement is scroll-driven (`MovePlayer`, move.asm @a871)

The **player sprite does not move on screen**. `$5a/$5b` (BG1 h-scroll) and
`$5c/$5d` (BG1 v-scroll) move by `PlayerMoveRateTbl[$ac]` each frame; the
tile coordinate `$1706/$1707` only advances when the scroll crosses a 16-px
boundary, wrapping with `MapSizeMask[$06fa]`:

```asm
; up:      $5c -= rate ; if (old & 0x0f) - rate borrows -> $1707 = ($1707-1) & mask
; right:   $5a += rate ; if (new & 0x0f) == 0        -> $1706 = ($1706+1) & mask
; down:    $5c += rate ; if (new & 0x0f) == 0        -> $1707 = ($1707+1) & mask
; left:    $5a -= rate ; if (old & 0x0f) - rate borrows -> $1706 = ($1706-1) & mask
```

`$7b` = party frame counter, incremented every frame while moving (drives the
walk animation). `$c1` = floor-damage flag (inverts colours every 8 frames).

`$06fa` = world-size id: 0 = overworld, 0 = underground (both 256), 2 = moon
(64).  (`LoadOverworld`/`LoadUnderground` `stz $06fa`; `LoadMoon` sets 2.)

### 2.3 Collision

**World map** (`CheckPlayerMoveWorld`, move.asm @ab98/@ac56):

```asm
$d2 = $a1 & $41                 ; current tile's terrain bits
; no vehicle:
passable = ((target & $41) & $d2) != 0
; vehicle v (1..6):
passable = (target & VehiclePassBit[v]) != 0
```

`$a1` = current tile prop byte 1; `$a3` = up, `$a5` = right, `$a7` = down,
`$a9` = left (filled by `UpdateLocalTiles`, vehicle.asm @9f6c).

Validated against the data: **bit 0 of byte1 is "walkable land"**, bit 6 is
unused in practice.

| tile | byte1 | bit0 | meaning |
| --- | --- | --- | --- |
| 25 (ocean, 22654 cells) | `$A4` | 0 | blocked |
| 19 | `$A0` | 0 | blocked |
| 22, 67, 42 (land) | `$B7` | 1 | walkable |
| 70 | `$AF` | 1 | walkable |

→ **15.8 % of the overworld is walkable**, which matches a mostly-ocean world.

The airship's mask is `$20`, and *every* tile's byte1 has bit 5 set
(`$A0`–`$B7`), so the airship passes over everything — correct for FF4.

**Sub-map** (`CheckTilePass`, move.asm @aa58) uses different bits:

```asm
if (cur & 4)              ; on a bridge tile
    passable = (next & 3) & $d2 != 0     ; z-level must match
else
    passable = (next & $0A) != 0
else-if (next & 4)        ; stepping onto a bridge
    passable = (next & 4) != 0
```

i.e. **sub-map floor = `prop1 & $0A`**, z-level = `prop1 & $03`,
bridge = `prop1 & $04` — which matches the rule already used by
`tools/ff4_export_world.py` and the C field engine.

---

## 3. Sprites

### 3.1 Which sheet? (`LoadPlayerGfx`, player.asm @c88b)

```asm
jsl GetTopCharPtr            ; X = party leader char record ($1000 + idx*$40)
lda $1003,x : and #$20 → sheet $0F     ; special states override the sheet
              and #$10 → sheet $0E
              and #$08 → sheet $10
lda $1001,x : and #$1F       ; character id
$06 = char_id
offset = char_id * 3 + $80   ; 24-bit; × $100 later => char_id*$300 + $8000
Tfr3bppGfx  from MapSpriteGfx ($1B:8000), $200 bytes -> VRAM $4000
```

So:

* **sheet = character id**, from `MapSpriteGfx + char_id*$300` (0x300 bytes,
  3 bpp = 32 8×8 tiles).
* `$1003` flags can force sheet `$0F`/`$0E`/`$10` (special poses).

### 3.2 Which palette? (`PlayerPalTbl`, sprite.asm @b2fa)

```
PlayerPalTbl: 00 00 01 02 02 02 00 01 01 03 00 01 00 00 00 00 00 00
              ^char id 0..17
```

Indexed by the character id; `$0710 = palette << 1` (the OAM attribute
palette field is bits 1–3).

### 3.3 Actor animation (`DrawPlayerWorld`, sprite.asm @b30c)

```asm
frame = $1705 << 4                       ; facing * 16 bytes
if ($ab != 0) {                          ; moving
    frame += ($7b & 8)                   ; walk phase: +8 or +0
    if (!($ab & 1))                      ; horizontal?
        $06 = ($7b & 8) >> 3             ; 0 or 1  -> the Y bob
}
for i in 0..3:
    x     = PlayerSpritePosTbl[i*2]
    y     = PlayerSpritePosTbl[i*2+1] - $06
    tile  = PlayerSpriteTiles[frame + i*2]
    attr  = (PlayerSpriteTiles[frame + i*2 + 1] & $F1) + $0710
```

`PlayerSpritePosTbl` ($15:C0B4) — the 4 OAM entries of the 16×16
actor, at screen position **(112,109)**:

```
$15:C0B4:  70 6D 00 00   78 6D 00 00   70 75 00 00   78 75 00 00
           (112,109)     (120,109)     (112,117)     (120,117)
```
(each entry is a full 4-byte OAM record: x, y, tile=0, attr=0)

`PlayerSpriteTiles` ($15:C0C4) — 8 used frames of 4 sprites
(`tile, attr`; attr `$20` = priority, `$60` = priority+hflip):

| frame | facing | step | tiles |
| --- | --- | --- | --- |
| 0 | up | 0 | 04 05 06 07 |
| 1 | up | 1 | 04 05 07↔ 06↔ |
| 2 | right | 0 | 09↔ 08↔ 0B↔ 0A↔ |
| 3 | right | 1 | 0D↔ 0C↔ 0F↔ 0E↔ |
| 4 | down | 0 | 00 01 02 03 |
| 5 | down | 1 | 00 01 03↔ 02↔ |
| 6 | left | 0 | 08 09 0A 0B |
| 7 | left | 1 | 0C 0D 0E 0F |

(↔ = drawn horizontally flipped, i.e. `attr & $40`.)

So the 16 tiles 0–15 form the 4-direction sheet:
**0–3 = down, 4–7 = up, 8–11 = left, 12–15 = right**; "right" is the "left"
art mirrored. Frames beyond 7 in the table are extra poses.

OAM attribute bits: `$01` tile bit 8, `$01`–`$0F` palette in bits 1–3,
`$20` priority, `$40` h-flip, `$80` v-flip.

### 3.4 NPCs (`DrawNPCs`, npc.asm @bb68)

NPC records live at `$0900` (0x20 bytes each, 16 slots; `$af` = slot*$20).
Slot validity = `$090b` non-zero. The sprite is built like the player's, but
the frame comes from the NPC's own animation state:

```asm
if ($0901 & $03) {              ; animated / moving?
    if ($0901 & $20) { ... }    ; special (moving NPC) → uses $7a timing
    else {                      ; direction from the record
        tile = NPCGfxID[...]    ; sheet (npc_gfx_id_en.asm, 512 entries)
        frame from $7a & $0c    ; 4-frame walk cycle
    }
} else {
    if ($090c & $02) { frame from _00bdfe / _00be06 tables (idle variants) }
    else {
        if ($0902 & $7f) { ... $08ff bit6/bit0 → frame }
        else { frame = ($7a & $10) >> 4 }    ; 2-frame idle blink
    }
}
```

NPC properties (`$0901`, `$0902`, `$08ff`, `$0909`, `$090c`) come from
`npcProperties` (the rip exposes these as named dicts) and the sheet id from
`npcGraphicsProperties` (512 entries, the rip has it).

**Key difference from the actor**: NPCs animate off the *global* frame
counter `$7a` (so they all animate in lockstep), while the actor animates off
`$7b`, the party counter (only advancing while moving).

### 3.5 Events

Event-driven sprite changes go through the event VM (`event_engine`):
`$DD` = change character sprite, `$F4/$F5` = set/clear NPC switches (which
gate whether an NPC is drawn at all — `CheckNPCSwitch`, npc.asm @c137).
Event scripts also move NPCs (`MoveNPCs`, npc.asm @bd54 /
`CheckMoveNPCs` @bb56), which sets `$0901`/direction and therefore picks the
walk frames above.

---

## 4. What this means for the engine

1. **Physics is cheap to replicate exactly** — it is: accumulate scroll by
   `{1,2,4,8}`, update tile coords on 16-px boundaries, wrap with
   `{256,256,64}`, and test `(cur & $41) & (next & $41)` for the world
   (`(next & $0A)` for sub-maps). No fixed-point, no physics engine.
2. **Sprites are fully specified** — sheet = char id from
   `MapSpriteGraphics`, palette = `PlayerPalTbl[char]`, 16 tiles arranged
   down/up/left/right, 2 walk steps per direction, plus the horizontal bob.
3. **The actor's screen position is constant** (112,109); only the scroll
   moves. In our engine that is: keep the actor centred in the viewport and
   move the camera, which is equivalent and already what `ff4_world` does.

---

## 5. Validation

`tests/test_world_rules.c` (new, standalone) checks the constants above
against the ROM/rip so a future edit cannot silently break them:

* `PlayerMoveRateTbl` = {1,2,4,8}; `TriggerRateTbl` = {15,7,3,1}
* `MapSizeMask` = {0xFF,0x7F,0x3F}; `VehiclePassBit` = {1,2,4,0x10,0x20,0x20,0x80}
* `XMoveTbl`/`YMoveTbl` direction vectors
* `PlayerPalTbl` = {0,0,1,2,2,2,0,1,1,3,0,1,0,0,0,0,0,0}
* `PlayerSpritePosTbl` = (112,109),(120,109),(112,117),(120,117)
* the 8 animation frames' tile lists
* the world passability rule: ocean tile `$A4` blocked, land `$B7` walkable
  (58 of 128 world tiles are walkable by bit 0)
* the airship mask `$20` passes every tile
