#!/usr/bin/env python3
"""
ff4data - THE uniform data layer for the FF4 engine (docs/ARCHITECTURE.md).

One reader, one schema.  Every table is located in the ROM once (with its
ASM symbol name), decoded here, and emitted as runtime blobs the engine
consumes directly - replacing the per-map PNG bakery (<id>.png, .over,
.f1..3, .fover1..3, .g1 variants) with shared art + per-map cell arrays.

The PNGs the other tools export stay as VERIFICATION REFERENCES (guards
compare the engine against them); the engine no longer loads them.

Emitted runtime data
  data/gfx/sheets/sheet{n}.idx     0x400 tiles x 64 index bytes (colour idx)
  data/gfx/anim/ts{n}.idx          animated slots: 4 phases x 16 slots x 64
  data/gfx/tileflags/ts{n}.bin     0x400 bytes: (lo, hi) per sub-tile
  data/gfx/palettes.bin            lines x 8 groups x 8 colours, RGBA
  data/maps/{id}.ff4map            cells + props + meta, both layout groups

Usage:
  python3 tools/ff4data.py               # emit everything
  python3 tools/ff4data.py --verify     # + structural self-test vs PNGs
"""
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import ff4_export_submaps as E            # noqa: E402  (verified decoders)

ROM_PATH = ROOT / "ff2.smc"
SHEETS = {k: v - 0x200 for k, v in {      # FF4KSTER spritesheets (headered)
    1: 0xADE00, 2: 0xF61A8, 3: 0xF0220, 4: 0xEEF40, 5: 0xF1768,
    6: 0xF2EC0, 7: 0xF3AC0, 8: 0xF51E8, 9: 0xF9700, 10: 0xF8200,
    11: 0xF61A8, 12: 0xFD408, 13: 0xFEA88}.items()}
SHEET_BPP = {1: 4}                         # only the airship sheet is 4bpp
TILESET = E.TILESET                       # 0xA2000 - 0x200: 16 x 0x400 tables
PALETTE = E.PALETTE                       # 0xA6000 - 0x200: palette lines
ANIM_GFX = 0xFB9C0                        # MapAnimGfx (bg_anim.asm)
BG_ANIM_TBL = E.BG_ANIM_TBL
TILEPROP = 0xA0000 - 0x200               # per-tileset: byte1[128], byte2[128]

MAPS_OUT = ROOT / "data" / "maps"
GFX_OUT = ROOT / "data" / "gfx"
PALETTE_LINES = 32                         # lines stored (palette ids 0..31)


def rom_body():
    b = open(ROM_PATH, "rb").read()
    return b[0x200:] if len(b) == 0x100200 else b


def sheet_for_tileset(n):
    """tileset (1..16) -> sprite sheet id (the FF4KSTER mapping)."""
    if n in (1, 16):
        return 1
    if 2 <= n <= 10:
        return n
    if n in (11, 12):
        return 9
    if 13 <= n <= 15:
        return n - 2
    return 1


def decode_tile(body, off, bpp):
    """64 colour indices (0..15) for one tile at `off`."""
    out = []
    for y in range(8):
        if bpp == 4:
            p0 = body[off + y * 2]
            p1 = body[off + y * 2 + 1]
            p2 = body[off + 16 + y * 2]
            p3 = body[off + 16 + y * 2 + 1]
        else:
            p0 = body[off + y * 2]
            p1 = body[off + y * 2 + 1]
            p2 = body[off + 16 + y]
            p3 = 0
        for b in range(8):
            out.append(((p0 >> b) & 1) | (((p1 >> b) & 1) << 1) |
                       (((p2 >> b) & 1) << 2) | (((p3 >> b) & 1) << 3))
    return out


