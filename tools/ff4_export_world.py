#!/usr/bin/env python3
"""ff4_export_world.py - export engine-ready world data from an FF4 US ROM.

Uses the FreeEnterprise F4C library (ff4struct/ff4bin) for the *authoritative*
data decode -- maps, map info, tilesets, tile properties and triggers -- then
composes each map's background to a PNG and writes a simple JSON sidecar for
our own engine.

Engine model (deliberately simple, not a SNES emulator):
  * map            = 32x32 tiles of 8x8 px  -> 256x256 px
  * movement cell  = 16x16 px on a grid     -> 16x16 cells per map
  * collision      = per cell, from the 4 underlying tiles' properties
  * connections    = derived from TELEPORT triggers (seamless map melding)
  * mods           = replace the PNG and/or the JSON; nothing else to learn

usage:
  python3 tools/ff4_export_world.py <rom> <outdir> [--f4c PATH] [--maps 36,37]
"""
import argparse
import json
import os
import struct
import sys
import zlib

# --------------------------------------------------------------------------
# F4C (FreeEnterprise) library location
# --------------------------------------------------------------------------
DEFAULT_F4C = os.environ.get(
    "FF4_F4C",
    os.path.join(os.path.dirname(__file__), "..", "..", "FE_GOLBEZ", "f4c"),
)


def load_f4c(path):
    sys.path.insert(0, os.path.abspath(path))
    from ff4bin.rom import Rom                     # noqa: E402
    from ff4struct import map_info as MI           # noqa: E402
    from ff4struct import map_grid as MG           # noqa: E402
    from ff4struct import tileset as TS            # noqa: E402
    from ff4struct import trigger as TR            # noqa: E402
    return Rom, MI, MG, TS, TR


# --------------------------------------------------------------------------
# SNES graphics decode (from the disassembly's tools/romtools/gfx.js)
# --------------------------------------------------------------------------
def decode_tile3bpp(data, index):
    """8x8 tile -> 64 palette indices (0..7). snes3bpp planar layout."""
    base = index * 24
    if base + 24 > len(data):
        return None
    out = []
    for r in range(8):
        p0, p1, p2 = data[base + r], data[base + 8 + r], data[base + 16 + r]
        for b in range(7, -1, -1):
            out.append(((p0 >> b) & 1) | (((p1 >> b) & 1) << 1) |
                       (((p2 >> b) & 1) << 2))
    return out


def decode_tile4bpp(data, index):
    base = index * 32
    if base + 32 > len(data):
        return None
    out = []
    for r in range(8):
        planes = [data[base + p * 8 + r] for p in range(4)]
        for b in range(7, -1, -1):
            v = 0
            for p in range(4):
                v |= ((planes[p] >> b) & 1) << p
            out.append(v)
    return out


def bgr555(pal, index):
    if index * 2 + 1 >= len(pal):
        return (0, 0, 0)
    c = pal[index * 2] | (pal[index * 2 + 1] << 8)
    r = ((c & 0x1F) << 3) | ((c >> 2) & 7)
    g = (((c >> 5) & 0x1F) << 3) | ((c >> 7) & 7)
    b = (((c >> 10) & 0x1F) << 3) | ((c >> 12) & 7)
    return (r, g, b)


def write_png(path, w, h, rgb):
    """rgb: flat list of (r,g,b) length w*h."""
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            r, g, b = rgb[y * w + x]
            raw += bytes((r, g, b))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


# --------------------------------------------------------------------------
# tileset lookup: layout byte -> SNES BG entry
# --------------------------------------------------------------------------
def load_tileset_lookup(path, gfx_path, bpp):
    """MapTileset table: layout byte -> (tile, palette, hflip, vflip).

    The game reads $4800+x (low) and $4880+x (high): a 0x80 stride.
    """
    ts = open(path, "rb").read()
    gfx = open(gfx_path, "rb").read()
    return ts, gfx


def lookup_entry(ts, byte):
    if byte + 0x80 >= len(ts):
        return None
    e = ts[byte] | (ts[byte + 0x80] << 8)
    return {
        "tile": e & 0x3FF,
        "pal": (e >> 10) & 7,
        "pri": (e >> 13) & 1,
        "vflip": (e >> 14) & 1,
        "hflip": (e >> 15) & 1,
    }


