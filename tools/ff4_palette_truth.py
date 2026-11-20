#!/usr/bin/env python3
"""
FF4 sprite/NPC palette ground truth, derived strictly from the SNES code.

This is the single source of truth.  Everything here is traced to a specific
ASM routine and verified against the ROM bytes.

--------------------------------------------------------------------------
SOURCE OF TRUTH: the code, not derived JSON
--------------------------------------------------------------------------
field/player.asm  LoadPlayerPal @c827
    Copies MapSpritePal[0..] into $0DDB, 16 bytes of colour then 16 bytes of
    zero, until Y == $0080.  => 4 CGRAM sprite slots (0..3), 32 bytes each,
    one MapSpritePal entry per slot.
        slot 0 <- entry 0
        slot 1 <- entry 1
        slot 2 <- entry 2
        slot 3 <- entry 3

field/npc.asm     LoadNPCPal   @c2bc
        sta  $3e / stz $3d            ; 16-bit value = 0x00:A
        lsr $3e / ror $3d   (x3)      ; >> 3  => X = (A & 7) * 32
        lda f:MapSpritePal+13*16,x    ; base = 208 + (A&7)*32
        ... copy 16 bytes, zero 16, repeat while Y & $3F != 0
    => 2 entries per call, base entry = 13 + 2*(A & 7)
    Called twice from InitNPCs:
        A = $0fe1 & $0F , Y = $0000   (map npcPalette1)
        A = $0fe1 >> 4  , Y = $0040   (map npcPalette2)
    $0E5B == $0DDB + $80  =>  slots 4,5 (first call) and 6,7 (second call).

field/npc.asm     InitNPCs     @c198
        lda $06 / cmp #$0e / bcs -> NPC path
        character:  lda f:PlayerPalTbl,x        (0..3)  -> CGRAM slot 0..3
        NPC:        lda f:NPCProp+3,x
                    and #$0c / lsr2 / clc / adc #$04   (4..7) -> slot 4..7
    stored in $090d,y

field/npc.asm     DrawNPCs     @bc4d
        lda $090d,x / asl / sta $07        ; OAM attr palette bits 1-3
        ... (attr & $f1) + $07

field/sprite.asm  PlayerPalTbl @b2fa
        .byte 0,0,1,2,2,2,0,1,1,3,0,1,0,0,0,0,0,0

--------------------------------------------------------------------------
ROM tables (located by byte-pattern search, see --verify)
--------------------------------------------------------------------------
MapSpritePal  file 0x68000   36 entries x 16 bytes  8 colours BGR555
              identical to data/gfx/pal/map_sprite_pal.bin
MapProp       file 0xA9C84   13 bytes/map, byte 6 = npcPalette1 | npcPalette2<<4
NPCPropPtrs   file 0x98000   384 x u16, offset from NPCProp base
NPCProp       file 0x98300   4 bytes/NPC, 0x00 gfx id terminates the list
NPCGfxID      file 0x97000   512 bytes, gfx id -> map sprite sheet
mapSpriteGfx  file 0xD8000   bank $1B / $8000; sheet<0x11 -> +sheet*0x300
Everything checkable with --verify.
"""

import json
import os
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROM_PATH = ROOT / "ff2.smc" if (ROOT / "ff2.smc").exists() else ROOT / "ff2-debug.smc"

MAP_SPRITE_PAL = 0x68000
MAP_PROP = 0xA9C84
NPC_PROP_PTRS = 0x98000
NPC_PROP = 0x98300
NPC_GFX_ID = 0x97000
PLAYER_PAL_TBL = 0xAB2FA
MAP_SPRITE_GFX = 0xD8000

NPAL = 36          # MapSpritePal entries
PAL_ENTRY = 16     # bytes per entry (8 colours)
CGRAMSZ = 32       # bytes per CGRAM palette slot (16 colours)


# --------------------------------------------------------------------------
# ROM
# --------------------------------------------------------------------------
def load_rom(path=None):
    p = Path(path) if path else ROM_PATH
    data = p.read_bytes()
    if len(data) % 0x400 == 0x200:      # strip copier header if present
        data = data[0x200:]
    return data


def bgr555_to_rgb(w):
    """SNES 5-bit-per-channel -> 8 bit, replicating the low bits (as the
    hardware's DAC does and as ff4_export_submaps.color16 does)."""
    return (((w & 0x1F) << 3) | ((w >> 2) & 7),
            (((w >> 5) & 0x1F) << 3) | ((w >> 7) & 7),
            (((w >> 10) & 0x1F) << 3) | ((w >> 12) & 7))


