#!/usr/bin/env python3
"""ff4_export_worldmap.py - render FF4's three world maps from the rip JSON.

Single source of truth: data/ff4-en-data.json (the ripped ROM data).
No disassembly parsing, no ROM reads - everything below is in the JSON.

Per world (0 = overworld, 1 = underground, 2 = moon):

  worldLayout{1,2,3}[y]        already RLE-decoded tilemap row (256 bytes;
                               moon: 64 rows of 64).  Values index the
                               tileset table.
  composed tiles (ROM)         the map is built from 128 16x16 tiles, each
                               made of four 8x8 base tiles.  The table is at
                               disasm 0x148000 (file 0xA0000), 0x200 bytes
                               per world as four 128-byte banks: composed
                               tile c's four base tiles are
                               comp[c], comp[c+128], comp[c+256], comp[c+384].
                               (The rip JSON stores this table in a display
                               form, so we read the ROM.)
  worldPaletteAssignments[w]   per-tile palette base (0x00/0x10/0x20/0x30)
  worldGraphics[w]             1 byte per pixel (4bpp already expanded),
                               64 bytes per tile, row-major
  worldPalettes[w]             64 colors as RGBA

  pixel index = worldGraphics[tile*64 + py*8 + px] + base
  color       = worldPalettes[pixel]

usage: python3 tools/ff4_export_worldmap.py [json] [outdir]
"""
import base64
import json
import os
import struct
import sys
import zlib

JSON = sys.argv[1] if len(sys.argv) > 1 else "data/ff4-en-data.json"
OUT = sys.argv[2] if len(sys.argv) > 2 else "data/world"
ROM = sys.argv[3] if len(sys.argv) > 3 else "ff2.smc"

# The rip JSON stores the world tileset lookup in the tool's own display
# form, not as the ROM table.  The game's table is at disasm 0x148000
# (bank:addr -> file offset), 0x200 bytes per world.
WORLD_TILESET_OFF = 0xA0000

# name, layout key, tileset key, graphics key, palette key, assignments key
WORLDS = [
    ("overworld",   "worldLayout1", 256, 256),
    ("underground", "worldLayout2", 256, 256),
    ("moon",        "worldLayout3", 64, 64),
]


def b64(v):
    s = v if len(v) % 4 == 0 else v + "=" * (4 - len(v) % 4)
    return base64.b64decode(s)


def u32(buf, i):
    return (buf[i * 4] | (buf[i * 4 + 1] << 8) |
            (buf[i * 4 + 2] << 16) | (buf[i * 4 + 3] << 24))


def write_png(path, w, h, rgb):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb[y * w * 3:(y + 1) * w * 3]

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def main():
    obj = json.load(open(JSON))
    data = obj.get("obj", obj)
    rom = open(ROM, "rb").read()
    if len(rom) == 0x100200:
        rom = rom[0x200:]

    os.makedirs(OUT, exist_ok=True)
    for wi, (name, lkey, w, h) in enumerate(WORLDS):
        rows = data[lkey]
        comp = rom[WORLD_TILESET_OFF + wi * 0x200:
                   WORLD_TILESET_OFF + (wi + 1) * 0x200]
        gfx = b64(data["worldGraphics"][wi])
        pal = b64(data["worldPalettes"][wi])
        assign = b64(data["worldPaletteAssignments"][wi])
        props = data["worldTileProperties"][wi]

        W, H = w * 16, h * 16          # 16x16 composed tiles
        img = bytearray(W * H * 3)
        idxmap = bytearray(W * H)
        for y in range(h):
            row = b64(rows[y]) if y < len(rows) else b""
            for x in range(w):
                c = row[x] if x < len(row) else 0
                for k in range(4):
                    o = c + k * 128
                    bt = comp[o] if o < len(comp) else 0
                    base = assign[bt] if bt < len(assign) else 0
                    gbase = bt * 64
                    ox, oy = (k % 2) * 8, (k // 2) * 8
                    for py in range(8):
                        for px in range(8):
                            o = gbase + py * 8 + px
                            pv = gfx[o] if o < len(gfx) else 0
                            ci = (pv + base) % 64
                            co = ci * 4
                            d = ((y * 16 + oy + py) * W +
                                 (x * 16 + ox + px))
                            idxmap[d] = ci
                            d *= 3
                            img[d] = pal[co]
                            img[d + 1] = pal[co + 1]
                            img[d + 2] = pal[co + 2]
        # cell -> composed tile number, and composed tile -> its four base
        # tiles: the identity a V-key report needs (see world_view.c)
        cellmap = bytearray(w * h)
        for y in range(h):
            row = b64(rows[y]) if y < len(rows) else b""
            for x in range(w):
                cellmap[y * w + x] = row[x] if x < len(row) else 0
        with open(os.path.join(OUT, "%s.tiles" % name), "wb") as f:
            f.write(bytes(cellmap))
        base = bytearray(512)
        for c in range(128):
            for k in range(4):
                base[c * 4 + k] = comp[c + k * 128]
        with open(os.path.join(OUT, "%s.base" % name), "wb") as f:
            f.write(bytes(base))
        with open(os.path.join(OUT, "%s.assign" % name), "wb") as f:
            f.write(bytes(assign))
        # lava palette animation (field/pal_anim.asm UpdateLavaPal @c4cd):
        # the underground cycles 8 colors (palette group 1, entries 18-25)
        # through 4 sets stepped by LavaPalTbl = 0,1,2,3,3,2,1,0 every 16
        # frames.  LavaAnimPal was located in the ROM by byte pattern
        # (0xA7A16) and validated against the static palette.
        if name == "underground":
            import struct as _st
            off = 0xA7A16
            if off + 64 <= len(rom):
                with open(os.path.join(OUT, "%s.lavapal" % name), "wb") as f:
                    f.write(rom[off:off + 64])

        out = os.path.join(OUT, "%s.png" % name)
        write_png(out, W, H, img)
        # palette-index map (1 byte/pixel) + palette, so the engine can
        # re-map groups at runtime (e.g. a per-world palette shift)
        with open(os.path.join(OUT, "%s.idx" % name), "wb") as f:
            f.write(bytes(idxmap))
        with open(os.path.join(OUT, "%s.pal" % name), "wb") as f:
            f.write(pal[:256])
        # passability: byte1[128] then byte2[128], indexed by CELL VALUE
        with open(os.path.join(OUT, "%s.props" % name), "wb") as f:
            f.write(bytes(t["byte1"] for t in props))
            f.write(bytes(t["byte2"] for t in props))
        # and a per-POSITION walkability byte (byte1 of the cell's tile) so
        # the engine can test the move rule without the layout
        walk = bytearray(w * h)
        for y in range(h):
            row = b64(rows[y]) if y < len(rows) else b""
            for x in range(w):
                c = row[x] if x < len(row) else 0
                walk[y * w + x] = props[c]["byte1"] if c < len(props) else 0
        with open(os.path.join(OUT, "%s.walk" % name), "wb") as f:
            f.write(walk)
        print("%-12s %4dx%-4d tiles=%dx%d -> %s (+ .idx/.pal)" %
              (name, W, H, w, h, out))


if __name__ == "__main__":
    main()