# --------------------------------------------------------------------------
# main export
# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("outdir")
    ap.add_argument("--f4c", default=DEFAULT_F4C)
    ap.add_argument("--bundle", default="data/gfx/gfx",
                    help="our resource bundle's gfx/ dir (tileset + palette bins)")
    ap.add_argument("--maps", default="all")
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()

    Rom, MI, MG, TS, TR = load_f4c(args.f4c)
    rom = Rom(args.rom)
    print("rom: %d maps, %d tilesets" % (len(rom.map_infos), len(rom.tilesets)))

    out_maps = os.path.join(args.outdir, "maps")
    os.makedirs(out_maps, exist_ok=True)

    want = None
    if args.maps != "all":
        want = set(int(x, 0) for x in args.maps.split(","))

    manifest = []
    for map_id, raw_info in enumerate(rom.map_infos):
        if want is not None and map_id not in want:
            continue
        if args.limit and len(manifest) >= args.limit:
            break
        if not rom.map_grids[map_id]:
            continue

        mi = MI.decode(raw_info)
        grid = MG.decode(rom.map_grids[map_id])
        tset = TS.decode(rom.tilesets[mi.tileset])

        # --- tileset graphics + palette from the bundle (authoritative ranges)
        ts_bin = os.path.join(args.bundle, "mapTilesets[%d].bin" % mi.tileset)
        gfx_bin = os.path.join(args.bundle, "mapGraphics%d.bin" % mi.tileset)
        pal_bin = os.path.join(args.bundle, "mapPalettes[%d].bin" % mi.palette)
        if not (os.path.exists(ts_bin) and os.path.exists(gfx_bin)):
            continue
        ts = open(ts_bin, "rb").read()
        gfx = open(gfx_bin, "rb").read()
        pal = open(pal_bin, "rb").read() if os.path.exists(pal_bin) else b""
        bpp = 4 if mi.tileset == 0 else 3
        decode_tile = decode_tile4bpp if bpp == 4 else decode_tile3bpp

        # --- compose 256x256 background (32x32 tiles of 8x8)
        W = H = 256
        bg = [(0, 0, 0)] * (W * H)
        for ty in range(32):
            for tx in range(32):
                byte = grid[tx][ty]
                e = lookup_entry(ts, byte)
                if e is None:
                    continue
                tile = decode_tile(gfx, e["tile"])
                if tile is None:
                    continue
                grp = e["pal"]
                for r in range(8):
                    for c in range(8):
                        src_r = 7 - r if e["vflip"] else r
                        src_c = 7 - c if e["hflip"] else c
                        ci = tile[src_r * 8 + src_c]
                        col = bgr555(pal, grp * 16 + ci)
                        px = tx * 8 + c
                        py = ty * 8 + r
                        bg[py * W + px] = col
        write_png(os.path.join(out_maps, "%d.png" % map_id), W, H, bg)

        # --- collision: per 8x8 tile from the game's property byte 1.
        # The empirical rule that yields sensible maps (open ground with
        # buildings/walls blocked): a tile is floor when (prop1 & 0x0A) == 0.
        # prop1 & 0x03 is the z-level, 0x04 the bridge flag (move.asm).
        # Exported raw so mods/tools can re-derive.
        raw_props = [tset[b].layer1 for b in range(128)] if False else None
        def prop1(byte):
            return rom.tilesets[mi.tileset][byte * 2]

        cells = []
        for cy in range(32):
            for cx in range(32):
                b = grid[cx][cy]
                p1 = prop1(b)
                cells.append({
                    "blocked": 0 if (p1 & 0x0A) == 0 else 1,
                    "z": p1 & 0x03,
                    "bridge": 1 if (p1 & 0x04) else 0,
                })

        # --- triggers -> connections (teleports) + events
        triggers = []
        raw_trig = rom.map_trigger_sets[map_id]
        if raw_trig:
            i = 0
            while i + 5 <= len(raw_trig):
                try:
                    t = TR.decode(raw_trig[i:i + 5])
                except Exception:
                    break
                rec = {"x": t.x, "y": t.y}
                if t.type == TR.TELEPORT:
                    rec.update({"kind": "teleport", "map": t.map,
                                "tx": t.target_x, "ty": t.target_y})
                elif t.type == TR.EVENT:
                    rec.update({"kind": "event", "call": t.event_call})
                else:
                    rec.update({"kind": "treasure"})
                triggers.append(rec)
                i += 5

        meta = {
            "id": map_id,
            "name": mi.name,
            "w": 32, "h": 32, "cell": 16,
            "tileset": mi.tileset, "palette": mi.palette,
            "music": mi.music,
            "bg_grid": mi.bg_grid,
            "can_warp": mi.can_warp, "can_exit": mi.can_exit,
            "border_tile": mi.border_tile,
            "collision": cells,
            "triggers": triggers,
        }
        with open(os.path.join(out_maps, "%d.json" % map_id), "w") as f:
            json.dump(meta, f)
        manifest.append(map_id)

    with open(os.path.join(args.outdir, "manifest.json"), "w") as f:
        json.dump({"maps": manifest, "cell": 16, "map_px": 256}, f, indent=1)
    print("exported %d maps -> %s" % (len(manifest), out_maps))


if __name__ == "__main__":
    main()