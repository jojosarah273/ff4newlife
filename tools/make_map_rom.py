#!/usr/bin/env python3
"""make_map_rom.py - patch RETAIL ff2.smc to boot directly onto any map.

Why: we cannot *play* FF4 to reach Kaipo/Kokkol/etc. (ruin story gates), and
rebuilding from the disassembly is unnecessary.  Instead we patch five bytes of
the retail ROM so the "start a new game" path skips the intro event ($10) and
jumps to a stub that names the map to load.  Retail stays retail.

Layout (LoROM: file offset O maps to bank O//0x8000 : 0x8000 + O%0x8000):

  hook     bank $00:$8081  (file 0x0081)
           was:  A9 10 20 63 DF      lda #$10 / jsr ExecEvent
           now:  5C 55 DE 01 EA      jml $01DE55 / nop

  stub     bank $01:$DE55  (file 0xDE55) - 1195 bytes of 0xFF padding
           lda #3  : sta.l $001700   map type 3 = sub-map
           lda #M  : sta.l $001702   map id
           lda #MSB: sta.l $001703   map id high bit (>255)
           lda #X  : sta.l $001706   spawn x (cells)
           lda #Y  : sta.l $001707   spawn y (cells)
           lda #0  : sta.l $0000B1   no event
           jsl $008313               LoadMap (called directly: MainLoop
                                     branches on $1700 before checking $CD)
           jml $0080AF               MainLoop

Verified against retail: MainLoop does `lda $cd / beq +3 / jmp $8313`.

usage:
    python3 tools/make_map_rom.py <map> [x] [y] [out.smc]     # single
    python3 tools/make_map_rom.py --dump-all <outdir>          # every map
"""
import os
import subprocess
import sys

SRC = "ff2.smc"
HOOK = 0x0081
STUB = 0xDE55
STUB_BANK = 0x01
CORE = "/usr/lib/x86_64-linux-gnu/libretro/snes9x_libretro.so"
RETRO_DUMP = os.environ.get("RETRO_DUMP", "/tmp/rd2")


def build_stub(mid, x, y):
    """Return the 8-bit machine code for the loader stub."""
    msb = 1 if mid > 255 else 0
    code = bytearray()

    def lda(v):                       # lda #imm
        code.extend((0xA9, v & 0xFF))

    def sta_l(addr):                  # sta.l $00:addr
        code.extend((0x8F, addr & 0xFF, (addr >> 8) & 0xFF, 0x00))

    lda(3)
    sta_l(0x1700)                     # sub-map
    lda(mid)
    sta_l(0x1702)                     # map id
    lda(msb)
    sta_l(0x1703)                     # map id MSB
    lda(x)
    sta_l(0x1706)                     # spawn x
    lda(y)
    sta_l(0x1707)                     # spawn y
    lda(0)
    sta_l(0x00B1)                     # no event running
    # Call LoadMap ($00:8313) directly.  Do NOT rely on $CD: MainLoop tests
    # $1700 FIRST and jumps to $8138 for $1700==3, so it never reaches the
    # `lda $cd / jmp $8313` check at $00DA.
    code.extend((0x22, 0x13, 0x83, 0x00))   # jsl $008313 (LoadMap)
    code.extend((0x5C, 0xAF, 0x80, 0x00))   # jml $0080AF (MainLoop)
    return code


def make_rom(mid, x=16, y=16, out=None):
    rom = bytearray(open(SRC, "rb").read())
    if len(rom) == 0x100200:          # strip a copier header if present
        rom = rom[0x200:]
    # hook: jml $01DE55 ; nop
    rom[HOOK:HOOK + 5] = bytes((0x5C, STUB & 0xFF, (STUB >> 8) & 0xFF,
                                STUB_BANK, 0xEA))
    stub = build_stub(mid, x, y)
    if STUB + len(stub) > 0xE300:
        raise SystemExit("stub does not fit in the free region")
    rom[STUB:STUB + len(stub)] = stub
    if out is None:
        out = "/tmp/maprom_%d.smc" % mid
    with open(out, "wb") as f:
        f.write(rom)
    return out


def dump(mid, x, y, outdir):
    rom = make_rom(mid, x, y)
    os.makedirs(outdir, exist_ok=True)
    tmp = "/tmp/_mapdump"
    os.makedirs(tmp, exist_ok=True)
    for f in os.listdir(tmp):
        os.remove(os.path.join(tmp, f))
    # 1500 frames: title -> Start at 400 -> field/map
    subprocess.run([RETRO_DUMP, CORE, rom, tmp, "1501", "1500", "400", "100"],
                   check=True, capture_output=True)
    final = os.path.join(tmp, "frame_1500.png")
    dst = os.path.join(outdir, "%d.png" % mid)
    if os.path.exists(final):
        os.replace(final, dst)
        return dst
    return None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    if sys.argv[1] == "--dump-all":
        outdir = sys.argv[2] if len(sys.argv) > 2 else "refs/maps_real"
        lo = int(sys.argv[3]) if len(sys.argv) > 3 else 0
        hi = int(sys.argv[4]) if len(sys.argv) > 4 else 360
        ok = bad = 0
        for mid in range(lo, hi + 1):
            try:
                p = dump(mid, 16, 16, outdir)
            except Exception as e:                    # noqa: BLE001
                print("map %3d FAILED %s" % (mid, e))
                bad += 1
                continue
            if p:
                print("map %3d -> %s" % (mid, p))
                ok += 1
            else:
                bad += 1
        print("done: %d ok, %d failed" % (ok, bad))
        return 0
    mid = int(sys.argv[1], 0)
    x = int(sys.argv[2], 0) if len(sys.argv) > 2 else 16
    y = int(sys.argv[3], 0) if len(sys.argv) > 3 else 16
    out = sys.argv[4] if len(sys.argv) > 4 else "/tmp/maprom_%d.smc" % mid
    print(make_rom(mid, x, y, out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