class Rom:
    def __init__(self, path=None):
        self.data = load_rom(path)

    # -- tables --------------------------------------------------------------
    def map_sprite_pal_entry(self, entry):
        """8 RGB colours for MapSpritePal[entry] (16 bytes, BGR555)."""
        off = MAP_SPRITE_PAL + (entry % NPAL) * PAL_ENTRY
        out = []
        for c in range(8):
            w = self.data[off + c * 2] | (self.data[off + c * 2 + 1] << 8)
            out.append(bgr555_to_rgb(w))
        return out

    def map_sprite_pal_raw(self, entry):
        off = MAP_SPRITE_PAL + (entry % NPAL) * PAL_ENTRY
        return self.data[off:off + PAL_ENTRY]

    def player_pal_tbl(self):
        return list(self.data[PLAYER_PAL_TBL:PLAYER_PAL_TBL + 18])

    def map_prop(self, map_id):
        b = self.data[MAP_PROP + map_id * 13: MAP_PROP + map_id * 13 + 13]
        return {
            "raw": b.hex(),
            "npc_palette1": b[6] & 0x0F,
            "npc_palette2": (b[6] >> 4) & 0x0F,
        }

    def npc_list(self, map_id):
        """Decoded NPCProp records for a map (InitNPCs layout)."""
        ptr = struct.unpack_from("<H", self.data, NPC_PROP_PTRS + map_id * 2)[0]
        base = NPC_PROP + ptr
        out = []
        i = 0
        while i < 4 * 64:                                  # safety bound
            b = self.data[base + i * 4: base + i * 4 + 4]
            if len(b) < 4 or b[0] == 0x00:                 # 0 gfx id terminates
                break
            b0, b1, b2, b3 = b
            out.append({
                "gfx_id": b0,
                "x": b1 & 0x3F,
                "y": b2,
                "facing": b3 & 0x03,
                "pal_sel": (b3 & 0x0C) >> 2,   # 0..3 -> CGRAM slot 4..7
                "speed": (b3 >> 6) & 0x03,
                "face_player": (b3 >> 4) & 0x01,
                "moving": (b3 >> 5) & 0x01,
                "byte1_hi": (b1 >> 7) & 1,     # bit7: don't reset direction
            })
            i += 1
        return out

    def npc_gfx_id(self, gfx_id):
        return self.data[NPC_GFX_ID + gfx_id]


# --------------------------------------------------------------------------
# The ASM routines, reimplemented
# --------------------------------------------------------------------------
def load_npc_pal(entry_index):
    """LoadNPCPal @c2bc.  Returns the two MapSpritePal entry numbers it copies.

    X = (A & 7) * 32 ; base byte offset = 13*16 + X ; copy 32 bytes = 2 entries.
    """
    x = (entry_index & 7) * 32
    base = 13 * 16 + x
    return base // 16, base // 16 + 1


def load_player_pal():
    """LoadPlayerPal @c827.  4 entries, straight from entry 0."""
    return [0, 1, 2, 3]


def cgram_slots(rom, map_id):
    """The 8 sprite CGRAM slots a map ends up with, with provenance.

    slot 0..3 : MapSpritePal 0..3                     (characters)
    slot 4,5  : 13+2*npcPalette1, +1                   (NPC group 1)
    slot 6,7  : 13+2*npcPalette2, +1                   (NPC group 2)
    """
    mp = rom.map_prop(map_id)
    p = load_player_pal()
    a0, a1 = load_npc_pal(mp["npc_palette1"])
    b0, b1 = load_npc_pal(mp["npc_palette2"])
    entries = [p[0], p[1], p[2], p[3], a0, a1, b0, b1]
    return entries, mp


def slot_for_character(rom, char_id):
    """Character CGRAM slot = PlayerPalTbl[char_id] (0..3)."""
    tbl = rom.player_pal_tbl()
    return tbl[char_id] if char_id < len(tbl) else 0


def slot_for_npc(npc):
    """NPC CGRAM slot = ((byte3 & 0x0C) >> 2) + 4."""
    return npc["pal_sel"] + 4


def palette_for_slot(rom, entries, slot):
    return rom.map_sprite_pal_entry(entries[slot])


