# Message box — LOCKED (no regressions)

Status: **locked**. Verified against a real snes9x frame and the ASM, and
guarded by two automated checks.

## Spec (measured, do not change)

`include/ff4/core/ff4_dialog_layout.h` holds every number.

| item | value |
|---|---|
| box x / width | `8` / `240` (30 tiles) |
| box first tile row y | `12` |
| body rows (`$df`) | `8` (`OpenDlgWindow` increments 1..8, once per two IRQs) |
| box rows | `10` = 1 top border + 8 body + 1 bottom border |
| text x | `24` (box + 2 tiles) |
| text line tops | `27, 43, 59, 75` (pitch `16`) |
| text columns | `26` |
| lines per page | `4` |
| border white lines (rendered) | `y = 17` (top), `89..90` (bottom) |
| border white columns (rendered) | `x = 9`, `246` |

Tiles (BG3 page at VRAM `$2000`, `WindowGfx` @ ROM **0x57000**,
`WindowPal` @ ROM **0x687D0**):

```
top    16 17*28 18
body   19 FF*28 1A
bottom 1B 1C*28 1D
```

`DecodeDlgText` stores the decoded byte directly in the tilemap, so **glyph
tile == FF4 text code**: `'A'=0x42 … 'Z'=0x5B`, `'a'=0x5C … 'z'=0x75`,
`'0'=0x80`, space `0xFF` (`include/ff4/core/ff4_window_text.h`).

## Behaviour sequence

`window draws in (top→bottom, one row per frame) → the whole page of text
appears at once → closing hides the text → the box shrinks.`

No typewriter. Drawn last, above map/NPCs/actor/overhang. SNES 256×224 space
only (`sc = zoom`); expanded view never moves or stretches the box.

## Regression guards

```bash
cd build && make test_dialog && ./test_dialog ../data/gfx/window
#   46 checks: layout constants, glyph codes, tile bitmaps (incl. the
#   transparent top half of 0x16/17/18 that caused the black bar),
#   and the palette's dark-blue body colour.

python3 tools/verify_messagebox.py        # renders and measures the real frame
#   message box OK: borders y=17/90 x=9/246, text lines [27, 43, 59]
```

`ctest -R dialog` runs the C half at build time.

## The three bugs that produced a "black bar above the box"

1. `blit_win_tile()` created a texture per tile but never set its blend mode,
   so alpha was ignored and the transparent upper half of the top border tiles
   became opaque black. `SDL_SetTextureBlendMode(..., SDL_BLENDMODE_BLEND)`.
2. `FF4_SHOT` fired while the 32-frame warp curtain was still closing; it now
   waits until the frame just drawn had `warp_frames == 0`.
3. A 1 px black debug outline was drawn around the 256×224 viewport. Removed.

## How the ground truth was captured

```bash
gcc -O2 -o /tmp/retro_dump tools/retro_dump.c -lz -ldl
/tmp/retro_dump /usr/lib/x86_64-linux-gnu/libretro/snes9x_libretro.so \
    ff2.smc /tmp/f2 2400 20 400      # press Start at frame 400
# /tmp/f2/frame_2100.png  = intro dialogue, native 256x224
```

Measurements were taken on that frame (white border lines/columns, text line
tops) — not from the older `dialogue.png`, whose vertical scale is inconsistent
by ±7 px.
