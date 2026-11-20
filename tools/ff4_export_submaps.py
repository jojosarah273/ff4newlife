#!/usr/bin/env python3
"""ff4_export_submaps.py - render field (sub-)maps from the ROM.

The rip's `mapTilesets` / `mapPalettes` are NOT in the ROM's byte layout, so
this follows FF4KSTER's working renderer (`DEV/FF4KSTER_LINUX`), whose
addresses are proven against the real game:

  tile props   0xA1000 + (n-1)*0x100      128 tiles x 2 bytes
  map tileset  0xA2000 + (n-1)*0x400      128 tiles x 4 sub-sprites,
                                          stored as FOUR 0x100 banks
  palettes     0xA6000 + pal*0x80          32 palettes x 8 groups x 8 colors
  sprite sheet per tileset (see SHEETS)    3bpp, 24 bytes per 8x8 tile

  for tile b, sub-sprite j (0..3):
      lo = ts[b*2 + j*0x100] ; hi = ts[b*2 + j*0x100 + 1]
      sprite = lo + (hi & 3)*0x100
      group  = (hi >> 2) & 7
      hflip  = NOT (hi & 0x40)          <- inverted on purpose
      vflip  = hi & 0x80
      draws at ((j%2)*8, (j/2)*8) inside the 16x16 cell

  3bpp tile: planes 0/1 interleaved per row, plane 2 at +16, LSB first.

TWO LAYERS (proven against kaipo1.png):

  BG2 = mapLayouts1[layout2]   drawn FIRST, opaque
  BG1 = mapLayouts1[layout1]   drawn ON TOP, colour index 0 TRANSPARENT

  For Kaipo (map 2) layout2 is a uniform backdrop: all 1024 cells are tile
  101, which resolves to group 4 of palette 4 -- the four blues
  (90,165,255) (33,115,214) (16,90,189) (57,57,57).  That is the sea around
  the town; BG1's cells over it use blank sprites, so the water shows
  through.  Before this fix we drew BG1's index 0 as opaque black, which
  made the sea black and looked like a wrong palette -- it was a missing
  layer all along.

usage: python3 tools/ff4_export_submaps.py [rom] [outdir] [maps]
"""
import json
import os
import struct
import sys
import zlib

ROM = sys.argv[1] if len(sys.argv) > 1 else "ff2.smc"
OUT = sys.argv[2] if len(sys.argv) > 2 else "data/submaps"
JSON = "data/ff4-en-data.json"
COLLISION = "data/collision.json"

# FF4KSTER's addresses assume a HEADERED (.smc) ROM; ours is headerless,
# so every table is 0x200 lower.  Verified by rendering Baron at both and
# comparing local smoothness: 14.8 (correct) vs 54.9 (headered).
HDR = 0x200
TILEPROP = 0xA1000 - HDR
TILESET = 0xA2000 - HDR
PALETTE = 0xA6000 - HDR

# FF4KSTER: spritesheets(n).offset
SHEETS = {k: v - HDR for k, v in {
          1: 0xADE00, 2: 0xF61A8, 3: 0xF0220, 4: 0xEEF40, 5: 0xF1768,
          6: 0xF2EC0, 7: 0xF3AC0, 8: 0xF51E8, 9: 0xF9700, 10: 0xF8200,
          11: 0xF61A8, 12: 0xFD408, 13: 0xFEA88}.items()}


# FF4KSTER: spritesheets(n).bpp -- only sheet 1 (Airship/Ship) is 4bpp,
# every other sheet is 3bpp.  Tilesets 1 and 16 use sheet 1, so ~25 maps
# rendered wrongly until this was found (map 34 scored 18%, 163/192/208 ~5%).
SHEET_BPP = {1: 4}

# FF4KSTER: spritesheets(n).animated_offset -- animated tile graphics live in a
# separate block.  A tile is animated when sprite_index is in 0x120..0x12F and
# the low byte has 0x20 set; its graphic is then read from here at
# (major*16 + minor + frame*4), major = (lo // 4) % 4, minor = lo % 4.
SHEETS_ANIM = {k: v - HDR for k, v in {
          1: 0xFBBC0, 2: 0xFBBC0, 3: 0xFBBC0, 4: 0xFBEC0, 5: 0xFC040,
          6: 0xFC4C0, 7: 0xFC640, 8: 0xFCAC0, 9: 0xFCAC0, 10: 0xFCAC0,
          11: 0xFCAC0, 12: 0xFCF40, 13: 0xFCF40}.items()}