# --------------------------------------------------------------------------
# Sprite sheet access (rip's decoded 1 byte/pixel form)
# --------------------------------------------------------------------------
GFX_DIR = ROOT / "data" / "gfx" / "gfx"

# field/field_data.asm NPCSpriteTbl @f846 -- 21 frames x 4 OAM sprites
# (x, y, tile, attr); attr bit 6 ($40) == horizontal flip.  Frame stride 16.
NPC_SPRITE_TBL = [
    [(0x00, 0x01, 0x04, 0x2C), (0x00, 0x09, 0x06, 0x2C), (0x08, 0x01, 0x05, 0x2C), (0x08, 0x09, 0x07, 0x2C)],
    [(0x00, 0x01, 0x04, 0x2C), (0x00, 0x09, 0x07, 0x6C), (0x08, 0x01, 0x05, 0x2C), (0x08, 0x09, 0x06, 0x6C)],
    [(0x00, 0x01, 0x09, 0x6C), (0x00, 0x09, 0x0B, 0x6C), (0x08, 0x01, 0x08, 0x6C), (0x08, 0x09, 0x0A, 0x6C)],
    [(0x00, 0x00, 0x0D, 0x6C), (0x00, 0x08, 0x0F, 0x6C), (0x08, 0x00, 0x0C, 0x6C), (0x08, 0x08, 0x0E, 0x6C)],
    [(0x00, 0x01, 0x00, 0x2C), (0x00, 0x09, 0x02, 0x2C), (0x08, 0x01, 0x01, 0x2C), (0x08, 0x09, 0x03, 0x2C)],
    [(0x00, 0x01, 0x00, 0x2C), (0x00, 0x09, 0x03, 0x6C), (0x08, 0x01, 0x01, 0x2C), (0x08, 0x09, 0x02, 0x6C)],
    [(0x00, 0x01, 0x08, 0x2C), (0x00, 0x09, 0x0A, 0x2C), (0x08, 0x01, 0x09, 0x2C), (0x08, 0x09, 0x0B, 0x2C)],
    [(0x00, 0x00, 0x0C, 0x2C), (0x00, 0x08, 0x0E, 0x2C), (0x08, 0x00, 0x0D, 0x2C), (0x08, 0x08, 0x0F, 0x2C)],
    [(0x00, 0x00, 0x14, 0x2C), (0x08, 0x00, 0x15, 0x2C), (0x00, 0x08, 0x16, 0x2C), (0x08, 0x08, 0x17, 0x2C)],
    [(0x00, 0x00, 0x18, 0x2C), (0x08, 0x00, 0x19, 0x2C), (0x00, 0x08, 0x1A, 0x2C), (0x08, 0x08, 0x1B, 0x2C)],
    [(0x00, 0x00, 0x18, 0x2C), (0x08, 0x00, 0x19, 0x2C), (0x00, 0x08, 0x1A, 0x2C), (0x08, 0x08, 0x1B, 0x2C)],
    [(0x00, 0x00, 0x18, 0x2C), (0x08, 0x00, 0x19, 0x2C), (0x00, 0x08, 0x1A, 0x2C), (0x08, 0x08, 0x1B, 0x2C)],
    [(0x00, 0x00, 0x10, 0x2C), (0x08, 0x00, 0x11, 0x2C), (0x00, 0x08, 0x12, 0x2C), (0x08, 0x08, 0x13, 0x2C)],
    [(0x00, 0x00, 0x10, 0x2C), (0x08, 0x00, 0x11, 0x2C), (0x00, 0x08, 0x12, 0x2C), (0x08, 0x08, 0x13, 0x2C)],
    [(0x00, 0x00, 0x1C, 0x2C), (0x08, 0x00, 0x1D, 0x2C), (0x00, 0x08, 0x1E, 0x2C), (0x08, 0x08, 0x1F, 0x2C)],
    [(0x00, 0x00, 0x1C, 0x2C), (0x08, 0x00, 0x1D, 0x2C), (0x00, 0x08, 0x1E, 0x2C), (0x08, 0x08, 0x1F, 0x2C)],
    [(0x00, 0x01, 0x00, 0x2C), (0x00, 0x09, 0x02, 0x2C), (0x08, 0x01, 0x01, 0x2C), (0x08, 0x09, 0x03, 0x2C)],
    [(0x00, 0x01, 0x04, 0x2C), (0x00, 0x09, 0x06, 0x2C), (0x08, 0x01, 0x05, 0x2C), (0x08, 0x09, 0x07, 0x2C)],
    [(0x00, 0x01, 0x08, 0x2C), (0x00, 0x09, 0x0A, 0x2C), (0x08, 0x01, 0x09, 0x2C), (0x08, 0x09, 0x0B, 0x2C)],
    [(0x00, 0x01, 0x0C, 0x2C), (0x00, 0x09, 0x0E, 0x2C), (0x08, 0x01, 0x0D, 0x2C), (0x08, 0x09, 0x0F, 0x2C)],
    [(0x00, 0x01, 0x01, 0x6C), (0x00, 0x09, 0x03, 0x6C), (0x08, 0x01, 0x00, 0x6C), (0x08, 0x09, 0x02, 0x6C)],
]


