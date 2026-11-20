#!/usr/bin/env python3
"""
Decode the sub-map tilemap layouts straight from the ROM.

Ground truth (field/tilemap.asm):
  SubTilemapPtrs @17/8000 (file 0xB8000):
      words 0..255   = layout ids 0..255, GROUP 0, relative to 17/8000
      words 256..383= layout ids 0..127, GROUP 1, relative to 18/8000
  group = ($0fe5 & 1)  (mapProperties.layoutMSB)
          OR $1701 != 0 (the map was entered from the underground or moon)
  DecodeSubTilemap @feba: X = 2*(id+group) bytes into the table, i.e.
      group 0 -> word id, group 1 -> word 256+id.
  pointer word with bit 15 set -> data bank +1, offset &= 0x7FFF
  stream (ff4-map codec, data-codec.js + @ff52):
      byte < 0x80            -> one raw cell
      byte >= 0x80           -> value = byte & 0x7F,
                                count = next byte + 1
  output: 1024 cell values (32x32), row-major.

Why this tool exists: the rip's mapLayouts1/2 (ff4-en-data.json) are correct
where present but EMPTY for every layout whose data range crosses a ROM bank
boundary - 63 of 384, including map 27 (a live warp target).  This decoder
reproduces the rip byte-for-byte where the rip has data and fills the gaps.

Outputs:
  data/layout_rom.json   {"group0": {id: base64...}, "group1": {id: base64...}}
  exit 1 if any entry disagrees with the rip where the rip is non-empty

Usage: python3 tools/ff4_layout_rom.py [--write]
"""
import base64
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROM = ROOT / "ff2.smc"
JSON = ROOT / "data" / "ff4-en-data.json"
OUT = ROOT / "data" / "layout_rom.json"

PTR_OFF = 0xB8000        # 17/8000
WORDS = 384               # 256 + 128
CELLS = 1024


def b64e(b):
    return base64.b64encode(bytes(b)).decode()


def b64d(v):
    s = v if len(v) % 4 == 0 else v + "=" * (-len(v) % 4)
    return base64.b64decode(s)


def read_stream(body, word, base_bank):
    cur = word
    bank = base_bank
    if cur & 0x8000:
        bank += 1
        cur &= 0x7FFF
    nxt = word_next = 0
    return cur, bank


def decode_word(body, T, i, base_bank, cap=CELLS):
    if i + 1 >= WORDS:
        return bytearray()
    cur = T[i]
    bank = base_bank
    if cur & 0x8000:
        bank += 1
        cur &= 0x7FFF
    if i + 1 >= WORDS:
        # the last entry has no next pointer: its end is the segment end
        # (sub_tilemap spans 17/8000..18/FCF8, so rel 18/8000 the end is
        # 0x17CF9 - 0x8000 - 0x7FFF... = 0x7CF9 - see ff4-en.map)
        nxt = 0x7CF9
    else:
        nxt = T[i + 1]
        if nxt & 0x8000:
            nxt &= 0x7FFF
    length = nxt - cur
    if length <= 0:
        length += 0x8000
    out = bytearray()
    y = cur
    b = bank
    while len(out) < length:
        if y >= 0x8000:
            b += 1
            y -= 0x8000
        if b * 0x8000 + y >= len(body):
            break
        out.append(body[b * 0x8000 + y])
        y += 1
    cells = bytearray()
    k = 0
    while k < len(out) and len(cells) < cap:
        v = out[k]
        if v & 0x80:
            if k + 1 >= len(out):
                break
            cnt = out[k + 1] + 1
            val = v & 0x7F
            k += 2
            cells += bytes([val]) * min(cnt, cap - len(cells))
        else:
            cells.append(v)
            k += 1
    return cells


def main(write=False):
    body = open(ROM, "rb").read()
    if len(body) == 0x100200:
        body = body[0x200:]
    T = [struct.unpack_from("<H", body, PTR_OFF + i * 2)[0]
         for i in range(WORDS)]
    o = json.load(open(JSON)).get("obj", {})

    g0 = {}
    g1 = {}
    fails = []
    match = empty_filled = 0
    for i in range(256):
        cells = decode_word(body, T, i, 0x17)
        g0[i] = b64e(cells)
        rip = o["mapLayouts1"][i]
        if rip:
            if b64d(rip) == bytes(cells):
                match += 1
            else:
                fails.append(f"group0[{i}]: rip {len(b64d(rip))}B vs ROM "
                             f"{len(cells)}B")
        elif len(cells) == CELLS:
            empty_filled += 1
    for j in range(128):
        cells = decode_word(body, T, 256 + j, 0x18)
        g1[j] = b64e(cells)
        rip = o["mapLayouts2"][j]
        if rip:
            if b64d(rip) == bytes(cells):
                match += 1
            else:
                fails.append(f"group1[{j}]: rip vs ROM mismatch")
        elif len(cells) == CELLS:
            empty_filled += 1

    print(f"pointer words: {WORDS}; matched the rip on {match} non-empty "
          f"entries; filled {empty_filled} empty ones from the ROM")
    wrong_len = sum(1 for i in range(256)
                    if len(b64d(g0[i])) != CELLS) + \
                sum(1 for j in range(128) if len(b64d(g1[j])) != CELLS)
    print(f"entries with != {CELLS} cells: {wrong_len}")
    if fails:
        print("MISMATCHES vs the rip:")
        for f in fails[:10]:
            print("  -", f)
        return 1
    if write:
        OUT.write_text(json.dumps({"group0": g0, "group1": g1}) + "\n")
        print(f"wrote {OUT}")
    else:
        print("dry run (pass --write to write data/layout_rom.json)")
    return 0


if __name__ == "__main__":
    sys.exit(main("--write" in sys.argv))
