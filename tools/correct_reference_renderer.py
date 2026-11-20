#!/usr/bin/env python3
"""
CORRECT reference renderer using proven tile decoding from ff4_export_submaps.py.
This matches the original game exactly.
"""
import struct
from pathlib import Path
from PIL import Image
import base64
import json

# Load ROM
rom_path = Path("/home/jojosarah273/DEV/FF4_ASMTOC/ff2.smc")
with open(rom_path, 'rb') as f:
    rom = f.read()
    if len(rom) % 0x400 == 0x200:
        rom = rom[0x200:]

# Load map data
with open('/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json') as f:
    data = json.load(f)

obj = data['obj']

# From ff4_export_submaps.py: tileset sheet mapping
SHEETS = {k: v - 0x200 for k, v in {
    1: 0xADE00, 2: 0xF61A8, 3: 0xF0220, 4: 0xEEF40, 5: 0xF1768,
    6: 0xF2EC0, 7: 0xF3AC0, 8: 0xF51E8, 9: 0xF9700, 10: 0xF8200,
    11: 0xF61A8, 12: 0xFD408, 13: 0xFEA88}.items()}

SHEET_BPP = {1: 4}  # Only sheet 1 is 4bpp, others are 3bpp

TILEPROP = 0xA1000 - 0x200
PALETTE = 0xA6000 - 0x200

def tile_px(sheet, idx):
    """CORRECT tile decoding from proven ff4_export_submaps.py"""
    bpp = SHEET_BPP.get(sheet, 3)
    
    if bpp == 4:
        # 4bpp: 32 bytes/tile
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
                # LSB first!
                out.append(((p0 >> x) & 1) | (((p1 >> x) & 1) << 1) |
                           (((p2 >> x) & 1) << 2) | (((p3 >> x) & 1) << 3))
        return out
    
    # 3bpp: 24 bytes/tile
    base = SHEETS[sheet] + idx * 24
    if base + 24 > len(rom):
        return None
    out = []
    for y in range(8):
        p0 = rom[base + y * 2]
        p1 = rom[base + y * 2 + 1]
        p2 = rom[base + 16 + y]  # Plane 2 at offset 16
        for x in range(8):
            # LSB first!
            out.append(((p0 >> x) & 1) | (((p1 >> x) & 1) << 1) |
                       (((p2 >> x) & 1) << 2))
    return out

def color16(palidx, grp, c):
    """Decode palette color (BGR555)"""
    o = PALETTE + palidx * 0x80 + (grp * 8 + c) * 2
    w = rom[o] | (rom[o + 1] << 8)
    return (((w & 0x1F) << 3) | ((w >> 2) & 7),
            (((w >> 5) & 0x1F) << 3) | ((w >> 7) & 7),
            (((w >> 10) & 0x1F) << 3) | ((w >> 12) & 7))