# field/gfx/map_anim_gfx.asm -> MapAnimGfx (verified at ROM 0xFB9C0, 0x1848 B)
ANIM_GFX_OFF = 0xFB9C0
# field/bg_anim.asm BGAnimTbl @cb4f, and LoadBGAnimGfx (@cb01) computes the
# base with a 16-BIT halve:  A = tbl + (tbl >> 1) is the HIGH byte and the
# carry (tbl & 1) << 7 is the LOW byte, i.e. offset = tbl * 384, NOT tbl*3/2.
BG_ANIM_TBL = [0x00, 0x00, 0x00, 0x02, 0x03, 0x06, 0x07, 0x0A,
               0x0A, 0x0A, 0x0A, 0x0A, 0x0D, 0x0D, 0x0D, 0x10]

_anim_cache = {}


def anim_staged(rom, tileset_n):
    """Reproduce LoadBGAnimGfx (@cb01) for tileset `tileset_n` (1-based).

    64 iterations of 32 bytes: 16 raw bytes then 8 (byte, 0x00) pairs.
    Each iteration is one 4bpp VRAM tile (planes 0/1 then planes 2/3, whose
    high byte is the zero written by the loader -> effectively 3bpp).
    """
    if tileset_n in _anim_cache:
        return _anim_cache[tileset_n]
    # tbl * 384 (see the note above); tileset 0 is 0 either way, which is why
    # the ship-deck verification did not catch this
    base = ANIM_GFX_OFF + BG_ANIM_TBL[(tileset_n - 1) & 15] * 384
    pos = base
    out = bytearray()
    for _ in range(64):
        out += rom[pos:pos + 16]
        pos += 16
        for _k in range(8):
            out.append(rom[pos])
            out.append(0)
            pos += 1
    out = bytes(out)
    _anim_cache[tileset_n] = out
    return out


