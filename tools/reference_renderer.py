#!/usr/bin/env python3
"""
Reference renderer - decodes tiles directly from ROM to establish ground truth.
This bypasses all our rendering code and shows what the ROM data actually contains.
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
map_props = obj['mapProperties'][36]  # Baron

def decode_snes_tile(tile_bytes, bpp=3):
    """Decode a SNES tile (8x8 pixels) in 2bpp or 3bpp format."""
    pixels = []
    
    if bpp == 3:
        # 3bpp: planes 0-1 interleaved (2 bytes per row), plane 2 separate (1 byte per row)
        for y in range(8):
            row = []
            plane01 = tile_bytes[y*2] | (tile_bytes[y*2+1] << 8)
            plane2 = tile_bytes[16 + y]
            
            for x in range(8):
                bit = 7 - x
                p0 = (plane01 >> bit) & 1
                p1 = (plane01 >> (bit + 8)) & 1
                p2 = (plane2 >> bit) & 1
                
                color_idx = p0 | (p1 << 1) | (p2 << 2)
                row.append(color_idx)
            pixels.append(row)
    else:  # bpp == 2
        # 2bpp: planes 0-1 interleaved (2 bytes per row)
        for y in range(8):
            row = []
            plane01 = tile_bytes[y*2] | (tile_bytes[y*2+1] << 8)
            
            for x in range(8):
                bit = 7 - x
                p0 = (plane01 >> bit) & 1
                p1 = (plane01 >> (bit + 8)) & 1
                
                color_idx = p0 | (p1 << 1)
                row.append(color_idx)
            pixels.append(row)
    
    return pixels

def decode_palette(palette_data, num_colors=16):
    """Decode SNES palette data (BGR555 format) to RGB."""
    colors = []
    for i in range(num_colors):
        if i * 2 + 1 < len(palette_data):
            bgr = palette_data[i*2] | (palette_data[i*2+1] << 8)
            r = ((bgr >> 0) & 0x1F) * 8
            g = ((bgr >> 5) & 0x1F) * 8
            b = ((bgr >> 10) & 0x1F) * 8
            colors.append((r, g, b))
    return colors

def render_tile(tile_pixels, palette, x_offset, y_offset, img):
    """Render an 8x8 tile to an image."""
    for y, row in enumerate(tile_pixels):
        for x, color_idx in enumerate(row):
            if color_idx < len(palette):
                px = x_offset + x
                py = y_offset + y
                if 0 <= px < img.width and 0 <= py < img.height:
                    img.putpixel((px, py), palette[color_idx])

def render_map_reference(map_id):
    """Render a map using ROM data directly."""
    mp = obj['mapProperties'][map_id]
    tileset_id = mp['graphics'] + 1
    palette_id = mp['palette']
    layout_id = mp['layout1']
    
    # Extract tileset from ROM
    tileset_offset = 0xA2000 + (tileset_id - 1) * 0x400
    tileset_data = rom[tileset_offset:tileset_offset + 0x400]
    
    # Extract palette from ROM
    palette_offset = 0xA6000 + palette_id * 0x80
    palette_data = rom[palette_offset:palette_offset + 0x80]
    palette = decode_palette(palette_data, 16)
    
    # Extract layout from JSON
    layout_data = obj['mapLayouts1'][layout_id]
    layout_bytes = base64.b64decode(layout_data)
    
    # Create output image (32x32 tiles = 256x256 pixels)
    img = Image.new('RGB', (256, 256), (0, 0, 0))
    
    # Render each tile
    for tile_y in range(32):
        for tile_x in range(32):
            tile_idx = layout_bytes[tile_y * 32 + tile_x]
            
            if tile_idx < 128:  # Valid tile
                # Extract tile data (32 bytes for 3bpp)
                tile_offset = tile_idx * 32
                if tile_offset + 32 <= len(tileset_data):
                    tile_bytes = tileset_data[tile_offset:tile_offset + 32]
                    tile_pixels = decode_snes_tile(tile_bytes, bpp=3)
                    
                    # Render tile
                    x_offset = tile_x * 8
                    y_offset = tile_y * 8
                    render_tile(tile_pixels, palette, x_offset, y_offset, img)
    
    return img

print("=" * 80)
print("REFERENCE RENDERER - GROUND TRUTH FROM ROM")
print("=" * 80)
print()

# Render Baron using ROM data
print("Rendering Baron (map 36) from ROM data...")
ref_render = render_map_reference(36)
ref_render.save('/tmp/baron_reference_render.png')
print("✓ Saved to /tmp/baron_reference_render.png")
print()

# Load original reference screenshot
orig_ref = Image.open('/home/jojosarah273/DEV/FF4_ASMTOC/refs/baron_fixed.png').convert('RGB')

# Load our current rendering
our_render = Image.open('/tmp/test_render_map36.png').convert('RGB').resize(orig_ref.size)

# Create comparison
comparison = Image.new('RGB', (orig_ref.width * 3 + 20, orig_ref.height), (0, 0, 0))
comparison.paste(orig_ref, (0, 0))
comparison.paste(ref_render, (orig_ref.width + 10, 0))
comparison.paste(our_render, (orig_ref.width * 2 + 20, 0))
comparison.save('/tmp/baron_three_way_comparison.png')

print("Created three-way comparison:")
print("  Left: Original game screenshot (baron_fixed.png)")
print("  Middle: Reference renderer (from ROM data)")
print("  Right: Our current rendering")
print()
print(f"Saved to: /tmp/baron_three_way_comparison.png")
print()

# Calculate match percentages
orig_pixels = list(orig_ref.getdata())
ref_pixels = list(ref_render.resize(orig_ref.size).getdata())
our_pixels = list(our_render.getdata())

ref_match = sum(1 for p1, p2 in zip(orig_pixels, ref_pixels) if p1 == p2)
our_match = sum(1 for p1, p2 in zip(orig_pixels, our_pixels) if p1 == p2)
total = len(orig_pixels)

print("Match percentages:")
print(f"  Reference renderer vs original: {100*ref_match/total:.2f}%")
print(f"  Our rendering vs original:      {100*our_match/total:.2f}%")
print()

if ref_match > our_match:
    print("✓ Reference renderer is BETTER than our current rendering")
    print("  This confirms our rendering pipeline has bugs.")
elif ref_match < our_match:
    print("✗ Reference renderer is WORSE than our current rendering")
    print("  This suggests our tile decoding might be correct.")
else:
    print("= Both renderers have similar accuracy")
print()

print("=" * 80)
print("NEXT STEPS")
print("=" * 80)
print()
print("1. View the three-way comparison:")
print("   eog /tmp/baron_three_way_comparison.png")
print()
print("2. If reference renderer matches original better:")
print("   - Our tile decoding is wrong")
print("   - Our palette application is wrong")
print("   - Need to fix the rendering pipeline")
print()
print("3. If both are equally wrong:")
print("   - Layout data might be wrong")
print("   - Need to check map layout extraction")
print()

