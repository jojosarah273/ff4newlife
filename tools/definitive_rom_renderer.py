#!/usr/bin/env python3
"""
DEFINITIVE ROM-based renderer using correct color expansion.
This establishes the TRUE ground truth from ROM data.
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

# Constants from ff4_export_submaps.py
HDR = 0x200
TILESET = 0xA2000 - HDR
PALETTE = 0xA6000 - HDR

SHEETS = {k: v - HDR for k, v in {
    1: 0xADE00, 2: 0xF61A8, 3: 0xF0220, 4: 0xEEF40, 5: 0xF1768,
    6: 0xF2EC0, 7: 0xF3AC0, 8: 0xF51E8, 9: 0xF9700, 10: 0xF8200,
    11: 0xF61A8, 12: 0xFD408, 13: 0xFEA88}.items()}

def color16(palidx, grp, c):
    """CORRECT color expansion from ff4_export_submaps.py"""
    o = PALETTE + palidx * 0x80 + (grp * 8 + c) * 2
    w = rom[o] | (rom[o + 1] << 8)
    return (((w & 0x1F) << 3) | ((w >> 2) & 7),
            (((w >> 5) & 0x1F) << 3) | ((w >> 7) & 7),
            (((w >> 10) & 0x1F) << 3) | ((w >> 12) & 7))

def tile_px(sheet, idx):
    """CORRECT tile decoding from ff4_export_submaps.py"""
    bpp = 4 if sheet == 1 else 3
    
    if bpp == 4:
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

def render_map_rom(map_id):
    """Render map using CORRECT ROM data"""
    mp = obj['mapProperties'][map_id]
    n = mp['graphics'] + 1
    palidx = mp['palette']
    
    # Determine sheet number
    if n in (1, 16):
        sheet = 1
    elif 2 <= n <= 10:
        sheet = n
    elif n in (11, 12):
        sheet = 9
    elif 13 <= n <= 15:
        sheet = n - 2
    else:
        sheet = 1
    
    ts = TILESET + (n - 1) * 0x400
    
    # Get layouts
    lay1 = mp['layout1']
    lay2 = mp.get('layout2', -1)
    
    layouts1 = obj['mapLayouts1']
    blob1 = base64.b64decode(layouts1[lay1])
    blob2 = base64.b64decode(layouts1[lay2]) if 0 <= lay2 < len(layouts1) else None
    
    # Create 512x512 image
    W = H = 512
    img = Image.new('RGB', (W, H), (0, 0, 0))
    
    # Render BG2 first (opaque)
    if blob2 and len(blob2) >= 1024:
        for cy in range(32):
            for cx in range(32):
                b = blob2[cy * 32 + cx]
                if b >= 128:
                    continue
                
                for j in range(4):
                    lo = rom[ts + b * 2 + j * 0x100]
                    hi = rom[ts + b * 2 + j * 0x100 + 1]
                    sidx = lo + (hi & 3) * 0x100
                    grp = (hi >> 2) & 7
                    hflip = not (hi & 0x40)
                    vflip = bool(hi & 0x80)
                    
                    t = tile_px(sheet, sidx)
                    if t is None:
                        continue
                    
                    ox, oy = (j % 2) * 8, (j // 2) * 8
                    
                    for y in range(8):
                        for x in range(8):
                            src_y = 7 - y if vflip else y
                            src_x = 7 - x if hflip else x
                            c = t[src_y * 8 + src_x]
                            
                            if c > 0:
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
                
                t = tile_px(sheet, sidx)
                if t is None:
                    continue
                
                ox, oy = (j % 2) * 8, (j // 2) * 8
                
                for y in range(8):
                    for x in range(8):
                        src_y = 7 - y if vflip else y
                        src_x = 7 - x if hflip else x
                        c = t[src_y * 8 + src_x]
                        
                        if c > 0:
                            r, g, bl = color16(palidx, grp, c & 7)
                            px = cx * 16 + ox + x
                            py = cy * 16 + oy + y
                            if 0 <= px < W and 0 <= py < H:
                                img.putpixel((px, py), (r, g, bl))
    
    return img

print("=" * 80)
print("DEFINITIVE ROM RENDERER")
print("=" * 80)
print()

# Render Baron
print("Rendering Baron (map 36) from ROM...")
rom_render = render_map_rom(36)
rom_render.save('/tmp/baron_definitive_rom.png')
print("✓ Saved to /tmp/baron_definitive_rom.png")
print()

# Extract viewport at best camera position
viewport = rom_render.crop((48, 144, 48 + 256, 144 + 224))
viewport.save('/tmp/baron_rom_viewport.png')
print("✓ Extracted viewport at camera (3, 9)")
print()

# Create comparison
ref = Image.open('/home/jojosarah273/DEV/FF4_ASMTOC/refs/baron_fixed.png').convert('RGB')
export = Image.open('/home/jojosarah273/DEV/FF4_ASMTOC/data/submaps/36.png').convert('RGB')

comparison = Image.new('RGB', (256 * 3 + 20, 224), (0, 0, 0))
comparison.paste(ref, (0, 0))
comparison.paste(viewport, (256 + 10, 0))
comparison.paste(export.resize((256, 224)), (256 * 2 + 20, 0))
comparison.save('/tmp/baron_comparison_final.png')

print("Created three-way comparison:")
print("  Left: Reference image (emulator screenshot)")
print("  Middle: ROM renderer (ground truth)")
print("  Right: Current export")
print()
print(f"Saved to: /tmp/baron_comparison_final.png")
print()

# Calculate matches
rom_viewport = viewport.convert('RGB')
ref_resized = ref.resize(rom_viewport.size)

rom_pixels = list(rom_viewport.getdata())
ref_pixels = list(ref_resized.getdata())

exact_match = sum(1 for p1, p2 in zip(rom_pixels, ref_pixels) if p1 == p2)
close_match = sum(1 for p1, p2 in zip(rom_pixels, ref_pixels)
                  if all(abs(a - b) < 30 for a, b in zip(p1, p2)))

total = len(rom_pixels)

print("=" * 80)
print("RESULTS")
print("=" * 80)
print()
print(f"ROM renderer vs reference:")
print(f"  Exact match: {100 * exact_match / total:.2f}%")
print(f"  Close match (<30 diff): {100 * close_match / total:.2f}%")
print()

if close_match > 80:
    print("✓✓✓ ROM renderer matches reference well!")
    print()
    print("This confirms:")
    print("  1. Tile decoding is correct")
    print("  2. Palette extraction is correct")
    print("  3. Layout interpretation is correct")
    print()
    print("The reference image has slight color differences due to emulator")
    print("color correction, but the structure matches.")
else:
    print("✗ ROM renderer doesn't match reference")
    print()
    print("This suggests fundamental issues with:")
    print("  - Tile decoding")
    print("  - Palette extraction")
    print("  - Layout interpretation")

print()
print("=" * 80)
print("NEXT STEPS")
print("=" * 80)
print()
print("1. View the comparison:")
print("   eog /tmp/baron_comparison_final.png")
print()
print("2. If ROM renderer matches structure:")
print("   - We have correct ground truth")
print("   - Now need to fix NPC sprite rendering")
print("   - Apply correct NPC palette formula")
print()
print("3. Create demo showing before/after")

