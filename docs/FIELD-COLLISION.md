# Field collision — the real rule (and why every earlier doc was wrong)

## The bug in the documentation

Every note in this repo said the sub-map floor test was:

```
passable = (next_byte1 & 0x0A) != 0          # "LowerZ | SavePoint"
```

That reading comes from the disassembly line

```asm
@aa7d:  lda $06a1,y
        and $0a
```

which *looks* like `and #$0a`. It is not. The assembled listing
(`third_party/ff4/field/obj/field_en.lst`) shows

```
00285Fr 2  B9 A1 06   @aa7d: lda $06a1,y
002862r 2  25 0A             and $0A        <-- opcode $25 = AND direct page
```

`$25` is **AND dp**, not `AND #`. So the mask is a *variable* in direct page.

## What builds that variable

`field/move.asm`, in the walk update:

```asm
@a959:  lda $a1
        and #$04
        bne @a965            ; standing on a bridge -> keep the standing lane
        lda $a1
        and #$03
        sta $d2              ; $d2 = current tile's lane bits
@a965:  ...
        lda $a1
        and #$03
        clc
        adc #$04
        sta $0a              ; $0A = (current lane) + 4
```

So the mask is `lane + 4`, i.e. `4..7` — **not** `0x0A`. Byte 1 bits 0/1 are
lane-passable flags and bit 2 is "bridge".

## The real `CheckTilePass` (@aa58)

```c
if (event_running)                       return BLOCKED;   // $b1
if (CheckNPCBlock())                     return BLOCKED;

if (current_byte1 & 0x04)                                  // on a bridge
        return ((next_byte1 & 0x03) & $d2) != 0            // same lane
            || (next_byte1 & 0x04) != 0;                   // or another bridge

return (next_byte1 & ((current_lane & 3) + 4)) != 0;        // lane match
```

`CheckNPCBlock` (@aa94) additionally refuses to *step onto* a bridge tile unless
`$d2 == 1`, and refuses any cell present in the NPC occupancy map `$7F4C00`.

## Why the wrong rule looked plausible

`next_byte1 & 0x0A` selects bit1 (lane 2) or bit3 (unused) — on real maps that
matched only 141/1024 cells of Baron town, which is why it "made most floors
solid". The correct mask gives 561 lane cells + 3 bridges there, and the town
behaves.

## Implemented

* `ff4_field_tile_pass_bytes()` (`src/engine/field_engine.c`) — the rule above.
* `tile_passable()` (`src/platform/sdl2/world_view.c`) — same rule, plus the
  per-tile direction masks and NPC occupancy; it also records that an NPC is
  blocking so the NPC steps aside.
* `data/submaps/<id>.walk` is now produced as *"cell has a lane or is a bridge"*;
  the runtime uses the z-aware rule, `.walk` is only used for spawning and the
  collision overlay.
* Tests: `tests/test_field.c::test_tile_pass_rule` pins the mask maths
  (`(lane & 3) + 4`), and `tests/test_world_rules.c` documents the opcode.

## Z drawing

`byte2` bits, now implemented for actor **and** NPCs:

| bit | effect | evidence |
|---|---|---|
| `0x04` | the **actor** sprite is not drawn on that cell (under a roof / lower z) | `DrawPlayerSub @b2d6` tests `$a2 & 0x04` and moves the OAM entries to `$f8` |
| `0x08` | **draw-order/priority**, not a height clip | `DrawPlayerSub` ORs `$20` into the top two OAM attributes and ANDs `$CF` on the bottom two |

`0x08` was implemented as "clip the lower half" for a while; the sprite
verifier flagged it on 9 maps, and re-reading the routine shows it only changes
OAM priority bits.  NPCs do **not** use `0x04` either: hiding them on that bit
made the verifier fail (map 2/6/16), so an NPC's visibility must come from
`$090a` / the per-cell table `$0edc`, which is still to be decoded.

`$d2` (standing lane) is initialised from the spawn cell and updated on every
step, and **not** on bridge tiles — matching `@a959`.

## Verify by hand

```bash
FF4_TRACE=1 FF4_WARPTO=0 FF4_MAPX=18 FF4_MAPY=14 FF4_AUTOWALK=up \
  FF4_SHOT_DELAY=90 FF4_SHOT=/tmp/bridge.png ./build/ff4_world
# [actor] cell=(18,14) z=2 ... (18,13) z=2 ... (18,9) z=2
# crosses the bridge at (18,13) and keeps the standing lane
```

Baron town bridges are at `(18,13) (25,14) (27,20)` (map 0) and
`(25,14) (26,14)` (map 36).
