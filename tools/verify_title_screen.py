#!/usr/bin/env python3
"""
Headless title screen verification system.
Extracts title screen from ROM and compares with reference image.
"""
import struct
import json
import base64
from pathlib import Path
import sys

ROM_PATH = Path("/home/jojosarah273/DEV/FF4_ASMTOC/ff2.smc")
REF_PATH = Path("/home/jojosarah273/DEV/FF4_ASMTOC/refs/snes_title_fixed.png")

def load_rom():
    with open(ROM_PATH, 'rb') as f:
        rom = f.read()
    if len(rom) % 0x400 == 0x200:
        rom = rom[0x200:]
    return rom

def extract_title_gfx(rom):
    """Extract title screen graphics (4bpp tiles)"""
    # TitleGfx is at offset 0x040000, size 0x2000
    offset = 0x040000
    size = 0x2000
    return rom[offset:offset+size]

def extract_title_tilemap(rom):
    """Extract title screen tilemap"""
    with open('/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json') as f:
        data = json.load(f)
    tilemap_b64 = data['obj']['splashTilemap']
    return base64.b64decode(tilemap_b64 + '==')

def extract_title_pal(rom):
    """Extract title background palette (128 colors)"""
    offset = 0x046800
    colors = []
    for i in range(128):
        bgr = struct.unpack_from('<H', rom, offset + i*2)[0]
        r = ((bgr >> 0) & 0x1F) * 8
        g = ((bgr >> 5) & 0x1F) * 8
        b = ((bgr >> 10) & 0x1F) * 8
        colors.append((r, g, b))
    return colors

def extract_title_sprite_pal(rom):
    """Extract title sprite palette (8 colors, grayscale)"""
    offset = 0x0A7CE6
    colors = []
    for i in range(8):
        bgr = struct.unpack_from('<H', rom, offset + i*2)[0]
        r = ((bgr >> 0) & 0x1F) * 8
        g = ((bgr >> 5) & 0x1F) * 8
        b = ((bgr >> 10) & 0x1F) * 8
        colors.append((r, g, b))
    return colors

def render_tile_4bpp(gfx_data, tile_index, palette):
    """Render a single 8x8 tile from 4bpp graphics"""
    pixels = []
    tile_offset = tile_index * 32
    
    for y in range(8):
        plane01 = struct.unpack_from('<H', gfx_data, tile_offset + y*2)[0]
        plane23 = struct.unpack_from('<H', gfx_data, tile_offset + 16 + y*2)[0]
        
        for x in range(8):
            bit = 7 - x
            p0 = (plane01 >> bit) & 1
            p1 = (plane01 >> (bit + 8)) & 1
            p2 = (plane23 >> bit) & 1
            p3 = (plane23 >> (bit + 8)) & 1
            
            color_index = p0 | (p1 << 1) | (p2 << 2) | (p3 << 3)
            pixels.append(palette[color_index])
    
    return pixels

def render_title_background():
    """Render the title screen background"""
    print("Extracting title screen data from ROM...")
    rom = load_rom()
    
    title_pal = extract_title_pal(rom)
    gfx_data = extract_title_gfx(rom)
    tilemap = extract_title_tilemap(rom)
    
    print(f"  Background palette: {len(title_pal)} colors")
    print(f"  Graphics: {len(gfx_data)} bytes")
    print(f"  Tilemap: {len(tilemap)} bytes")
    
    # Render 32x28 tiles (256x224 pixels)
    width = 32 * 8
    height = 28 * 8
    image = [None] * (width * height)
    
    for ty in range(28):
        for tx in range(32):
            tilemap_offset = (ty * 32 + tx) * 2
            if tilemap_offset + 1 >= len(tilemap):
                continue
            
            entry = struct.unpack_from('<H', tilemap, tilemap_offset)[0]
            tile_index = entry & 0x3FF
            palette_group = (entry >> 10) & 7
            hflip = (entry >> 14) & 1
            vflip = (entry >> 15) & 1
            
            # Get palette for this tile (each group is 16 colors)
            pal_start = palette_group * 16
            tile_palette = title_pal[pal_start:pal_start+16]
            
            tile_pixels = render_tile_4bpp(gfx_data, tile_index, tile_palette)
            
            # Apply flipping
            if hflip or vflip:
                flipped = []
                for y in range(8):
                    for x in range(8):
                        fx = 7 - x if hflip else x
                        fy = 7 - y if vflip else y
                        flipped.append(tile_pixels[fy * 8 + fx])
                tile_pixels = flipped
            
            # Copy to image
            for y in range(8):
                for x in range(8):
                    px = tx * 8 + x
                    py = ty * 8 + y
                    if px < width and py < height:
                        image[py * width + px] = tile_pixels[y * 8 + x]
    
    return image, width, height

def save_ppm(filename, image, width, height):
    """Save image as PPM"""
    with open(filename, 'w') as f:
        f.write(f"P3\n{width} {height}\n255\n")
        for pixel in image:
            if pixel is None:
                f.write("0 0 0 ")
            else:
                r, g, b = pixel
                f.write(f"{r} {g} {b} ")

def main():
    print("=" * 80)
    print("TITLE SCREEN VERIFICATION SYSTEM")
    print("=" * 80)
    print()
    
    # Render the background
    image, width, height = render_title_background()
    
    # Save rendered image
    output_path = Path("/tmp/rendered_title_background.ppm")
    save_ppm(output_path, image, width, height)
    print(f"\n✓ Rendered background saved to {output_path}")
    
    # Extract sprite palette
    rom = load_rom()
    sprite_pal = extract_title_sprite_pal(rom)
    
    print("\nTitle Sprite Palette (8 colors, grayscale):")
    for i, (r, g, b) in enumerate(sprite_pal):
        print(f"  Color {i}: RGB({r:3d}, {g:3d}, {b:3d})")
    
    print("\n" + "=" * 80)
    print("ANALYSIS:")
    print("=" * 80)
    print()
    print("The title screen consists of:")
    print("1. Background tilemap (rendered above)")
    print("2. Sprite overlay (main actor Cecil) - NOT YET RENDERED")
    print()
    print("The sprite uses TitleSpritePal (8 grayscale colors).")
    print()
    print("NEXT STEPS:")
    print("1. Find where the title sprite graphics are defined")
    print("2. Find where the sprite OAM data is set up")
    print("3. Render the sprite overlay")
    print("4. Compare complete render with reference image")
    print()
    print("Reference image: " + str(REF_PATH))
    print("Rendered background: " + str(output_path))

if __name__ == '__main__':
    main()