# field/sprite.asm PlayerSpriteTiles @c0c4 -- 8 frames x 4 OAM sprites
# (tile, attr); attr bit 6 ($40) == horizontal flip.
PLAYER_SPRITE_TILES = [
    [(0x04, 0x20), (0x05, 0x20), (0x06, 0x20), (0x07, 0x20)],   # 0 up    a
    [(0x04, 0x20), (0x05, 0x20), (0x07, 0x60), (0x06, 0x60)],   # 1 up    b
    [(0x09, 0x60), (0x08, 0x60), (0x0B, 0x60), (0x0A, 0x60)],   # 2 right a
    [(0x0D, 0x60), (0x0C, 0x60), (0x0F, 0x60), (0x0E, 0x60)],   # 3 right b
    [(0x00, 0x20), (0x01, 0x20), (0x02, 0x20), (0x03, 0x20)],   # 4 down  a
    [(0x00, 0x20), (0x01, 0x20), (0x03, 0x60), (0x02, 0x60)],   # 5 down  b
    [(0x08, 0x20), (0x09, 0x20), (0x0A, 0x20), (0x0B, 0x20)],   # 6 left  a
    [(0x0C, 0x20), (0x0D, 0x20), (0x0E, 0x20), (0x0F, 0x20)],   # 7 left  b
]

_sheet_cache = {}


def sprite_sheet(sheet):
    if sheet in _sheet_cache:
        return _sheet_cache[sheet]
    p = GFX_DIR / f"mapSpriteGraphics[{sheet}].bin"
    if not p.exists():
        _sheet_cache[sheet] = b""
        return b""
    d = p.read_bytes()
    _sheet_cache[sheet] = d
    return d