def emit_sheets(body):
    (GFX_OUT / "sheets").mkdir(parents=True, exist_ok=True)
    meta = {}
    for n, off in SHEETS.items():
        bpp = SHEET_BPP.get(n, 3)
        tsize = 32 if bpp == 4 else 24
        out = bytearray()
        avail = min(0x400, (len(body) - off) // tsize)
        for i in range(0x400):
            if i < avail:
                out += bytes(decode_tile(body, off + i * tsize, bpp))
            else:
                out += bytes(64)          # beyond the sheet: transparent
        (GFX_OUT / "sheets" / f"sheet{n}.idx").write_bytes(out)
        meta[n] = {"bpp": bpp, "tiles": 0x400}
    (GFX_OUT / "sheets" / "manifest.json").write_text(json.dumps(meta))
    return len(meta)


def emit_anim(body):
    """Animated slots, per tileset: 4 phases x 16 slots x 64 index bytes."""
    (GFX_OUT / "anim").mkdir(parents=True, exist_ok=True)
    for ts in range(1, 17):
        staged = E.anim_staged(body, ts)          # emulator-verified staging
        out = bytearray()
        for p in range(4):
            for k in range(16):
                out += bytes(E.anim_tile_px4(E.anim_tile4(staged, k, p)))
        (GFX_OUT / "anim" / f"ts{ts}.idx").write_bytes(out)
    return 16


def emit_tileflags(body):
    (GFX_OUT / "tileflags").mkdir(parents=True, exist_ok=True)
    for n in range(1, 17):
        off = TILESET + (n - 1) * 0x400
        (GFX_OUT / "tileflags" / f"ts{n}.bin").write_bytes(body[off:off + 0x400])
    return 16


def emit_palettes(body):
    """Palette lines: 8 groups x 8 colours, BGR555 -> RGBA (0 = transparent
    marker: colour 0 of group 0 stays (0,0,0,0))."""
    out = bytearray()
    for line in range(PALETTE_LINES):
        for g in range(8):
            for c in range(8):
                o = PALETTE + line * 0x80 + (g * 8 + c) * 2
                w = struct.unpack_from("<H", body, o)[0]
                r = ((w & 0x1F) << 3) | ((w >> 2) & 7)
                gg = (((w >> 5) & 0x1F) << 3) | ((w >> 7) & 7)
                b = (((w >> 10) & 0x1F) << 3) | ((w >> 12) & 7)
                # colour 0 is a REAL colour (BG2 backdrops draw it);
                # transparency is a per-pass render decision, not a
                # palette property
                out += bytes([r, gg, b, 255])
    (GFX_OUT / "palettes.bin").write_bytes(out)
    return PALETTE_LINES


def layouts(rom_layouts):
    """group0/group1 layout cells, straight from the ROM decode."""
    return rom_layouts


def emit_maps(body):
    """One pack per map: cells (bg1/bg2, both groups) + props + meta."""
    obj = json.load(open(ROOT / "data" / "ff4-en-data.json"))
    o = obj.get("obj", obj)
    props = o["mapProperties"]
    lay_rom = json.load(open(ROOT / "data" / "layout_rom.json"))
    import base64

    def b64d(v):
        return base64.b64decode(v if len(v) % 4 == 0 else v + "=" * (-len(v) % 4))

    g0 = {int(k): b64d(v) for k, v in lay_rom["group0"].items()}
    g1 = {int(k): b64d(v) for k, v in lay_rom["group1"].items()}
    MAPS_OUT.mkdir(parents=True, exist_ok=True)
    done = 0
    for mid, p in enumerate(props):
        if not isinstance(p, dict):
            continue
        lay = p.get("layout1", -1)
        lay2 = p.get("layout2", -1)
        if not (0 <= lay < 256) or len(g0.get(lay, b"")) != 1024:
            continue
        n = p.get("graphics", 0) + 1
        if not (1 <= n <= 16):
            continue
        bg2_g0 = g0.get(lay2, b"") if 0 <= lay2 < 256 else b""
        has_g1 = mid <= 127 and len(g1.get(lay, b"")) == 1024 and \
            g1[lay] != g0[lay]
        # the game's group-1 BG2 falls back to group 0 when the id has no
        # group-1 backdrop (bg2_for() in the exporter does the same)
        bg2_g1 = b""
        if has_g1:
            if 0 <= lay2 < 128 and len(g1.get(lay2, b"")) == 1024:
                bg2_g1 = g1[lay2]
            else:
                bg2_g1 = bg2_g0

        # per-cell props from the tileset's property table
        tp = TILEPROP + (n - 1) * 0x100
        cellb1 = bytearray(1024)
        cellb2 = bytearray(1024)
        for i in range(1024):
            t = g0[lay][i]
            if t < 128:
                cellb1[i] = body[tp + t * 2]
                cellb2[i] = body[tp + t * 2 + 1]

        hdr = struct.pack("<4sHHHBB", b"FFM1", n, sheet_for_tileset(n),
                          p.get("palette", 0) & 0xFF,
                          1 if has_g1 else 0,
                          1 if len(bg2_g0) == 1024 else 0)
        blob = bytearray(hdr)
        blob += g0[lay]
        blob += (bg2_g0 + b"\xff" * 1024)[:1024]
        blob += bytes(cellb1) + bytes(cellb2)
        if has_g1:
            blob += g1[lay]
            blob += (bg2_g1 + b"\xff" * 1024)[:1024]
        (MAPS_OUT / f"{mid}.ff4map").write_bytes(blob)
        done += 1
    return done


WORLD_NAMES = ("overworld", "underground", "moon")


def emit_worlds(obj):
    """One pack per world (docs/ARCHITECTURE.md step 1):
    <name>.ff4world = cells + composed->base + base art + palette.
    The art is the 8bpp expanded form (nibble pairs already resolved to
    palette indices, exactly as the rip's worldGraphics stores them),
    with the per-tile palette assignment baked in at render time like
    the exporter does ((gfx + assign) % 64).
    The pack reproduces data/world/<name>.png exactly (verified)."""
    import struct as _st

    out = ROOT / "data" / "world"
    out.mkdir(parents=True, exist_ok=True)
    import base64 as _b64

    def b64d(v):
        return _b64.b64decode(v if len(v) % 4 == 0 else v + "=" * (-len(v) % 4))

    done = 0
    for wi, nm in enumerate(WORLD_NAMES):
        gfx = b64d(obj["worldGraphics"][wi])
        pal = b64d(obj["worldPalettes"][wi])
        assign = b64d(obj["worldPaletteAssignments"][wi])
        cells = (out / f"{nm}.tiles").read_bytes()
        base = (out / f"{nm}.base").read_bytes()
        lavapal = b""
        if nm == "underground":
            lavapal = (out / "underground.lavapal").read_bytes()
        # the moon is 64x64 cells; over/underground 256x256
        w = h = 64 if nm == "moon" else 256
        # the world-water animation phases (TfrBGAnimGfx): the staged
        # MapAnimGfx block, 8 tiles x 4 phases x 64 B (byte-proven vs
        # emulator VRAM captures - tools/ff4_water_models.py --verify).
        # NOTE: all three worlds stage the SAME MapAnimGfx block 0.
        romf = open(ROOT / "ff2.smc", "rb").read()
        if len(romf) == 0x100200:
            romf = romf[0x200:]
        staged = bytearray()
        _x = 0
        while len(staged) < 0x800:
            for _i in range(16):
                staged.append(romf[0xFB9C0 + _x]); _x += 1
            for _i in range(8):
                staged.append(romf[0xFB9C0 + _x]); _x += 1
                staged.append(0)
        staged = bytes(staged)
        wateranim = bytearray()
        for ph in range(4):
            for i in range(4):
                j = ((ph - 1) % 4 + 4 * i) * 0x80
                wateranim += staged[j:j + 0x80]
        wateranim = bytes(wateranim)
        hdr = _st.pack("<4sHHHBB", b"FFW2", w, h, len(gfx) // 64,
                       len(pal) // 4, 1 if lavapal else 0)
        blob = bytearray(hdr)
        blob += cells
        blob += base[:512]
        blob += assign[:256]
        blob += gfx
        blob += pal
        blob += wateranim
        blob += lavapal
        (out / f"{nm}.ff4world").write_bytes(blob)
        done += 1
    return done


def main(argv):
    body = rom_body()
    obj = json.load(open(ROOT / "data" / "ff4-en-data.json")).get("obj", {})
    n_s = emit_sheets(body)
    n_a = emit_anim(body)
    n_t = emit_tileflags(body)
    n_p = emit_palettes(body)
    n_m = emit_maps(body)
    n_w = emit_worlds(obj)
    print(f"ff4data: {n_s} sheets, {n_a} anim blocks, {n_t} tileflag tables, "
          f"{n_p} palette lines, {n_m} map packs, {n_w} world packs "
          f"-> data/gfx + data/maps + data/world")
    if "--verify" in argv:
        rc = verify(body)
        rc2 = verify_worlds()
        return rc or rc2
    return 0


# --------------------------------------------------------------------------
# world pack self-test
# --------------------------------------------------------------------------

def verify_worlds():
    import struct
    from PIL import Image

    fails = 0
    for nm in WORLD_NAMES:
        pk = (ROOT / "data" / "world" / f"{nm}.ff4world").read_bytes()
        magic, w, h, nart, npal, lav = struct.unpack_from("<4sHHHBB", pk, 0)
        o = 12
        cells = pk[o:o + w * h]; o += w * h
        base = pk[o:o + 512]; o += 512
        assign = pk[o:o + 256]; o += 256
        gfx = pk[o:o + nart * 64]; o += nart * 64
        pal = pk[o:o + npal * 4]
        W, H = w * 16, h * 16
        img = Image.new("RGB", (W, H))
        px = img.load()
        for cy in range(h):
            for cx in range(w):
                c = cells[cy * w + cx]
                for k in range(4):
                    bt = base[c * 4 + k]
                    pb = assign[bt]
                    ox, oy = (k % 2) * 8, (k // 2) * 8
                    for py in range(8):
                        for pxi in range(8):
                            ci = (gfx[bt * 64 + py * 8 + pxi] + pb) % 64
                            co = ci * 4
                            px[cx * 16 + ox + pxi, cy * 16 + oy + py] = \
                                (pal[co], pal[co + 1], pal[co + 2])
        ref = Image.open(ROOT / f"data/world/{nm}.png").convert("RGB")
        bad = sum(1 for y in range(0, H, 4) for x in range(0, W, 4)
                  if px[x, y] != ref.getpixel((x, y)))
        print(f"world {nm}: {bad} sampled px differ vs the reference PNG")
        if bad:
            fails += 1
    if fails:
        print("world packs VERIFY: FAIL")
        return 1
    print("world packs VERIFY OK: reproduce the reference PNGs exactly")
    return 0


# --------------------------------------------------------------------------
# structural self-test: render from the emitted blobs and compare against the
# exporter's PNGs (the verification references)
# --------------------------------------------------------------------------

def verify(body):
    import base64
    from PIL import Image

    o = json.load(open(ROOT / "data" / "ff4-en-data.json")).get("obj", {})

    def b64d(v):
        return base64.b64decode(v if len(v) % 4 == 0 else v + "=" * (-len(v) % 4))

    lay_rom = json.load(open(ROOT / "data" / "layout_rom.json"))
    g1 = {int(k): b64d(v) for k, v in lay_rom["group1"].items()}
    sheets = {}
    for n in SHEETS:
        sheets[n] = (GFX_OUT / "sheets" / f"sheet{n}.idx").read_bytes()
    anims = {}
    for ts in range(1, 17):
        anims[ts] = (GFX_OUT / "anim" / f"ts{ts}.idx").read_bytes()
    flags = {}
    for ts in range(1, 17):
        flags[ts] = (GFX_OUT / "tileflags" / f"ts{ts}.bin").read_bytes()
    palblob = (GFX_OUT / "palettes.bin").read_bytes()

    def render(mid, group, phase=0):
        pk = (MAPS_OUT / f"{mid}.ff4map").read_bytes()
        magic, ts, sheet, pal, has_g1, has_bg2 = struct.unpack_from(
            "<4sHHHBB", pk, 0)
        base = 12
        cells = pk[base:base + 1024]
        bg2 = pk[base + 1024:base + 2048]
        if group == 1:
            off = base + 1024 + 1024 + 2048
            cells = pk[off:off + 1024]
            bg2 = pk[off + 1024:off + 2048]
        fl = flags[ts]
        sh = sheets[sheet]
        anim = anims[ts]
        pl = 0x100 * (pal & 0xFF)
        img = Image.new("RGB", (512, 512), (0, 0, 0))
        px = img.load()
        for cy in range(32):
            for cx in range(32):
                b = bg2[cy * 32 + cx]
                if b < 128:
                    draw_cell(px, cx, cy, b, fl, sh, anim, pl, phase, opaque=True)
        for cy in range(32):
            for cx in range(32):
                b = cells[cy * 32 + cx]
                if b < 128:
                    draw_cell(px, cx, cy, b, fl, sh, anim, pl, phase,
                              opaque=False)
        return img

    def draw_cell(px, cx, cy, b, fl, sh, anim, pl, phase, opaque):
        for j in range(4):
            lo = fl[b * 2 + j * 0x100]
            hi = fl[b * 2 + j * 0x100 + 1]
            sidx = lo + (hi & 3) * 0x100
            grp = (hi >> 2) & 7
            hf = not (hi & 0x40)
            vf = bool(hi & 0x80)
            if 0x120 <= sidx <= 0x12F:
                base = (phase * 16 + (sidx - 0x120)) * 64
                t = anim[base:base + 64]
            else:
                t = sh[sidx * 64:sidx * 64 + 64]
            ox, oy = (j % 2) * 8, (j // 2) * 8
            for y in range(8):
                for x in range(8):
                    sy = (7 - y if vf else y)
                    sx = (7 - x if hf else x)
                    c = t[sy * 8 + sx]
                    if c == 0 and not opaque:
                        continue
                    o = pl + (grp * 8 + (c & 7)) * 4
                    col = (palblob[o], palblob[o + 1], palblob[o + 2])
                    px[cx * 16 + ox + x, cy * 16 + oy + y] = col

    fails = 0
    for mid, group, tag in ((0, 0, "png"), (2, 0, "png"), (7, 0, "png"),
                            (8, 0, "png"), (36, 0, "png"), (162, 0, "png"),
                            (7, 1, "g1.png"), (36, 1, "g1.png")):
        ref = Image.open(ROOT / f"data/submaps/{mid}.{tag}").convert("RGB")
        got = render(mid, group)
        bad = sum(1 for y in range(512) for x in range(512)
                  if got.getpixel((x, y)) != ref.getpixel((x, y)))
        print(f"map {mid} group {group}: {bad} px differ vs the reference PNG")
        if bad:
            fails += 1
    for ph in range(4):
        ref = Image.open(ROOT / f"data/submaps/2.{'png' if ph == 0 else f'f{ph}.png'}").convert("RGB")
        got = render(2, 0, ph)
        bad = sum(1 for y in range(512) for x in range(512)
                  if got.getpixel((x, y)) != ref.getpixel((x, y)))
        print(f"map 2 phase {ph}: {bad} px differ vs the reference")
        if bad:
            fails += 1
    if fails:
        print(f"\nff4data VERIFY: {fails} FAILURE(S)")
        return 1
    print("\nff4data VERIFY OK: packs reproduce the reference PNGs exactly")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
