# Collision reference — what is passable and what isn't

Derived from the disassembly and **verified against the ROM** by
`tests/test_world_rules.c`.  Bit names come from the rip definition
(`third_party/ff4/vanilla/ff4-en-rip.json`, which carries the
`stringTable` the disassembly used).

---

## 1. World maps (overworld / underground / moon)

Tile property `byte1` = **Passability** (one flag byte per tile, 128 tiles
per world, at `$14:8A80`; world *w* at `+w*$100`; tile *t* at `+t*2`).

| bit | value | meaning |
| --- | --- | --- |
| 0 | `0x01` | Passable on **Lower Z-Level** |
| 1 | `0x02` | **Chocobo** can move / lava |
| 2 | `0x04` | **Black Chocobo** can fly |
| 3 | `0x08` | Black Chocobo can **land** |
| 4 | `0x10` | **Hovercraft** can move |
| 5 | `0x20` | **Airship** can fly (no lava) |
| 6 | `0x40` | Passable on **Upper Z-Level** |
| 7 | `0x80` | **Lunar Whale** can fly |

`byte2` = **Flags** + battle background:

| bits | meaning |
| --- | --- |
| `0x07` | battle background: 0 Grass, 1 Beach, 2 Forest, 3 Desert, 4 Underground, 5 Moon |
| `0x08` | Hide Bottom of Sprite |
| `0x10` | Airship Can Land |
| `0x40` | Enable Random Battles |
| `0x80` | Trigger Tile |

### The rule (`CheckPlayerMoveWorld`, move.asm)

```c
/* on foot / no vehicle  ($1704 == 0) */
passable = ((cur_prop1 & 0x41) & (next_prop1 & 0x41)) != 0;
/*  0x41 = LowerZ | UpperZ  →  you may walk between tiles that share a
    z-level walkability bit; ocean (no bit0/bit6) is never walkable. */

/* with a vehicle v (1..6) */
passable = (next_prop1 & VehiclePassBit[v]) != 0;
/*  VehiclePassBit = {0x01, 0x02, 0x04, 0x10, 0x20, 0x20, 0x80}  ($00:AAE6)
      1 chocobo      0x02
      2 black choc.  0x04  (fly)
      3 hovercraft   0x10
      4 airship      0x20  (fly)
      5 airship      0x20
      6 lunar whale  0x80  */
```

### Measured result (overworld, 128 tiles)

* **58 tiles are walkable** (`0x41` set) — 15.8 % of the map by cell count.
* **127 of 128 tiles** are airship-flyable (`0x20`); only tile `104` is not.
* Ocean (`0xA4` = bits 2,5,7) is fly-only, never walkable.
* Land `0xB7` (bits 0,1,2,4,5,7) and `0xAF` (bits 0,1,2,3,5,7) are walkable;
  `0xAF` also lets the chocobo *land*, i.e. it is proper land.

Flag groups actually used on the overworld:

| flags | tiles |
| --- | --- |
| Airship, LunarWhale | 43 |
| LowerZ, Chocobo, BlackChocoFly, Hovercraft, Airship, LunarWhale | 34 |
| LowerZ, BlackChocoFly, Airship, LunarWhale | 11 |
| Chocobo, BlackChocoFly, Hovercraft, Airship, LunarWhale | 11 |
| BlackChocoFly, Airship, LunarWhale | 10 |
| (5 more combinations) | … |
| (none) — tile `104` | 1 |

World tilemap cells are **16×16 composed tiles** (see ENGINE.md); the player
occupies one cell and moves cell-to-cell, so the unit of collision is the
cell.

---

## 2. Field / sub-maps

Tile property `byte1` (per tileset, 128 tiles, 2 bytes each, at
`$14:8E00 + tileset*$100`):

| bit | value | meaning |
| --- | --- | --- |
| 0 | `0x01` | Passable on **Upper Z-Level** |
| 1 | `0x02` | Passable on **Lower Z-Level** |
| 2 | `0x04` | **Bridge Tile** |
| 3 | `0x08` | **Save Point** |
| 4 | `0x10` | **Door Tile** |

`byte2` = **Flags**:

| bit | value | meaning |
| --- | --- | --- |
| 0 | `0x01` | Damage Tile |
| 1 | `0x02` | Unknown 1.1 |
| 2 | `0x04` | Hide Sprite on Lower Z-Level |
| 3 | `0x08` | Hide Bottom of Sprite |
| 4 | `0x10` | **Exit to Previous Map** |
| 5 | `0x20` | Through-Tile |
| 6 | `0x40` | **Enable Random Battles** |
| 7 | `0x80` | **Trigger Tile** |

### The rule (`CheckTilePass`, move.asm)

```c
if (cur_prop1 & 0x04) {                    /* standing on a bridge */
    passable = ((next_prop1 & 0x03) & (cur_prop1 & 0x03)) != 0;
} else if (next_prop1 & 0x04) {            /* stepping onto a bridge */
    passable = 1;
} else {
    passable = (next_prop1 & 0x0A) != 0;   /* LowerZ | SavePoint */
}
```

### Verification on real maps

* **Baron (map 36, tileset 3)** — 20 triggers; every trigger tile has
  `byte2 = 0x80` (**Trigger**).  The map has exactly one **Exit** tile
  (`0x10`) and no battle tiles (towns have no random battles).
* **Map 43 (tileset 6)** uses `0x20` (Through), `0x40` (Random Battles) —
  confirming battle = `0x40`.

---

## 3. ⚠️ Bug found in our existing code

`include/ff4/engine/field_engine.h` defines the field `byte2` flags **shifted
down by one bit**:

| name | ours | should be | authoritative meaning |
| --- | --- | --- | --- |
| `FF4_TP2_DAMAGE` | 0x01 | 0x01 | Damage Tile ✓ |
| `FF4_TP2_HIDE_Z` | 0x02 | **0x04** | Hide Sprite on Lower Z-Level |
| `FF4_TP2_GRASS` | 0x04 | **0x08** | Hide Bottom of Sprite |
| `FF4_TP2_EXIT` | 0x08 | **0x10** | Exit to Previous Map |
| `FF4_TP2_WATER` | 0x10 | **0x20** | Through-Tile |
| `FF4_TP2_BATTLE` | 0x20 | **0x40** | Enable Random Battles |
| `FF4_TP2_TRIGGER` | 0x40 | **0x80** | Trigger Tile |

and `byte1`:

| name | ours | should be |
| --- | --- | --- |
| `FF4_TP1_Z_MASK` | 0x03 | 0x03 (upper\|lower z) ✓ |
| `FF4_TP1_BRIDGE` | 0x04 | 0x04 ✓ |
| `FF4_TP1_LAYER(n)` | `0x04 << n` | `0x01 << n` (0 = upper, 1 = lower) |

So trigger tiles, battle tiles and exit tiles are currently being detected on
the wrong bits.  **Not changed yet** — flagged here so it is fixed
deliberately, with the test in place to catch regressions.