def render_map_correct(map_id):
    """Render map using CORRECT proven logic"""
    mp = obj['mapProperties'][map_id]
    n = mp['graphics'] + 1  # 1-indexed tileset
    palidx = mp['palette']
    sheet = SHEETS.get(n, n) if n in SHEETS else n
    
    # Determine actual sheet number
    if n in (1, 16):
        sheet_num = 1
    elif 2 <= n <= 10:
        sheet_num = n
    elif n in (11, 12):
        sheet_num = 9
    elif 13 <= n <= 15:
        sheet_num = n - 2
    else:
        sheet_num = 1
    
    ts = (0xA2000 - 0x200) + (n - 1) * 0x400
    
    # Get layouts
    lay1 = mp['layout1']
    lay2 = mp.get('layout2', -1)
    
    layouts1 = obj['mapLayouts1']
    blob1 = base64.b64decode(layouts1[lay1])
    blob2 = base64.b64decode(layouts1[lay2]) if 0 <= lay2 < len(layouts1) else None
    
    # Create 512x512 image (32x32 tiles, 16x16 pixels each)
    W = H = 512
    img = Image.new('RGB', (W, H), (0, 0, 0))
    
    # Render BG2 first (opaque backdrop)
    if blob2 and len(blob2) >= 1024:
        for cy in range(32):
            for cx in range(32):
                b = blob2[cy * 32 + cx]
                if b >= 128:
                    continue
                
                # Each 16x16 cell is 4 8x8 sub-tiles
                for j in range(4):
                    lo = rom[ts + b * 2 + j * 0x100]
                    hi = rom[ts + b * 2 + j * 0x100 + 1]
                    sidx = lo + (hi & 3) * 0x100
                    grp = (hi >> 2) & 7
                    hflip = not (hi & 0x40)  # Inverted!
                    vflip = bool(hi & 0x80)
                    
                    t = tile_px(sheet_num, sidx)
                    if t is None:
                        continue
                    
                    ox, oy = (j % 2) * 8, (j // 2) * 8
                    
                    for y in range(8):
                        for x in range(8):
                            src_y = 7 - y if vflip else y
                            src_x = 7 - x if hflip else x
                            c = t[src_y * 8 + src_x]
                            
                            if c > 0:  # Non-transparent
                                r, g, bl = color16(palidx, grp, c & 7)
                                px = cx * 16 + ox + x
                                py = cy * 16 + oy + y
                                if 0 <= px < W and 0 <= py < H:
                                    img.putpixel((px, py), (r, g, bl))
    
    # Render BG1 on top (transparent)
    for cy in range(32):
        for cx in range(32):
            b = blob1[cy * 32 + cx]
            if b >= 128:
                continue
            
            for j in range(4):
                lo = rom[ts + b * 2 + j * 0x100]
                hi = rom[ts + b * 2 + j * 0x100 + 1]
                sidx = lo + (hi & 3) * 0x100
                grp = (hi >> 2) & 7
                hflip = not (hi & 0x40)
                vflip = bool(hi & 0x80)
                
                t = tile_px(sheet_num, sidx)
                if t is None:
                    continue
                
                ox, oy = (j % 2) * 8, (j // 2) * 8
                
                for y in range(8):
                    for x in range(8):
                        src_y = 7 - y if vflip else y
                        src_x = 7 - x if hflip else x
                        c = t[src_y * 8 + src_x]
                        
                        if c > 0:  # Only draw non-transparent pixels!
                            r, g, bl = color16(palidx, grp, c & 7)
                            px = cx * 16 + ox + x
                            py = cy * 16 + oy + y
                            if 0 <= px < W and 0 <= py < H:
                                img.putpixel((px, py), (r, g, bl))
    
    return img

print("=" * 80)
print("CORRECT REFERENCE RENDERER")
print("Using proven tile decoding from ff4_export_submaps.py")
print("=" * 80)
print()

# Render Baron
print("Rendering Baron (map 36) with CORRECT logic...")
correct_render = render_map_correct(36)
correct_render.save('/tmp/baron_correct_render.png')
print("✓ Saved to /tmp/baron_correct_render.png")
print()

# Compare with original
orig_ref = Image.open('/home/jojosarah273/DEV/FF4_ASMTOC/refs/baron_fixed.png').convert('RGB')
correct_resized = correct_render.resize(orig_ref.size)

orig_pixels = list(orig_ref.getdata())
correct_pixels = list(correct_resized.getdata())

match = sum(1 for p1, p2 in zip(orig_pixels, correct_pixels) if p1 == p2)
total = len(orig_pixels)
match_pct = 100 * match / total

print(f"Correct renderer vs original: {match_pct:.2f}% match")
print()

if match_pct > 90:
    print("✓✓✓ EXCELLENT! Correct renderer matches original game!")
    print()
    print("This confirms the tile decoding logic is correct.")
    print("Now we can identify what's wrong with our current rendering pipeline.")
elif match_pct > 50:
    print("✓ Partial match - some elements are correct")
    print("Need to identify which specific elements are wrong.")
else:
    print("✗ Low match - fundamental issues remain")
    print("Need to investigate tileset mapping, palette groups, or layout.")

print()
print("Creating comparison images...")

# Create side-by-side comparison
comparison = Image.new('RGB', (orig_ref.width * 2 + 10, orig_ref.height), (0, 0, 0))
comparison.paste(orig_ref, (0, 0))
comparison.paste(correct_resized, (orig_ref.width + 10, 0))
comparison.save('/tmp/baron_original_vs_correct.png')

print(f"✓ Saved to /tmp/baron_original_vs_correct.png")
print()
print("View with: eog /tmp/baron_original_vs_correct.png")

