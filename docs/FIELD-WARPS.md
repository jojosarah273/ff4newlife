# Field warps — stitching the map graph

## Where the edges live

| table | source | size | meaning |
|---|---|---|---|
| `MapTriggers1` | `field/trigger.asm` (ptr table `MapTriggersPtrs`) | 256 maps | per-map cell triggers |
| `MapTriggers2` | same, second bank | 128 maps | maps 256..383 |
| `worldTriggers1/2/3` | `field/vehicle.asm` loaders | 58 / 17 / 7 | 2x2 cell blocks on each world map |

Both are already decoded in `data/ff4-en-data.json`. The exporter writes
`data/submaps/<id>.trig`, one line per trigger:

```
x y map xDest yDest dir event
```

## What the runtime does (`world_view.c`)

* `sm_trig_load()` reads `<map>.trig` on every sub-map load (max 32).
* When the party **lands on a cell** the cell is looked up:
  * `props2 & 0x10` → **exit tile**: pop the previous map stack and return
  * otherwise a trigger → push the current map and start a 32-frame curtain
* The warp is applied at the curtain midpoint: the destination is loaded and
  the party placed at `xDest,yDest` facing `dir`; the arrival `event` id is
  logged (wiring it to the event VM is the next step).
* Targets `>= 0xFB` are world maps: `251 → overworld`, `252 → underground`,
  `253 → moon`. `254/255` are **spare world slots** and are ignored — see below.

## The 254/255 puzzle (important)

`MapProp[251..255]` all have `graphics = 0` and no field layout; they are the
five world slots. Of the 979 trigger records, **527 point at 254/255**. Those
are the *event/overlay* records that share the same table (the trigger record's
"map" field is not always a warp destination). `tools/ff4_warp_graph.py`
classifies them and the runtime skips them, so they can neither warp the party
into nothing nor break a traversal.

## The graph, verified

```bash
python3 tools/ff4_warp_graph.py          # summary + checks
python3 tools/ff4_warp_graph.py 0        # details for one map
python3 tools/ff4_warp_graph.py --dot /tmp/warps.dot
```

Current numbers:

```
sub-map cell triggers : 979
world-map warps       :  82
warps to a rendered map / world : 450
spare-world slots (254/255)     : 527   (event/overlay, not followed)
targets with no map at all      :   2   (map 272/275 -> map 27, unrendered)
field maps reachable from map 0 :  33 / 361
```

The "no trigger back" check found 40 world→map warps whose map has no trigger
near its entry cell. Inspecting them shows why: e.g. **Damcyan (37)** has only
two triggers, both to sub-maps — its way out is an *exit tile* (`props2 & 0x10`)
or an event script, not a trigger. So the graph is consistent; the remaining
links are a different mechanism.

## Verified traversal

```bash
FF4_TRACE=1 FF4_WARPTO=0 FF4_MAPX=16 FF4_MAPY=29 FF4_AUTOWALK=down \
  FF4_SHOT_DELAY=120 FF4_SHOT=/tmp/rt.png ./build/ff4_world
```

```
[trig] 20 triggers on map 0
[trig] event 102 -> map 251 at (102,158) dir 1
```

which is exactly the ROM record `(16,31) → map 251 (102,158) event 102`, and the
screenshot shows the overworld at Baron with the party standing on it.

## Next

1. Fire the arrival `event` id through the event VM (that is how FF4 gates
   first-visit scenes and map changes).
2. Exit tiles: only a few maps use `props2 & 0x10`; verify each by walking.
3. Maps 27/272/275: the exporter skipped this map (layout blob empty); decode
   before trusting those two edges.

---

# Map transition — the SNES mosaic

## What the game does

`field/event.asm  EventCmd_d2` (event command `$d2`, "mosaic screen"):

```asm
@ee5c:  stz $79
@ee5e:  jsr WaitVblankShort
        lda $79
        lsr                 ; frame / 2  -> one step every 2 frames
        tax
        lda f:EventMosaicTbl,x
        sta $2106           ; MOSAIC: block size per BG layer
        inc $79
        lda $79
        cmp #$40            ; 64 frames total
        bne @ee5e
```

`EventMosaicTbl` (`field/field_data.asm @fa66`):

```
0F 1F 2F 3F 4F 5F 6F 7F 8F 9F AF BF CF DF EF FF   ; ramp up   (16 steps)
EF DF CF BF AF 9F 8F 7F 6F 5F 4F 3F 2F 1F 0F 0F   ; ramp down (16 steps)
```

So: **64 frames, block size 0 → 15 → 0, two frames per step**, applied to the
BG layers via `$2106`. The map is swapped while the screen is at maximum
block size, so the change is hidden.

## What the engine does

* `warp_frames` now runs **1..64**; the map is swapped at **32** (the peak).
* Each frame derives the block size from the same 1..16..1 ramp
  (`mosaic_blk`), and a post pass on the SNES viewport replaces every N×N block
  with its top-left pixel — exactly what `$2106` does to the PPU output.
* `FF4_CURTAIN=1` restores the old black wipe for comparison.
* `FF4_SHOT_IN_TRANSITION=1` lets a screenshot fire mid-transition (otherwise
  captures wait for the swap to finish).

Verified by capturing frames 1/12/22/31/33/44/54/63: the world map pixelates to
16×16 blocks, the town appears at the peak still pixelated, then de-pixelates.
