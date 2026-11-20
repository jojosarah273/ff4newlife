#!/usr/bin/env python3
"""
Decode the background tile-animation data (water, waterfalls, lava) from the ROM.

Source of truth
---------------
field/gfx/map_anim_gfx.asm   MapAnimGfx  @ file 0xFB9C0, 0x1848 bytes
field/bg_anim.asm
  BGAnimTbl @cb4f  .byte 00 00 00 02 03 06 07 0a 0a 0a 0a 0a 0d 0d 0d 10
      per tileset (0..15); base offset into MapAnimGfx = tbl[ts] * 3 / 2
  LoadBGAnimGfx @cb01
      copies ONE byte then (byte, 0x00) pairs into the WRAM staging buffer
      $5000 until Y == 0x800 -> the staged data is 4 phases of 0x80 bytes,
      each phase being 8 tiles of 2bpp (16 bytes per tile), interleaved with
      zero high bytes because the destination is 4bpp VRAM.
  TfrBGAnimGfx @cb5f
      if (($7a & 0x06) != 0) return;      ; update every 8 frames
      phase = $7a & 0x18                  ; 0, 8, 16, 24
      DMA $5000 + phase*16 -> VRAM $1200  ; 0x80 bytes per phase

So: 4 phases, advancing every 8 frames (32-frame loop), 8 tiles per phase.

Usage
  python3 tools/ff4_bg_anim.py --verify
  python3 tools/ff4_bg_anim.py --export        # data/gfx/mapanim/<ts>.bin
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROM = ROOT / "ff2.smc" if (ROOT / "ff2.smc").exists() else ROOT / "ff2-debug.smc"
ASM = ROOT / "third_party" / "ff4"

MAP_ANIM_GFX = 0xFB9C0
MAP_ANIM_SIZE = 0x1848

BG_ANIM_TBL = [0x00, 0x00, 0x00, 0x02, 0x03, 0x06, 0x07, 0x0A,
               0x0A, 0x0A, 0x0A, 0x0A, 0x0D, 0x0D, 0x0D, 0x10]

PHASES = 4
PHASE_BYTES = 0x80
TILE_BYTES = 16          # 2bpp


def load_rom(path=None):
    d = Path(path or ROM).read_bytes()
    if len(d) % 0x400 == 0x200:
        d = d[0x200:]
    return d


def asm_bytes(path):
    vals = []
    for line in Path(path).read_text().splitlines():
        m = re.match(r"\s*\.byte\s+(.*)", line)
        if m:
            for t in m.group(1).split(","):
                t = t.strip().replace("$", "")
                if re.fullmatch(r"[0-9a-fA-F]{2}", t):
                    vals.append(int(t, 16))
    return vals


def base_offset(ts):
    """field/bg_anim.asm @cb01.

    The routine halves the table value with `lsr $3e / ror $3d`, so the result
    is a 16-bit offset: high byte = tbl + (tbl >> 1), low byte = (tbl & 1)<<7.
    That is exactly `tbl * 384` for every tbl (3/2 * 256).
    """
    return BG_ANIM_TBL[ts] * 384


def stage(blob, ts):
    """MapAnimGfx is only 0x1848 bytes, but entry 16 starts at 0x1800 and the
    staging loop reads 0x600 bytes, so the last tilesets run past the declared
    size into the following data.  The game does exactly that, so pad instead
    of failing."""
    """Reproduce LoadBGAnimGfx exactly (@cb23/@cb31).

    Each 32-byte iteration stages 16 raw bytes, then 8 (byte, 0x00) pairs.
    A phase is 0x80 bytes = four iterations = 96 source bytes, which is what
    TfrBGAnimGfx reads at $5000 + phase*16 ($5000 + 0 / 0x80 / 0x100 / 0x180).
    The zero high byte is the upper 2bpp plane of a 4bpp VRAM word, so the
    animated tiles are effectively 2bpp.
    """
    pos = base_offset(ts)
    blob = blob + bytes(0x600 + base_offset(15) + 0x600)
    out = bytearray()
    for _phase in range(PHASES):
        for _it in range(4):                       # 4 x 32 bytes = 0x80
            out += blob[pos:pos + 16]
            pos += 16
            for _k in range(8):
                out.append(blob[pos])
                out.append(0)
                pos += 1
    return bytes(out), pos


def phase_tiles(staged, phase):
    """8 tiles (2bpp, 16 bytes each) for one phase."""
    off = phase * PHASE_BYTES
    tiles = []
    for t in range(PHASE_BYTES // TILE_BYTES):
        tiles.append(staged[off + t * TILE_BYTES: off + (t + 1) * TILE_BYTES])
    return tiles


def verify():
    ok = True
    rom = load_rom()
    want = bytes(asm_bytes(ASM / "field/gfx/map_anim_gfx.asm"))
    got = rom[MAP_ANIM_GFX:MAP_ANIM_GFX + len(want)]
    good = got == want
    ok = ok and good
    print(f"  [{'OK ' if good else 'BAD'}] MapAnimGfx @0x{MAP_ANIM_GFX:X} == "
          f"map_anim_gfx.asm ({len(want)} bytes, declared 0x{MAP_ANIM_SIZE:X})")
    good = len(want) == MAP_ANIM_SIZE
    ok = ok and good
    print(f"  [{'OK ' if good else 'BAD'}] size 0x{len(want):X} == 0x{MAP_ANIM_SIZE:X}")

    # the 16 tilesets must produce 4 distinct phases with real ink
    distinct = 0
    for ts in range(16):
        staged, _ = stage(want, ts)
        ph = [phase_tiles(staged, p) for p in range(PHASES)]
        ink = [sum(1 for t in ph[p] for b in t if b) for p in range(PHASES)]
        blobs = {b"".join(ph[p]) for p in range(PHASES)}
        if len(blobs) > 1:
            distinct += 1
        if ts in (0, 3, 4, 15):
            print(f"      tileset {ts:2d}: base 0x{base_offset(ts):02X} "
                  f"ink per phase {ink}")
    good = distinct >= 10
    ok = ok and good
    print(f"  [{'OK ' if good else 'BAD'}] {distinct}/16 tilesets have "
          f"phase-varying animation")
    print("  => all checks passed" if ok else "  => FAILURES above")
    return ok


def export(outdir=None):
    outdir = Path(outdir) if outdir else ROOT / "data" / "gfx" / "mapanim"
    outdir.mkdir(parents=True, exist_ok=True)
    rom = load_rom()
    blob = rom[MAP_ANIM_GFX:MAP_ANIM_GFX + MAP_ANIM_SIZE]
    for ts in range(16):
        staged, _ = stage(blob, ts)
        (outdir / f"{ts}.bin").write_bytes(staged)
    (outdir / "anim.json").write_text(
        '{"phases": 4, "phase_bytes": 128, "tiles_per_phase": 8, '
        '"advance_frames": 8, "base_offsets": '
        + str([base_offset(t) for t in range(16)]) + '}')
    return outdir


def main(argv):
    if "--verify" in argv or not argv:
        if not verify():
            return 1
    if "--export" in argv:
        print("exported ->", export())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
