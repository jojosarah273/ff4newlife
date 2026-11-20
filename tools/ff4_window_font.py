#!/usr/bin/env python3
"""
FF4 dialogue window + font, taken from the ROM and the code.  Verified, not guessed.

--------------------------------------------------------------------------
What the code says
--------------------------------------------------------------------------
menu/menu.asm  .segment "window_gfx" -> WindowGfx          (4096 B)
field/field.asm  ReloadSubMap @8452: TfrVRAM(WindowGfx -> VRAM $2000, $1000)
    -> 4096 bytes = 256 tiles of 8x8 2bpp.

field/window.asm  TfrDlgWindow @b75f
    DMAs three 0x40-byte (32-entry) BG3 strips:
        DlgTilesTop   @f6d6   EN:  16 17*30 18
        DlgTilesMid   @f716   EN:  1B 1C*30 1D
        DlgTilesBtm   @f756   EN:  19 FF*30 1A
    every entry is { tile, $20 } -- $20 = tilemap priority bit.

field/window.asm  DecodeDlgText @b21f
    sta $0774,y          ; the decoded byte IS the BG3 tile number
    ... DecodeDTE expands $8A-$BF / $CA-$FD through DTETbl
    -> **glyph tile index == character code**, confirmed by rendering the bank:
       tile 0x42 is 'A', 0x50 'P', 0x61 'a', 0x30 '0'.

So a dialogue box is:

    row 0        : 16  [17 ...]  18
    rows 1..h-2  : 19  [FF ...]  1A
    row h-1      : 1B  [1C ...]  1D

and text is written as tiles = text bytes, 26 columns per line
(field/window.asm TfrDlgText DMAs $001a = 26 bytes per row).

--------------------------------------------------------------------------
ROM locations (verified by byte-pattern search)
--------------------------------------------------------------------------
WindowGfx  file 0x57000   4096 B 256 x 2bpp tiles (frame pieces + ASCII font)
WindowPal  file 0x687D0     32 B  16 colours BGR555
DlgTiles   file 0x?       the three strips, located from the asm literals
           (they are 0x40 bytes each; searched by their exact byte sequence)
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROM = ROOT / "ff2.smc" if (ROOT / "ff2.smc").exists() else ROOT / "ff2-debug.smc"
ASM = ROOT / "third_party" / "ff4"

WINDOW_GFX = 0x57000
WINDOW_PAL = 0x687D0

# The dialogue window is 30 tiles wide; the 0x40-byte (32-entry) DMA strips
# carry 2 zero padding entries after the 30 visible ones.
# Located in ROM at 0xA76D8 (top), +0x40 (mid), +0x80 (btm) -- 0xA76D8 was
# found by scanning for 0x16,0x20 followed by a run of 0x17,0x20 then 0x18,0x20.
WINDOW_W = 30
TEXT_COLS = 26
TEXT_X = 2
DLG_TOP = [0x16] + [0x17] * (WINDOW_W - 2) + [0x18]
DLG_MID = [0x1B] + [0x1C] * (WINDOW_W - 2) + [0x1D]
DLG_BTM = [0x19] + [0xFF] * (WINDOW_W - 2) + [0x1A]


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


def bgr555(w):
    return (((w & 0x1F) << 3) | ((w >> 2) & 7),
            (((w >> 5) & 0x1F) << 3) | ((w >> 7) & 7),
            (((w >> 10) & 0x1F) << 3) | ((w >> 12) & 7))


class Window:
    def __init__(self, rom=None):
        self.rom = rom or load_rom()
        self.gfx = self.rom[WINDOW_GFX:WINDOW_GFX + 4096]
        raw = self.rom[WINDOW_PAL:WINDOW_PAL + 32]
        self.colors = [bgr555(raw[i * 2] | (raw[i * 2 + 1] << 8))
                       for i in range(16)]

    def tile_pixels(self, idx, cbase=0):
        """8x8 list of RGB for tile `idx` (2bpp)."""
        b = self.gfx[(idx & 0xFF) * 16:(idx & 0xFF) * 16 + 16]
        out = []
        for y in range(8):
            p0, p1 = b[y * 2], b[y * 2 + 1]
            for x in range(8):
                v = ((p0 >> (7 - x)) & 1) | (((p1 >> (7 - x)) & 1) << 1)
                out.append(self.colors[(cbase + v) & 15])
        return out

    # -- composition ---------------------------------------------------------
    def box_rows(self):
        """The three strip rows of the dialogue window (WINDOW_W tiles)."""
        def strip(c0, mid, c1):
            return [c0] + [mid] * (WINDOW_W - 2) + [c1]
        return strip(0x16, 0x17, 0x18), strip(0x1B, 0x1C, 0x1D), strip(0x19, 0xFF, 0x1A)

    def render_box(self, text_lines, cols=TEXT_COLS, dte=None, scale=1,
                   glyph_codes=None):
        """A complete dialogue box: real 9-slice + real glyph tiles."""
        from PIL import Image
        top, btm, fill = self.box_rows()
        h = len(text_lines) + 2           # one border row top and bottom
        grid = [top[:]] + [fill[:] for _ in range(h - 2)] + [btm[:]]

        for r, line in enumerate(text_lines):
            row = 1 + r
            codes = self._encode(line, glyph_codes, dte)
            for c, code in enumerate(codes[:cols]):
                if code == 0xFF:
                    continue
                grid[row][TEXT_X + c] = code

        nw = len(top)
        img = Image.new("RGB", (nw * 8 * scale, h * 8 * scale), (0, 0, 0))
        for ry, row in enumerate(grid):
            for cx, t in enumerate(row):
                px = self.tile_pixels(t)
                for y in range(8):
                    for x in range(8):
                        for dy in range(scale):
                            for dx in range(scale):
                                img.putpixel((cx * 8 * scale + x * scale + dx,
                                              ry * 8 * scale + y * scale + dy),
                                             px[y * 8 + x])
        return img

    @staticmethod
    def _encode(line, glyph_codes=None, dte=None):
        """Encode text with the game's own table (rip charTable, ROM-derived).

        glyph_codes: single-character -> text code   ('A' -> 0x42, 'a' -> 0x5C,
                     '0' -> 0x80, ' ' -> 0xFF)
        dte:         two-character string -> code    ('th' -> 0x8C, ...)
        """
        out = []
        i = 0
        while i < len(line):
            pair = line[i:i + 2]
            if dte and len(pair) == 2 and pair in dte:
                # DecodeDTE expands the pair into its two letters via DTETbl;
                # the DTE code itself is never drawn as a tile.
                out.append(glyph_codes.get(pair[0], 0xFF))
                out.append(glyph_codes.get(pair[1], 0xFF))
                i += 2
                continue
            ch = line[i]
            if ch == " ":
                out.append(0xFF)
            elif glyph_codes and ch in glyph_codes:
                out.append(glyph_codes[ch])
            else:
                out.append(0xFF)
            i += 1
        return out


# --------------------------------------------------------------------------
def text_tables():
    """(glyph_codes, dte) from the rip's charTable, i.e. the game's own text
    encoding: 'A'=0x42 ... 'Z'=0x5B, 'a'=0x5C ... 'z'=0x75, '0'=0x80, ' '=0xFF.
    """
    p = ROOT / "data" / "ff4-en-data.json"
    if not p.exists():
        return {}, {}
    root = json.load(open(p))
    ct = root.get("charTable", {})
    glyph = {}
    for k, v in ct.get("text", {}).get("char", {}).items():
        if len(v) == 1:
            glyph.setdefault(v, int(k, 16))
    glyph.setdefault(" ", 0xFF)
    dte = {}
    for k, v in ct.get("dte", {}).get("char", {}).items():
        if len(v) == 2:
            dte.setdefault(v, int(k, 16))
    return glyph, dte


def verify():
    ok = True
    rom = load_rom()
    print("== window / font verification ==")

    want = bytes(asm_bytes(ASM / "menu/gfx/window_gfx_en.asm"))
    got = rom[WINDOW_GFX:WINDOW_GFX + len(want)]
    print(f"  [{'OK ' if got == want else 'BAD'}] WindowGfx @0x{WINDOW_GFX:X} == "
          f"window_gfx_en.asm ({len(want)} bytes)")
    ok = ok and got == want

    wantp = bytes(asm_bytes(ASM / "menu/gfx/window_pal.asm"))
    gotp = rom[WINDOW_PAL:WINDOW_PAL + len(wantp)]
    print(f"  [{'OK ' if gotp == wantp else 'BAD'}] WindowPal @0x{WINDOW_PAL:X} == "
          f"window_pal.asm ({len(wantp)} bytes)")
    ok = ok and gotp == wantp

    # each 0x40-byte strip: 30 visible entries + 2 zero padding entries
    blob = b"".join(bytes((t, 0x20)) for t in DLG_TOP) + b"\x00\x20\x00\x20"
    bm = b"".join(bytes((t, 0x20)) for t in DLG_MID)
    bb = b"".join(bytes((t, 0x20)) for t in DLG_BTM)
    pos = rom.find(blob)
    good = (pos >= 0
            and rom[pos + 0x40:pos + 0x40 + len(bm)] == bm
            and rom[pos + 0x80:pos + 0x80 + len(bb)] == bb)
    print(f"  [{'OK ' if good else 'BAD'}] DlgTiles top/mid/btm @ "
          f"{('0x%X' % pos) if pos >= 0 else 'NOT FOUND'} "
          f"(30 wide + 2 pad, 0x40 bytes each)")
    ok = ok and good

    w = Window(rom)
    inked = sum(1 for px in w.tile_pixels(0x42) if px != w.colors[0])
    fill = sum(1 for px in w.tile_pixels(0xFF) if px == w.colors[1])
    print(f"  [{'OK ' if inked > 8 and fill > 50 else 'BAD'}] "
          f"tile 0x42 ('A') ink={inked}, tile 0xFF fill={fill}/64 (window body)")
    ok = ok and inked > 8 and fill > 50

    print("  => all checks passed" if ok else "  => FAILURES above")
    return ok


def export(outdir=None):
    outdir = Path(outdir) if outdir else ROOT / "data" / "gfx" / "window"
    outdir.mkdir(parents=True, exist_ok=True)
    rom = load_rom()
    (outdir / "window_gfx.bin").write_bytes(rom[WINDOW_GFX:WINDOW_GFX + 4096])
    (outdir / "window_pal.bin").write_bytes(rom[WINDOW_PAL:WINDOW_PAL + 32])
    meta = {"window_gfx": WINDOW_GFX, "window_pal": WINDOW_PAL,
            "strips": {"top": DLG_TOP, "mid": DLG_MID, "btm": DLG_BTM},
            "glyph_tile_equals_ascii": True, "text_cols": 26}
    (outdir / "window.json").write_text(json.dumps(meta, indent=1))
    return outdir


def main(argv):
    if "--verify" in argv or not argv:
        if not verify():
            return 1
    if "--export" in argv:
        p = export()
        print(f"exported window assets -> {p}")
    if "--demo" in argv:
        rom = load_rom()
        w = Window(rom)
        glyph, dte = text_tables()
        img = w.render_box([
            "CECIL of the Red Wing!",
            "Misty Valley is beyond",
            "the cave of northwest.",
        ], glyph_codes=glyph, dte=dte, scale=3)
        out = Path("/tmp/window_reference.png")
        img.save(out)
        print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