def anim_tile4(staged, m, phase):
    """The 32-byte 4bpp tile the game puts at VRAM tile m for `phase`.

    TfrBGAnimGfx (@cb5f) DMAs 4 chunks of 0x80 bytes at stride 0x200:
        chunk k of phase p starts at staged tile 16*k + 4*p
    so VRAM tile m (0..15) comes from staged tile 16*(m//4) + 4*p + (m%4).
    Verified byte-for-byte against snes9x VRAM ($2400..$25FF, 4/4 phases).
    """
    t = 16 * (m // 4) + 4 * phase + (m % 4)
    return staged[t * 32:(t + 1) * 32]


def anim_tile_px4(t4):
    """8x8 palette indices from a staged 4bpp tile.

    BIT ORDER (torch audit, verified against CopyTilemapToBuffer @f872):
    the game copies the flag pair (lo,hi) VERBATIM as the BG1 tilemap word,
    so hardware renders MSB-left art and flips iff hi bit 6/7 is SET.  The
    static path compensates by storing art LSB-left and flipping when the
    bit is CLEAR (the FF4KSTER convention).  The animated path must use the
    SAME convention as the static path, or mixed cells (torch: animated
    flame + static bracket, e.g. tileset 15 tiles 15/93) render with the
    animated half mirrored - "torches split, right side on the left".
    """
    out = []
    for y in range(8):
        p0, p1 = t4[y * 2], t4[y * 2 + 1]
        p2, p3 = t4[16 + y * 2], t4[16 + y * 2 + 1]
        for x in range(8):
            out.append(((p0 >> x) & 1) | (((p1 >> x) & 1) << 1) |
                       (((p2 >> x) & 1) << 2) | (((p3 >> x) & 1) << 3))
    return out


def anim_phase_px(rom, tileset_n, slot, phase):
    """8x8 colour indices for animated VRAM tile `slot` (0x120..0x12F) at
    `phase`, for the map's tileset.

    EMULATOR-VERIFIED: dumping VRAM $2400..$25FF in the castle-interior scene
    and matching against `anim_staged()` gave 16/16 identical tiles for every
    tile, in two frames 8 apart (see docs/FIELD-ANIM-ANALYSIS.md).  The base is
    MapAnimGfx + BGAnimTbl[tileset-1]*384, the source is 3bpp (24 B/tile) and
    the phase layout is 16*(k//4) + 4*phase + (k%4).
    """
    staged = anim_staged(rom, tileset_n)
    return anim_tile_px4(anim_tile4(staged, slot - 0x120, phase))


def is_anim_sprite(lo, hi):
    """A sub-sprite is animated when its graphic index is 0x120..0x12F and the
    low byte has bit 0x20 set (the tileset format used by the game)."""
    sidx = lo + (hi & 3) * 0x100
    return 0x120 <= sidx <= 0x12F and (lo & 0x20) != 0


def anim_tile_px(rom, sheet, n):
    """Animated tile `n` (frame 0) for `sheet`."""
    bpp = SHEET_BPP.get(sheet, 3)
    size = 32 if bpp == 4 else 24
    base = SHEETS_ANIM.get(sheet, 0) + n * size
    if base + size > len(rom):
        return None
    out = []
    for y in range(8):
        p0 = rom[base + y * 2]
        p1 = rom[base + y * 2 + 1]
        if bpp == 4:
            p2 = rom[base + 16 + y * 2]
            p3 = rom[base + 16 + y * 2 + 1]
        else:
            p2 = rom[base + 16 + y]
            p3 = 0
        for x in range(8):
            out.append(((p0 >> x) & 1) | (((p1 >> x) & 1) << 1) |
                       (((p2 >> x) & 1) << 2) | (((p3 >> x) & 1) << 3))
    return out


def sheet_bpp(n):
    return SHEET_BPP.get(n, 3)


def sheet_for(n):
    """FF4KSTER's tileset -> sprite sheet switch (n is 1-based)."""
    if n in (1, 16):
        return 1
    if 2 <= n <= 10:
        return n
    if n in (11, 12):
        return 9
    if 13 <= n <= 15:
        return n - 2
    return 1


def b64(v):
    import base64
    s = v if len(v) % 4 == 0 else v + "=" * (4 - len(v) % 4)
    return base64.b64decode(s)


def color16(rom, pal, palidx, grp, c):
    o = PALETTE + palidx * 0x80 + (grp * 8 + c) * 2
    w = rom[o] | (rom[o + 1] << 8)
    return (((w & 0x1F) << 3) | ((w >> 2) & 7),
            (((w >> 5) & 0x1F) << 3) | ((w >> 7) & 7),
            (((w >> 10) & 0x1F) << 3) | ((w >> 12) & 7))


def tile_px(rom, sheet, idx):
    bpp = SHEET_BPP.get(sheet, 3)
    if bpp == 4:
        # 4bpp: 32 bytes/tile, planes 2/3 interleaved at +16, values 0..15
        base = SHEETS[sheet] + idx * 32
        if base + 32 > len(rom):
            return None
        out = []
        for y in range(8):
            p0 = rom[base + y * 2]
            p1 = rom[base + y * 2 + 1]
            p2 = rom[base + 16 + y * 2]
            p3 = rom[base + 16 + y * 2 + 1]
            for x in range(8):
                out.append(((p0 >> x) & 1) | (((p1 >> x) & 1) << 1) |
                           (((p2 >> x) & 1) << 2) | (((p3 >> x) & 1) << 3))
        return out
    base = SHEETS[sheet] + idx * 24
    if base + 24 > len(rom):
        return None
    out = []
    for y in range(8):
        p0 = rom[base + y * 2]
        p1 = rom[base + y * 2 + 1]
        p2 = rom[base + 16 + y]
        for x in range(8):
            out.append(((p0 >> x) & 1) | (((p1 >> x) & 1) << 1) |
                       (((p2 >> x) & 1) << 2))
    return out


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
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
        f.write(chunk(b"IEND", b""))


def draw_cells(rom, ts, sheet, palidx, blob, W, img, transparent, n=1, phase=0):
    """Draw one 32x32-cell layer into `img`.

    transparent=True skips colour index 0, which is how BG1 lets BG2 show
    through (SNES BG transparency).  BG2 is drawn with transparent=False.
    `phase` selects the animation frame for cells whose tile is animated.
    """
    for cy in range(32):
        for cx in range(32):
            b = blob[cy * 32 + cx]
            if b >= 128:
                continue
            for j in range(4):
                lo = rom[ts + b * 2 + j * 0x100]
                hi = rom[ts + b * 2 + j * 0x100 + 1]
                sidx = lo + (hi & 3) * 0x100
                grp = (hi >> 2) & 7
                hf = not (hi & 0x40)
                vf = bool(hi & 0x80)
                if is_anim_sprite(lo, hi):
                    # frame 0 of the emulator-verified animated block for this
                    # tileset (see anim_phase_px / docs/FIELD-ANIM-ANALYSIS.md)
                    t = anim_phase_px(rom, n, sidx, phase)
                else:
                    t = tile_px(rom, sheet, sidx)
                if t is None:
                    continue
                ox, oy = (j % 2) * 8, (j // 2) * 8
                for y in range(8):
                    for x in range(8):
                        src = (7 - y if vf else y) * 8 + (7 - x if hf else x)
                        c = t[src]
                        if transparent and c == 0:
                            continue
                        r, g, bl = color16(rom, palidx, palidx, grp, c & 7)
                        d = ((cy * 16 + oy + y) * W + cx * 16 + ox + x) * 3
                        img[d], img[d + 1], img[d + 2] = r, g, bl


def draw_anim_frame(rom, ts, sheet, palidx, blobs, W, img, frame, n=1):
    """Draw ONLY the animated cells of a map, for animation phase `frame`.

    An animated sub-sprite is a graphic index 0x120..0x12F (field/bg_anim.asm
    + the tileset format).  Its four phases come from the tileset's animated
    block (anim_phase_px, emulator-verified):

        staged tile = 16*(k//4) + 4*phase + (k%4),  k = index - 0x120

    The result is an RGBA overlay (transparent everywhere except animated
    cells) that the runtime draws on top of the static map, advancing the
    phase every 8 frames like TfrBGAnimGfx ($7a & 0x18, $7a & 0x06 == 0).
    """
    drew = 0
    for blob in blobs:
        if not blob or len(blob) < 1024:
            continue
        for cy in range(32):
            for cx in range(32):
                b = blob[cy * 32 + cx]
                if b >= 128:
                    continue
                for j in range(4):
                    lo = rom[ts + b * 2 + j * 0x100]
                    hi = rom[ts + b * 2 + j * 0x100 + 1]
                    if not is_anim_sprite(lo, hi):
                        continue
                    sidx = lo + (hi & 3) * 0x100
                    grp = (hi >> 2) & 7
                    hf = not (hi & 0x40)
                    vf = bool(hi & 0x80)
                    t = anim_phase_px(rom, n, sidx, frame)
                    drew += 1
                    ox, oy = (j % 2) * 8, (j // 2) * 8
                    for y in range(8):
                        for x in range(8):
                            src = (7 - y if vf else y) * 8 + (7 - x if hf else x)
                            c = t[src]
                            if c == 0:
                                continue
                            r, g, bl = color16(rom, palidx, palidx, grp, c & 7)
                            d = ((cy * 16 + oy + y) * W + cx * 16 + ox + x) * 4
                            img[d], img[d + 1], img[d + 2], img[d + 3] = \
                                r, g, bl, 255
    return drew


def draw_overhang(rom, ts, sheet, palidx, blob, W, img, n=1, phase=0):
    """Draw the OVERHANG layer: sub-sprites flagged "front" (hi byte & 0x20).

    FF4KSTER (types/gameobjects/methods/tileset.bas):
        tiles(i).sprite(j).front = iif(hi and &h20, true, false)
    Front sub-sprites are drawn AFTER the actor, so the player walks under
    roofs, tree canopy and bridges.  This is PER SUB-SPRITE -- a 16x16 cell
    can have only its top half in front.

    The previous rule (tileprop byte1 & 0x01) was wrong: on Village Mist it
    flagged tile 0 (133 cells, which reads as grass painted over the player)
    and did NOT flag tile 21 (587 cells, the canopy/roofs) that must be.
    """
    for cy in range(32):
        for cx in range(32):
            b = blob[cy * 32 + cx]
            if b >= 128:
                continue
            for j in range(4):
                lo = rom[ts + b * 2 + j * 0x100]
                hi = rom[ts + b * 2 + j * 0x100 + 1]
                if not (hi & 0x20):
                    continue                      # drawn behind the actor
                sidx = lo + (hi & 3) * 0x100
                grp = (hi >> 2) & 7
                hf = not (hi & 0x40)
                vf = bool(hi & 0x80)
                # some animated tiles are also front (ts3 tile 69, ts5 tiles
                # 51/52 - waterfalls in front of the actor): draw the phase
                if is_anim_sprite(lo, hi):
                    t = anim_phase_px(rom, n, sidx, phase)
                else:
                    t = tile_px(rom, sheet, sidx)
                if t is None:
                    continue
                ox, oy = (j % 2) * 8, (j // 2) * 8
                for y in range(8):
                    for x in range(8):
                        src = (7 - y if vf else y) * 8 + (7 - x if hf else x)
                        c = t[src]
                        if c == 0:
                            continue
                        r, g, bl = color16(rom, palidx, palidx, grp, c & 7)
                        d = ((cy * 16 + oy + y) * W + cx * 16 + ox + x) * 4
                        img[d], img[d + 1], img[d + 2], img[d + 3] = \
                            r, g, bl, 255


def write_png_rgba(path, w, h, rgba):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgba[y * w * 4:(y + 1) * w * 4]

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
        f.write(chunk(b"IEND", b""))


DIR_BITS = {"up": 1, "right": 2, "down": 4, "left": 8}


def parse_dirs(spec):
    """'lr' / 'up,down' / 10 -> allowed-direction bit mask."""
    if isinstance(spec, int):
        return spec & 0xF
    mask = 0
    for part in str(spec).replace(" ", "").split(","):
        if not part:
            continue
        if part.isdigit():
            mask |= int(part) & 0xF
            continue
        for ch in part:
            mask |= {"u": 1, "r": 2, "d": 4, "l": 8}.get(ch.lower(), 0)
    return mask or 0xF


NPC_PROP_BASE = 0x098300     # file offset of NPCProp
NPC_PROP_LEN = 0x13E8


def find_npc_graphic(rom, sw, x, y):
    """Recover an NPC's graphic id from the NPCProp 4-byte records.

    Records are [switch0, x|flags, y|flags, graphic] and contiguous, but the
    per-map block offsets drift (a running sum of NPC counts is off by a byte
    for most maps), so match on the NPC's identity instead of computing an
    offset.  (switch0, x, y) was unique for every NPC tested.
    """
    for off in range(NPC_PROP_BASE, NPC_PROP_BASE + NPC_PROP_LEN - 4):
        b = rom[off:off + 4]
        if b[0] == (sw & 0xFF) and (b[1] & 0x1F) == (x & 0x1F) \
                and (b[2] & 0x1F) == (y & 0x1F):
            return b[3]
    return None


NPC_PAL_FILE = "data/npc_pal.json"


def load_npc_palettes():
    """{map: [pal_selector0, pal_selector1]} measured in game."""
    try:
        d = json.load(open(NPC_PAL_FILE))
    except Exception:                                  # noqa: BLE001
        return {}
    out = {}
    for k, v in (d.get("maps") or {}).items():
        try:
            out[int(k)] = [int(v[0]) & 0x3F, int(v[1]) & 0x3F]
        except Exception:                              # noqa: BLE001
            continue
    return out


def load_collision():
    """-> ({tileset: {tile: 0|1}}, {tileset: {tile: dir_mask}})."""
    table, dirs = {}, {}
    try:
        d = json.load(open(COLLISION))
    except Exception:                                  # noqa: BLE001
        return table, dirs
    for ts_key, spec in (d.get("tilesets") or {}).items():
        try:
            ts = int(ts_key)
        except ValueError:
            continue
        m = {}
        for t in spec.get("impassable") or []:
            m[int(t)] = 0
        for t in spec.get("passable") or []:
            m[int(t)] = 1
        table[ts] = m
        dm = {}
        for t, ds in (spec.get("dirs") or {}).items():
            dm[int(t)] = parse_dirs(ds)
        if dm:
            dirs[ts] = dm
    return table, dirs


def load_layout_rom():
    """data/layout_rom.json: layouts decoded straight from the ROM
    (tools/ff4_layout_rom.py).  The rip is EMPTY for every layout whose data
    crosses a ROM bank boundary - 63 ids, including map 27's - so the ROM
    decode fills those gaps (and matches the rip byte-for-byte elsewhere)."""
    import base64 as _b64
    from pathlib import Path as _P
    p = _P(__file__).resolve().parent.parent / "data" / "layout_rom.json"
    if not p.exists():
        return {}, {}
    d = json.load(open(p))
    def dec(t):
        return {int(k): _b64.b64decode(v) for k, v in t.items()}
    return dec(d.get("group0", {})), dec(d.get("group1", {}))


def main():
    rom = open(ROM, "rb").read()
    if len(rom) == 0x100200:
        rom = rom[0x200:]
    obj = json.load(open(JSON))
    o = obj.get("obj", obj)
    props = o["mapProperties"]
    layouts1 = o["mapLayouts1"]
    layouts2 = o["mapLayouts2"]

    want = None
    if len(sys.argv) > 3:
        want = set(int(x, 0) for x in sys.argv[3].split(","))
    os.makedirs(OUT, exist_ok=True)

    layout_rom_g0, layout_rom_g1 = load_layout_rom()
    coll_table, dir_table = load_collision()
    pal_table = load_npc_palettes()
    # running NPC count per map, for the npcGraphicsProperties index
    npc_flat_offset = {}
    _n = 0
    for _m in range(len(props)):
        npc_flat_offset[_m] = _n
        _pl = props[_m]
        if isinstance(_pl, dict):
            try:
                _l = o["npcProperties"][_m]
            except (KeyError, IndexError):
                _l = None
            if isinstance(_l, list):
                _n += len(_l)
    done = 0
    anim_maps = 0
    def bg2_for(p, group):
        """BG2 blob for a layout group (group 1 = underground/moon)."""
        lay2 = p.get("layout2", -1)
        if group == 1:
            g = layout_rom_g1.get(lay2, b"")
            if len(g) == 1024:
                return g
            # no group-1 backdrop: fall back to group 0
        if 0 <= lay2 < len(layouts1) and layouts1[lay2]:
            return b64(layouts1[lay2])
        return layout_rom_g0.get(lay2, None) if lay2 in layout_rom_g0 else None

    def export_one(mid, blob, bg2, suffix=""):
        """Render one map (or its group-1 variant) and write all files."""
        nonlocal done, anim_maps
        p = props[mid]
        lay = p.get("layout1", -1)
        n = p.get("graphics", 0) + 1          # rip is 0-based, KSTER 1-based
        if n < 1 or n > 16:
            return
        tsn = n                     # keep the tileset number; `n` is reused
                                    # later as the NPC dict in the npc loop
        palidx = p.get("palette", 0)
        sheet = sheet_for(n)
        ts = TILESET + (n - 1) * 0x400

        W = H = 512
        img = bytearray(W * H * 3)

        # BG2 (backdrop) first, then BG1 over it with index 0 transparent.
        #
        # NOTE: BOTH layout1 and layout2 index mapLayouts1.  Evidence:
        #   * layout1 and layout2 both range 0..255, but mapLayouts2 only has
        #     128 entries -- 80 of 383 maps would be out of range.
        #   * mapLayouts1[layout2] has a median of 1 distinct tile per map
        #     (a uniform backdrop); mapLayouts2[layout2] has 48.
        #   * Kaipo: mapLayouts1[34] is all tile 101 = the sea.
        # NOTE: `bg2` is a PARAMETER (bg2_for(p, group)): the caller picks
        # the group-1 backdrop for the underground variants.  This block used
        # to rebind it, which silently rendered every .g1.png with the
        # GROUP-0 backdrop - fixed; the base pass passes the same value it
        # always computed, so group-0 output is unchanged.
        if bg2 is not None and len(bg2) >= 1024:
            draw_cells(rom, ts, sheet, palidx, bg2, W, img, transparent=False, n=tsn)
        draw_cells(rom, ts, sheet, palidx, blob, W, img, transparent=True, n=tsn)

        write_png(os.path.join(OUT, "%d%s.png" % (mid, suffix)), W, H, img)

        # --- tile animation: full per-phase composites ------------------
        # A cell is animated when any sub-sprite's graphic index is
        # 0x120..0x12F (field/bg_anim.asm + FF4KSTER's tileset decode; the
        # phase source is anim_phase_px, emulator-verified - see
        # docs/FIELD-ANIM-ANALYSIS.md).  For those maps we re-render the WHOLE
        # map (BG2 then BG1) for phases 1..3 as <id>.f1..f3.png, and the
        # runtime swaps the texture every 8 frames.  Compositing per phase
        # keeps BG2 under BG1 by construction: the earlier overlay approach
        # painted an animated BG2 sea OVER the BG1 town drawn on it.
        map_anim = False
        for src_blob in (bg2, blob):
            if src_blob is None or len(src_blob) < 1024:
                continue
            for ci in range(1024):
                b = src_blob[ci]
                if b >= 128:
                    continue
                for j in range(4):
                    lo2 = rom[ts + b * 2 + j * 0x100]
                    hi2 = rom[ts + b * 2 + j * 0x100 + 1]
                    if is_anim_sprite(lo2, hi2):
                        map_anim = True
                        break
                if map_anim:
                    break
            if map_anim:
                break
        if map_anim:
            for ph in range(1, 4):
                img_f = bytearray(W * H * 3)
                if bg2 is not None and len(bg2) >= 1024:
                    draw_cells(rom, ts, sheet, palidx, bg2, W, img_f,
                               transparent=False, n=tsn, phase=ph)
                draw_cells(rom, ts, sheet, palidx, blob, W, img_f,
                           transparent=True, n=tsn, phase=ph)
                write_png(os.path.join(OUT, "%d%s.f%d.png" % (mid, suffix, ph)),
                          W, H, img_f)
                # overhang phase variant (animated waterfalls are front)
                over_f = bytearray(W * H * 4)
                draw_overhang(rom, ts, sheet, palidx, blob, W, over_f,
                              n=tsn, phase=ph)
                write_png_rgba(os.path.join(OUT, "%d%s.fover%d.png" % (mid, suffix, ph)),
                               W, H, over_f)
            anim_maps += 1

        # collision + per-cell tile properties (2 bytes/cell: byte1, byte2)
        tp = TILEPROP + (n - 1) * 0x100
        walk = bytearray(1024)
        # NOTE: do not name this `props` -- that shadows mapProperties above,
        # which silently broke the loop after the first map.
        cell_props = bytearray(2048)

        # A tile with no opaque pixel is "nothing here" (e.g. Kaipo's sea:
        # BG1 tile 50 is fully transparent and only BG2's water shows).
        # Its props still claim a z-level, so it must be forced impassable or
        # the player can walk on open water.
        opaque_cache = {}

        def tile_opaque(t):
            if t in opaque_cache:
                return opaque_cache[t]
            ok = False
            for j in range(4):
                lo2 = rom[ts + t * 2 + j * 0x100]
                hi2 = rom[ts + t * 2 + j * 0x100 + 1]
                px2 = tile_px(rom, sheet, lo2 + (hi2 & 3) * 0x100)
                if px2 and any(px2):
                    ok = True
                    break
            opaque_cache[t] = ok
            return ok

        # CheckTilePass (move.asm @aa58) masks the neighbour with the DP
        # variable $0A = (current & 3) + 4 (move.asm @a965); the assembled
        # opcode is 25 0A = "and $0A", not the immediate 0x0A.
        # byte1 bits 0/1 are the passable lanes, bit 2 = bridge.
        # A cell is therefore standable when it has a lane (0x03) or is a
        # bridge (0x04); blank tiles (no opaque pixel) are void/sea.
        # .walk stores the flattened RESULT; the runtime uses the z-aware rule.
        for i in range(1024):
            b = blob[i]
            if b < 128:
                b1 = rom[tp + b * 2]
                b2 = rom[tp + b * 2 + 1]
                over = coll_table.get(n, {})
                if b in over:
                    walk[i] = over[b]
                else:
                    walk[i] = 1 if ((b1 & 0x03) or (b1 & 0x04)) else 0
                cell_props[i * 2] = b1
                cell_props[i * 2 + 1] = b2
        with open(os.path.join(OUT, "%d%s.walk" % (mid, suffix)), "wb") as f:
            f.write(walk)
        with open(os.path.join(OUT, "%d%s.props" % (mid, suffix)), "wb") as f:
            f.write(cell_props)
        # tile index per cell (0..127, 255 = empty) so the engine and the
        # V-key dump can name the tile, not just its property bytes.
        # per-cell allowed-direction mask (0x0F = unrestricted); applied to
        # BOTH entering and leaving a cell, so e.g. a bed blocks up/down.
        dtab = dir_table.get(n, {})
        dirs = bytearray(1024)
        for i in range(1024):
            b = blob[i]
            dirs[i] = dtab.get(b, 0x0F) if b < 128 else 0x0F
        with open(os.path.join(OUT, "%d%s.dirs" % (mid, suffix)), "wb") as f:
            f.write(dirs)

        # NPCs are per-MAP, not per-layout-group: only the base pass writes them
        if suffix == "":
                # --- NPCs: position, facing, palette, sprite sheet, dialogue ---
                # npcProperties is a per-map list; mapDialog[mid] holds that map's lines.
                # NOTE: npcGraphicsProperties has only 512 entries but there are ~1223
                # NPCs in total, so the graphic index is NOT a simple running count --
                # it is a best-effort guess here and should be re-derived from the ROM.
                try:
                    nlist = o["npcProperties"][p["npc"]] or []
                except Exception:                              # noqa: BLE001
                    nlist = []
                dlg = (o.get("mapDialog") or [])
                dlg = dlg[mid] if mid < len(dlg) else []
                gp = o.get("npcGraphicsProperties") or []
                gidx = npc_flat_offset.get(mid, 0)
                with open(os.path.join(OUT, "%d.npc" % mid), "w") as f:
                    for i, n in enumerate(nlist):
                        # NB: do NOT call this `sheet` -- that is the map's tileset
                        # sheet and clobbering it breaks the next map's render.
                        # VERIFIED against the disassembly + ROM:
                        #   InitNPCs:  lda f:NPCProp,x / jsr LoadNPCGfx
                        #   LoadNPCGfx: ldx $3d / lda f:NPCGfxID,x
                        # so NPC record BYTE 0 is the graphic id (the rip labels that
                        # byte "switch0"), and $1701 (the MAP's MSB, i.e.
                        # mapProperties.npcMSB) adds 256 for the second bank.
                        # NPCGfxID == npcGraphicsProperties (512 entries, both start
                        # 00, 65, 65).  NPCPropPtrs is at 0x098000, NPCProp 0x098300.
                        gfx = (n.get("switch0", 0) & 0xFF)
                        if p.get("npcMSB"):
                            gfx += 256
                        npc_sheet = gp[gfx] if gfx < len(gp) else 0
                        text = dlg[i] if i < len(dlg) else ""
                        text = text.replace("\\n", " ").replace("\\t", " ")
                        # CORRECT NPC PALETTE FORMULA (from SNES ASM reverse engineering):
                        # Characters (sheet < 14): use npcPalette1
                        #   final = (15 + 2*(npcPalette1 & 7) + PlayerPalTbl[char_id]//2) % 36
                        # NPCs (sheet >= 14): use npcPalette2
                        #   final = (15 + 2*(npcPalette2 & 7) + palette_field//2) % 36
                        # The magic number 15 comes from empirical verification: Baron uses palettes 15 and 17
                        PlayerPalTbl = [0, 0, 1, 2, 2, 2, 0, 1, 1, 3, 0, 1, 0, 0, 0, 0, 0, 0]
                        if npc_sheet < 14:
                            # Character sprite
                            char_pal = PlayerPalTbl[npc_sheet] if npc_sheet < len(PlayerPalTbl) else 0
                            npc_pal = (15 + 2 * (p.get("npcPalette1", 0) & 7)) % 36
                        else:
                            # Regular NPC
                            pal_field = n.get("palette", 0)
                            npc_pal = (15 + 2 * (p.get("npcPalette2", 0) & 7)) % 36
                        f.write("%d %d %d %d %d %d %d %d\t%s\n"
                                % (n.get("x", 0) & 31, n.get("y", 0) & 31,
                                   n.get("direction", 0) & 3,
                                   npc_pal & 0xFF, npc_sheet,
                                   1 if n.get("randomMovement") else 0,
                                   n.get("speed", 1) & 3,
                                   1 if n.get("animated") else 0, text))
        with open(os.path.join(OUT, "%d%s.tiles" % (mid, suffix)), "wb") as f:
            f.write(bytes((b if b < 128 else 255) for b in blob))

        # OVERHANG layer: sub-sprites with hi & 0x20 ("front") draw above
        # the actor, so the player can walk under roofs/canopy/bridges.
        over = bytearray(W * H * 4)
        draw_overhang(rom, ts, sheet, palidx, blob, W, over, n=tsn)
        write_png_rgba(os.path.join(OUT, "%d%s.over.png" % (mid, suffix)), W, H, over)

        # (the old .anim*.png overlay export was removed: per-phase
        # composites above replace it, and they cannot mis-layer)

        if suffix == "":
            t = p.get("title")
            titles = o.get("mapTitle") or []
            nm = titles[t] if isinstance(t, int) and 0 <= t < len(titles) else ""
            n_upper = 0
            for b in set(blob):
                if b < 128:
                    for j in range(4):
                        if rom[ts + b * 2 + j * 0x100 + 1] & 0x20:
                            n_upper += 1
            n_hidez = sum(1 for i in range(1024)
                          if cell_props[i * 2 + 1] & 0x04)
            n_hidebot = sum(1 for i in range(1024)
                            if cell_props[i * 2 + 1] & 0x08)
            meta = {"id": mid, "w": 32, "h": 32, "cell": 16,
                    "tileset": n, "palette": palidx, "name": nm,
                    "layout1": p.get("layout1", -1),
                    "layout2": p.get("layout2", -1),
                    "has_bg2": bg2 is not None,
                    "overhang_cells": n_upper,
                    "hide_z_cells": n_hidez,
                    "hide_bottom_cells": n_hidebot}
            with open(os.path.join(OUT, "%d.json" % mid), "w") as f:
                json.dump(meta, f)
        done += 1

    for mid in range(len(props)):
        if want is not None and mid not in want:
            continue
        p = props[mid]
        if not isinstance(p, dict):
            continue
        lay = p.get("layout1", -1)
        if not (0 <= lay < len(layouts1)):
            continue
        blob = b64(layouts1[lay]) if lay < len(layouts1) and layouts1[lay] \
            else layout_rom_g0.get(lay, b"")
        if len(blob) < 1024:
            continue
        export_one(mid, blob, bg2_for(p, 0))
        # group-1 layout variant (DecodeSubTilemap @feba: group =
        # layoutMSB | $1701 != 0 -> pointer word 256+id, base 18/8000).
        # The underground/moon session renders EVERY map from that table.
        g1 = layout_rom_g1.get(lay, b"")
        if mid <= 127 and len(g1) == 1024 and g1 != blob:
            export_one(mid, g1, bg2_for(p, 1), ".g1")

    print("rendered %d sub-maps -> %s (%d with animated tiles)"
          % (done, OUT, anim_maps))

    # NPC records are NOT derived here any more.  Palette selection comes from
    # the SNES code (LoadPlayerPal / LoadNPCPal / InitNPCs), so data/submaps/*.npc
    # is produced by tools/ff4_palette_truth.py --export-npc, which reads the ROM
    # tables directly (MapSpritePal @0x68000, MapProp @0xA9C84, NPCProp @0x98300).
    try:
        import sys as _sys
        _sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import ff4_palette_truth as _pt
        _rom = _pt.Rom(ROM if ROM else None)
        _n = _pt.export_npc_files(_rom, OUT)
        print("npc files regenerated from ROM tables: %d" % _n)
    except Exception as _e:                                    # noqa: BLE001
        print("WARNING: could not regenerate .npc files (%s);" % _e)
        print("         run: python3 tools/ff4_palette_truth.py --export-npc")


if __name__ == "__main__":
    main()