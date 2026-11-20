# Warps and map transitions (decoded)

## 1. The map of maps

The world→sub-map links are a flat table in the rip, per world:

| key | world | entries |
| --- | --- | --- |
| `worldTriggers1` | overworld | 58 |
| `worldTriggers2` | underground | 17 |
| `worldTriggers3` | moon | 7 |

**82 world warps total**, and **117 sub-maps carry a return trigger** back to a
world map. Each entry is already parsed as:

```json
{ "x": 101, "y": 156, "map": 36, "xDest": 16, "yDest": 30,
  "direction": 0, "event": 16 }
```

* `x,y` — the world cell(s) that trigger the warp
* `map` — the destination map id (`>= 0xFB` means a world map: 251/252/253/255)
* `xDest,yDest` — entry position in the destination map
* `direction` — facing on arrival
* `event` — the event script id that runs on arrival (this is how the game
  gates "first visit" scenes)

### Shape of a warp zone

Warps are **2×2 cell blocks** (four consecutive entries), e.g. Baron:

```
(101,156) (102,156)
(101,157) (102,157)   -> map 36 "Baron", enter (16,30), facing up, event 16
```

So a town/castle entrance is a 4-cell rectangle on the world map, not a point.

### Notable specials

| trigger | meaning |
| --- | --- |
| `map == 255` | **WORLD4** pseudo-map — used by the airship/underworld transitions and the "exit to world" cases (entered with an `xDest` of 0/24/43/54 and a specific event) |
| `map == 252/253` | the other world maps (underground / moon) |
| repeated rows | a zone, not a single cell |

Complete extraction:

```python
o = json.load(open('data/ff4-en-data.json'))['obj']
for key in ('worldTriggers1', 'worldTriggers2', 'worldTriggers3'):
    for t in o[key]:
        print(t['x'], t['y'], '->', t['map'], t['xDest'], t['yDest'], t['event'])
```

The reverse edges (sub-map → world) live in `mapTriggers1[map]` as
`kind: "teleport"` entries whose `map >= 0xFB`.

## 2. The transition animation

Two routines, `field.asm @923f` (out) and `@9269` (in), plus two IRQ handlers
(`UpdateWipeIRQ @91ca`, `UpdateWipeNMI @9204`).

```
WipeOut:  $d9 = 1 ; TM = $07 (BG1|BG2|BG3) ; TS = $01 (BG1 on subscreen)
          $79 = 0 ; repeat 32x { wait 2nd IRQ ; inc $79 }

WipeIn:   $d9 = 1 ; $79 = $1f ; INIDISP = $80 (force blank)
          repeat 32x { wait 2nd IRQ ; dec $79 }
          $d9 = 0 ; INIDISP = $0f (or $80 during an event)
```

* **32 steps, one per frame** → ~0.53 s at 60 Hz.
* `$79` is the step counter (0…31 out, 31…0 in).
* The visual is **not a fade** — it is per-scanline blanking plus a colour
  window:

```
UpdateWipeNMI:  VTIMEL = $6f - WipeScanlineTbl[$79*2+1]
                WH0    = $80 - WipeScanlineTbl[$79*2]
                WH1    = $7f + WipeScanlineTbl[$79*2]
                INIDISP = $80

UpdateWipeIRQ:  1st IRQ (at VTIMEL): INIDISP = $0f      (bright)
                2nd IRQ            : INIDISP = $80      (blank)
```

`WipeScanlineTbl` (`field_data.asm @fb1e`, 32 pairs) runs `$7f,$6e → … → $06,$05`,
so:

* the **bright band grows from 1 scanline to ~106** (a vertical curtain),
* while the **colour-match window narrows** (`WH0`/`WH1` close from
  `$01..$FE` to `$7A..$85`) — a rectangular iris closing toward the centre,
* `TS = $01` puts BG1 on the **subscreen**, which is how the game swaps
  between the old and new map graphics mid-wipe.

### Engine equivalent

We cannot (and do not need to) reproduce the HDMA/IRQ trick. The observable
behaviour is:

```c
/* 32 frames */
t = step / 31.0;                       /* 0..1 */
bright = (t < 0.5) ? 15 : 15 * (1 - t);
draw(map_new);                          /* the incoming map */
clip_to_iris(centre_x, centre_y,       /* the closing band */
             radius = lerp(full, 0, t));
blank_outside(bright);
```

i.e. a **0.5 s iris/curtain reveal centred on the screen**, with the new map
already drawn underneath, which is what the player actually sees.

## 3. Implementation notes

* The warp must run **before** the map reload (position is set by the
  trigger), and the reload must happen during the wipe's blanked phase.
* `event` on the trigger is the arrival script; the game runs it after the
  wipe completes (`ExecEvent` once `$d9` clears).
* The prev-map stack (`$172C` ring, already modelled as
  `FF4_WorldState.prev_map`) is pushed on every warp into a sub-map, and
  popped by exit tiles (`byte2 & $10`).
* A warp to `map >= 0xFB` switches the *world* renderer rather than loading a
  sub-map.