def render_sprite_common(sheet, frame, colors, table):
    """Render one 16x16 frame using an OAM table (player or NPC)."""
    data = sprite_sheet(sheet)
    img = [[(0, 0, 0, 0)] * 16 for _ in range(16)]
    if not data:
        return img
    if frame < 0 or frame >= len(table):
        frame = 0
    for idx in range(4):
        entry = table[frame][idx]
        if len(entry) == 4:
            ox, oy, tile, attr = entry
        else:                              # player table: (tile, attr)
            tile, attr = entry
            ox, oy = (idx % 2) * 8, (idx // 2) * 8
        flip = bool(attr & 0x40)
        for py in range(8):
            for px in range(8):
                o = tile * 64 + py * 8 + px
                if o >= len(data):
                    continue
                v = data[o]
                if v == 0:
                    continue
                sxc = (7 - px) if flip else px
                dx, dy = ox + sxc, oy + py
                if 0 <= dx < 16 and 0 <= dy < 16:
                    img[dy][dx] = colors[v & 7] + (255,)
    return img


def render_sprite(sheet, frame, colors):
    """Player/actor frame."""
    return render_sprite_common(sheet, frame, colors, PLAYER_SPRITE_TILES)


def render_npc_sprite(sheet, frame, colors):
    """NPC frame (NPCSpriteTbl)."""
    return render_sprite_common(sheet, frame, colors, NPC_SPRITE_TBL)


# --------------------------------------------------------------------------
# Verification: prove the offsets really are the tables
# --------------------------------------------------------------------------
def verify(rom):
    ok = True
    print("== verifying table addresses against ROM bytes ==")

    def chk(name, got, want):
        nonlocal ok
        good = got == want
        ok = ok and good
        print(f"  [{'OK ' if good else 'BAD'}] {name}")
        return good

    chk("MapSpritePal[0] raw == 000000005d2296017f014c7e747f8c69",
        rom.map_sprite_pal_raw(0).hex(), "000000005d2296017f014c7e747f8c69")

    # map_sprite_pal.bin must be byte-identical to ROM
    binp = ROOT / "data" / "gfx" / "pal" / "map_sprite_pal.bin"
    if binp.exists():
        same = binp.read_bytes() == rom.data[MAP_SPRITE_PAL:MAP_SPRITE_PAL + 0x240]
        chk("data/gfx/pal/map_sprite_pal.bin == ROM 0x68000", same, True)

    mp0 = rom.map_prop(0)
    chk("MapProp[0] npcPalette1/2 == 0/1", (mp0["npc_palette1"], mp0["npc_palette2"]), (0, 1))
    mp1 = rom.map_prop(1)
    chk("MapProp[1] npcPalette1/2 == 0/9", (mp1["npc_palette1"], mp1["npc_palette2"]), (0, 9))
    mp2 = rom.map_prop(2)
    chk("MapProp[2] npcPalette1/2 == 0/5", (mp2["npc_palette1"], mp2["npc_palette2"]), (0, 5))

    chk("PlayerPalTbl == 0,0,1,2,2,2,0,1,1,3,0,1,0,0,0,0,0,0",
        rom.player_pal_tbl(), [0, 0, 1, 2, 2, 2, 0, 1, 1, 3, 0, 1, 0, 0, 0, 0, 0, 0])

    # NPCGfxID first bytes from npc_gfx_id_en.asm
    chk("NPCGfxID[0..7] == 00 65 65 18 18 56 1d 1d",
        rom.data[NPC_GFX_ID:NPC_GFX_ID + 8].hex(), "0065651818561d1d")

    # NPCProp[0] gfx id 0x2d at file 0x98300 (npc_prop_en.asm NPCProp_0000)
    chk("NPCProp[0].gfx_id == 0x2d", rom.npc_list(0)[0]["gfx_id"], 0x2D)

    # LoadNPCPal addressing
    chk("LoadNPCPal(0) -> entries 13,14", load_npc_pal(0), (13, 14))
    chk("LoadNPCPal(1) -> entries 15,16", load_npc_pal(1), (15, 16))
    chk("LoadNPCPal(5) -> entries 23,24", load_npc_pal(5), (23, 24))
    chk("LoadNPCPal(15) aliases to (A&7)=7 -> entries 27,28", load_npc_pal(15), (27, 28))
    chk("LoadNPCPal(9) aliases to (A&7)=1 -> entries 15,16", load_npc_pal(9), (15, 16))

    print("  => all checks passed" if ok else "  => FAILURES above")
    return ok


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------
def hexcolor(c):
    return "%02X%02X%02X" % c[:3]


def report_map(rom, map_id):
    entries, mp = cgram_slots(rom, map_id)
    npcs = rom.npc_list(map_id)
    print(f"\n=== map {map_id}: npcPalette1={mp['npc_palette1']} "
          f"npcPalette2={mp['npc_palette2']}  ({len(npcs)} npcs) ===")
    print("  CGRAM sprite slots:")
    for s in range(8):
        cols = rom.map_sprite_pal_entry(entries[s])
        kind = "char" if s < 4 else "npc"
        print(f"    slot {s} ({kind}) <- MapSpritePal[{entries[s]:2d}]  "
              + " ".join(hexcolor(c) for c in cols))
    print("  npcs:")
    for i, n in enumerate(npcs):
        slot = slot_for_npc(n)
        sheet = rom.npc_gfx_id(n["gfx_id"])
        print(f"    #{i:2d} ({n['x']:2d},{n['y']:2d}) gfx={n['gfx_id']:3d} "
              f"sheet={sheet:3d} pal_sel={n['pal_sel']} -> slot {slot} "
              f"= entry {entries[slot]:2d}")
    return entries, npcs


def export_triggers(outdir=None):
    """data/submaps/<id>.trig (group 0) and <id>.trig1 (group 1).

    Source: field/trigger.asm.  The table is indexed by `map*2` and a second
    block of 128 pointers is selected when the map was entered from the
    underground or the moon:

        @9c3c  lda $1702 / asl / lda $1701 / beq / inc $3e (-> +512 bytes)
        MapTriggersPtrs:  256 pointers (maps 0..255)   -> mapTriggers1
                          then 128 pointers (maps 0..127) -> mapTriggers2

    So mapTriggers2 is NOT maps 256..383: it is the second trigger set of maps
    0..127, used when `$1701` is set (which CheckTriggerWorld sets to 1 for
    entries in the underground/moon).  97 maps have one, and some maps (7, 27,
    ...) have *only* group 1 - which is why warps were missing.

    Record layout (5 bytes, verified against the ROM at 0xA8000):
        x, y, map, xDest/dir, yDest
        map == 0xFE  -> treasure chest record (item in the 5th byte)
        map == 0xFF  -> trigger script, id in the 4th byte
        map >= 0xFB  -> world warp, world = map-0xFB, dest = (xDest, yDest)
        else         -> submap warp, dest = (xDest & 0x3F, yDest),
                        arrival facing = xDest >> 6
    """
    outdir = Path(outdir) if outdir else (ROOT / "data" / "submaps")
    jp = ROOT / "data" / "ff4-en-data.json"
    if not jp.exists():
        return 0
    o = json.load(open(jp)).get("obj", {})
    t1 = o.get("mapTriggers1") or []
    t2 = o.get("mapTriggers2") or []
    done = 0

    def write(map_id, lst, suffix):
        lines = []
        for e in lst:
            lines.append("%d %d %d %d %d %d %d\n" % (
                e.get("x", 0) & 0xFF, e.get("y", 0) & 0xFF,
                e.get("map", 0) & 0xFF, e.get("xDest", 0) & 0xFF,
                e.get("yDest", 0) & 0xFF, e.get("direction", 0) & 3,
                e.get("event", 0) & 0xFF))
        (outdir / f"{map_id}{suffix}").write_text("".join(lines))

    # group 0: maps 0..255
    for map_id in range(256):
        if map_id >= len(t1):
            break
        if not (outdir / f"{map_id}.png").exists():
            continue
        write(map_id, t1[map_id], ".trig")
        (outdir / f"{map_id}.trig1").unlink(missing_ok=True)
        done += 1
    # group 1: maps 0..127
    for map_id in range(128):
        if map_id >= len(t2):
            break
        if not (outdir / f"{map_id}.png").exists():
            continue
        if t2[map_id]:
            write(map_id, t2[map_id], ".trig1")
    # stale files from the old (wrong) 256..383 mapping
    for stray in list(outdir.glob("*.trig")) + list(outdir.glob("*.trig1")):
        try:
            m = int(stray.name.split(".")[0])
        except ValueError:
            continue
        if m >= 256:
            stray.unlink()
    return done


def export_slot_table(rom, path=None):
    """Write data/map_sprite_slots.bin: 384 maps x 8 bytes, the MapSpritePal
    entry number loaded into CGRAM sprite slot 0..7 for that map.

    slot 0..3 = entries 0..3            (LoadPlayerPal)
    slot 4..5 = 13+2*npcPalette1 (+1)   (LoadNPCPal, Y=0)
    slot 6..7 = 13+2*npcPalette2 (+1)   (LoadNPCPal, Y=$40)
    """
    path = Path(path) if path else (ROOT / "data" / "map_sprite_slots.bin")
    buf = bytearray()
    for map_id in range(384):
        entries, _ = cgram_slots(rom, map_id)
        buf += bytes(entries)
    path.write_bytes(buf)
    return path, len(buf)


def export_engine_palettes(rom, outdir=None):
    """Write data/gfx/gfx/npcPalettes[n].bin as 32-byte RGBA (8 colours).

    world_view.c sprite_load() reads exactly this:
        spr_rgba[i] = 0xFF000000 | pal[i*4]<<16 | pal[i*4+1]<<8 | pal[i*4+2]
    """
    outdir = Path(outdir) if outdir else GFX_DIR
    outdir.mkdir(parents=True, exist_ok=True)
    n = 0
    for e in range(NPAL):
        cols = rom.map_sprite_pal_entry(e)
        buf = bytearray()
        for i, (r, g, b) in enumerate(cols):
            buf += bytes((r, g, b, 0 if i == 0 else 255))
        (outdir / f"npcPalettes[{e}].bin").write_bytes(buf)
        n += 1
    return n


def export_npc_files(rom, outdir=None, maps=None):
    """.npc line: x y dir palette sheet random speed anim <TAB> text

    palette is the ABSOLUTE MapSpritePal entry (what world_view loads).
    """
    outdir = Path(outdir) if outdir else ROOT / "data" / "submaps"
    dialogs = None
    jp = ROOT / "data" / "ff4-en-data.json"
    if jp.exists():
        try:
            obj = json.load(open(jp)).get("obj", {})
            dialogs = obj.get("mapDialog")
            npc_names = None
        except Exception:
            dialogs = None

    done = 0
    for map_id in range(384):
        if maps and map_id not in maps:
            continue
        entries, mp = cgram_slots(rom, map_id)
        npcs = rom.npc_list(map_id)
        png = outdir / f"{map_id}.png"
        if not png.exists():
            continue
        dlg = []
        if dialogs and map_id < len(dialogs):
            dlg = dialogs[map_id] or []
        lines = []
        for i, n in enumerate(npcs):
            slot = slot_for_npc(n)
            sheet = rom.npc_gfx_id(n["gfx_id"])
            text = dlg[i] if i < len(dlg) else ""
            text = (text or "").replace("\\n", " ").replace("\\t", " ").replace("\n", " ")
            # direction: ($0909) facing; anim flag: keep 1 for animated NPCs
            lines.append("%d %d %d %d %d %d %d %d\t%s\n" % (
                n["x"], n["y"], n["facing"], entries[slot], sheet,
                1 if n["byte1_hi"] else 0, n["speed"], 1, text))
        (outdir / f"{map_id}.npc").write_text("".join(lines))
        done += 1
    return done


def contact_sheet(rom, map_id, path):
    """Render every NPC of a map (down pose) plus the actor, on a labelled grid."""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print("PIL not available; skipping contact sheet")
        return None

    entries, mp = cgram_slots(rom, map_id)
    npcs = rom.npc_list(map_id)
    cols = 8
    rows = max(1, (len(npcs) + 1 + cols - 1) // cols)
    cw, ch = 16 * 4, 16 * 4 + 10
    img = Image.new("RGB", (cols * cw, rows * ch), (24, 24, 32))
    d = ImageDraw.Draw(img)

    def paste(frame_img, cx, cy, scale=4):
        for y in range(16):
            for x in range(16):
                p = frame_img[y][x]
                if p[3] == 0:
                    continue
                for dy in range(scale):
                    for dx in range(scale):
                        img.putpixel((cx + x * scale + dx, cy + y * scale + dy), p[:3])

    # actor with char 0 (Cecil) and its character slot
    actor_slot = slot_for_character(rom, 0)
    actor_cols = palette_for_slot(rom, entries, actor_slot)
    paste(render_sprite(0, 4, actor_cols), 0, 0)
    d.text((2, 16 * 4), f"actor c0 s{actor_slot}", fill=(255, 255, 128))

    for i, n in enumerate(npcs):
        cx = ((i + 1) % cols) * cw
        cy = ((i + 1) // cols) * ch
        slot = slot_for_npc(n)
        pcols = palette_for_slot(rom, entries, slot)
        sheet = rom.npc_gfx_id(n["gfx_id"])
        paste(render_npc_sprite(sheet, (n["facing"] & 3) * 2, pcols), cx, cy)
        d.text((cx + 2, cy + 16 * 4), f"gfx{n['gfx_id']} sh{sheet} e{entries[slot]}",
               fill=(200, 200, 160))
    img.save(path)
    return path


def main(argv):
    rom = Rom()
    if "--verify" in argv:
        return 0 if verify(rom) else 1

    if "--export-palettes" in argv:
        n = export_engine_palettes(rom)
        print(f"wrote {n} npcPalettes[n].bin (32-byte RGBA)")

    if "--export-slots" in argv:
        p, n = export_slot_table(rom)
        print(f"wrote {p} ({n} bytes)")

    if "--export-trig" in argv:
        n = export_triggers()
        print(f"wrote {n} .trig files")

    if "--export-npc" in argv:
        n = export_npc_files(rom)
        print(f"wrote {n} .npc files")

    if "--sheet" in argv:
        mid = int(argv[argv.index("--sheet") + 1])
        out = f"/tmp/map{mid}_npcs.png"
        contact_sheet(rom, mid, out)
        print("wrote", out)
        report_map(rom, mid)
        return 0

    maps = [int(x) for x in argv[argv.index("--maps") + 1].split(",")] \
        if "--maps" in argv else [0, 1, 2]
    for m in maps:
        report_map(rom, m)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
