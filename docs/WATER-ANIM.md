# FF4 field water/liquid animation — the three systems

Status: **field tile animation done and guarded; cave water modeled and
byte-proven against an in-game capture; world ocean/lava ASM-decoded,
pending the coastal emulator capture.** All modeling lives in
`tools/ff4_water_models.py`; the cave-water proof vector is pinned at
`data/field/cavewater_capture/` (dense WRAM snapshots, snes9x).

## How each system works (from the disassembly)

### 1. Field tile animation (4-phase) — DONE, guarded

`TfrBGAnimGfx @cb5f`: 16 animated slots per tileset, 4 phases, one phase
every 8 frames.  The engine renders per-phase composites from the packs;
`tools/verify_animation.py` pins it (0 px vs the composites, cadence,
phase pinning, bit-order guard).

### 2. Cave water (UpdateCaveWaterGfx @8ac4) — BYTE-PROVEN

Runs from the sub-map loop (called after `DrawNPCs`), on even `$7a`:

```
k = ($7a & 0x1e) >> 1            ; cycles 0..15, 32-frame period
x = CaveWaterTbl[k] * 2          ; @fc66: 00 21 02 23 04 25 06 27
                                 ;         20 01 22 03 24 05 26 07
three RotateWord chains, carry threading through all three:
    (x, x+0x20), (x+1, x+0x21), (x+0x10, x+0x30)
RotateWord @8b1d (65816): with A = buf[x+0x20], C = carry
    $09 = $07 = A
    ror $09 ; ror $08 ; ror $07
    out: buf[x] = $08, buf[x+0x20] = $07, C = $07 bit0
```

Empirically (`--verify`): **99/99 frames reproduced byte-exactly** from a
dense WRAM capture of `$7f5800` (frames 15462–15660 step 2, phase 1).

The buffer is the 256-byte window cached from VRAM word `$2000`
(`LoadWaterGfx @8f69`) and re-uploaded by `TfrWaterTiles @8e0e` slivers.
The capture was taken during the opening's sub-map (event mode) — the
cave-water shimmer was running there.

**Remaining for the renderer wiring:** which displayed tiles the four
buffer tiles correspond to on cave-water maps (the buffer art was not
found verbatim in the ripped sheets/anim blocks — the VRAM staging
differs; needs one targeted VRAM↔tilemap capture).

### 3. World ocean/lava (UpdateWaterAnim @8fa1) — ASM-DECODED, pending proof

- **X phase** on even `$7c`, `k = ($7c>>1)&0xf`, run base =
  `WaterShiftX[k]` (`@90aa`, all 8-aligned).  The run pair
  (A = buf[base..base+8], B = buf[base+0x40..+0x48]) becomes a
  cross-conveyor: `A' = [B[7]]+A[0:7]`, `B' = [A[7]]+B[0:7]` — each run
  shifts down one byte, the heads swap tails.
- **Y phase** every frame, four stride-8 column rotations at
  `WaterShiftY[k]+0x38` and `+0xb8` (twice each).
- The X window is the **ocean tile art**: it matched `worldGraphics`
  tiles `0x80..0x83` (+assign 32) byte-exactly (flyover VRAM `$4000`,
  odd stream — the mode-7 staging interleaves a dither stream on the
  even bytes).  The Y window matched tiles `0x7A..0x7D`.
- Re-upload per frame: two 8-byte slivers to VRAM word
  `$20{WaterShiftX[k]}` and `+0x40` (`TfrWaterGfx @8e67`), and the Y
  window DMA'd to word `$1e80`.

**Pending:** a standing-in-game coastal capture (free overworld control)
to byte-prove the X/Y model the way the cave model is proven.  The
harness can now drive the game there (`FF4_PRESSBTN=0` B-spams the
opening dialogs; `FF4_PRESS_TO` stops; hold-direction args walk; the
game's own start is world cell (101,158), `field.asm` init `$1706/$1707`)
— what remains is navigating out of the opening's scripted scenes to the
shore without wandering into Baron.

## Harness notes (tools/retro_dump.c, new)

- `FF4_PRESSBTN=<id>` — extra button pressed together with START/A
  (0 = SNES B, the dialog skip).
- `FF4_PRESS_TO=<frame>` — stop the periodic presses.
- WRAM sampling: `FF4_RAM_EVERY/FROM/TO/ADDR(lo:len)/OUT` —
  `ADDR=15800:200` captures the X window (`$7f5800`) and Y window
  (`$7f5900`) in one file.
- Gotchas learned the hard way: lexicographic frame sort (`frame_9990`
  > `frame_15400`), the intro's flyover does NOT run the field anim
  loop (windows load but stay static), and the periodic presses open
  the menu on the field (pausing the anim) — stop them with
  `FF4_PRESS_TO` before sampling.


## Update (this session): the REAL world shimmer, byte-proven

A battery save (FF4_SRM) + precise menu taps ("300:8:5,500:5:5,700:8:5")
gets the emulator into a saved game standing before a big underground
lake.  Dense VRAM capture during the visible shimmer shows:

- **The visible world water animation is `TfrBGAnimGfx`** (the standard
  4-phase system), writing the 8 mode-7 tile slots at VRAM word $1200
  (byte $2400-$25ff) every 8 frames — NOT the `UpdateWaterAnim` WRAM
  windows (those load at field init and stay static in-game).
- `WaitFrame @914b` calls `TfrBGAnimGfx` in EVERY world mode; on world
  maps it runs unconditionally (sub-maps gate on `$7a & $06`).
- **Phase mapping proven byte-exact** against
  `data/field/worldwater_capture/vram_{4002,4010,4018,4026}.bin`:
  phase p's 512 bytes = staged blocks (p-1)%4, +4, +8, +12 (0x80 each)
  of the `LoadBGAnimGfx` staging of `MapAnimGfx` block 0 (ROM $fb9c0).
  Guarded by `tools/ff4_water_models.py --verify` (4/4 phases exact).
- **Data gap found**: the underground's blue water renders with colors
  OUTSIDE the rip's 64-colour `worldPalettes` — the mode-7 world uses a
  256-colour palette, and the water blues (e.g. (8,89,198), (0,0,99))
  live in the extended range.  The static `.idx` export renders
  underground 0x19 as lava-orange; the emulator shows animated blue.
  No guard ever compared the world view to the emulator before, so this
  was invisible.

### Remaining for the engine wiring (next session, clearly scoped)

1. Capture/derive the world's full 256-colour mode-7 palette (snes9x
   CGRAM is not in the libretro memory map — derive from the emulator
   screen + the anim art bytes, or find the ROM source the palette
   uploader uses).
2. The $2400 art -> screen pixel pipeline (the staged (byte,$00)
   interleave reads as alternating art/colour-0 in mode 7 — verify
   against the captured shots; the water flecks match the diagonal
   one-pixel-per-phase drift visible in the staged phases).
3. Then: which composed cells' base tiles map to the 8 animated slots
   (mode-7 tile numbers 0x90-0x97), and render them from the phase art
   in `wp_pass` — the guard compares the engine's per-phase view
   against the captured shots.
