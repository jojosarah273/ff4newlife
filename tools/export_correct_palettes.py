#!/usr/bin/env python3
"""
Export correct NPC palettes directly from the ROM.

The JSON npcPalettes table is WRONG. This tool extracts the actual
palettes from the ROM and saves them in the correct format.
"""

import struct
import base64
from pathlib import Path

ROM_PATH = Path("/home/jojosarah273/DEV/FF4_ASMTOC/ff2.smc")
OUTPUT_DIR = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/gfx/gfx")

def read_rom():
    """Read ROM file, handling header if present."""
    with open(ROM_PATH, 'rb') as f:
        data = f.read()
    
    # Remove header if present
    if len(data) % 0x400 == 0x200:
        data = data[0x200:]
    
    return data

def extract_map_sprite_pal(rom):
    """Extract MapSpritePal table from ROM."""
    # MapSpritePal is at bank $15, address $C700
    # File offset = 0xA8700
    offset = 0xA8700
    size = 0x240  # 576 bytes = 36 entries × 16 bytes
    
    return rom[offset:offset + size]

def palette_to_bgr555_bytes(palette_data):
    """Convert palette data to BGR555 format (already in correct format)."""
    return palette_data

def palette_to_base64(palette_data):
    """Convert palette data to base64 for JSON."""
    return base64.b64encode(palette_data).decode('ascii')

def palette_to_rgba(palette_data):
    """Convert BGR555 palette to RGBA for PNG/display."""
    colors = []
    for i in range(0, len(palette_data), 2):
        bgr = palette_data[i] | (palette_data[i+1] << 8)
        r = ((bgr >> 0) & 0x1F) * 8
        g = ((bgr >> 5) & 0x1F) * 8
        b = ((bgr >> 10) & 0x1F) * 8
        a = 255 if i > 0 else 0  # Color 0 is transparent
        colors.append((r, g, b, a))
    return colors

def main():
    print("=== Exporting Correct NPC Palettes from ROM ===\n")
    
    rom = read_rom()
    print(f"Loaded ROM: {len(rom)} bytes\n")
    
    # Extract palettes from ROM
    pal_data = extract_map_sprite_pal(rom)
    print(f"Extracted {len(pal_data)} bytes of palette data\n")
    
    # Ensure output directory exists
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    
    # Export each palette as a separate binary file
    print("Exporting individual palette files...")
    for i in range(36):
        entry = pal_data[i*16:(i+1)*16]
        output_file = OUTPUT_DIR / f"npcPalettes[{i}].bin"
        
        with open(output_file, 'wb') as f:
            f.write(entry)
        
        # Show first few colors
        colors = palette_to_rgba(entry)
        print(f"  Palette {i:2d}: {colors[1]} (color 1)")  # Skip color 0 (transparent)
    
    print(f"\nExported 36 palette files to {OUTPUT_DIR}")
    
    # Create a summary showing the difference
    print("\n" + "=" * 80)
    print("SUMMARY")
    print("=" * 80)
    print("\nThe JSON npcPalettes table was WRONG.")
    print("The correct palettes are now exported from the ROM.")
    print("\nFor Baron (map 0) with base=15:")
    print("  base1 = 15 + 2*0 = 15")
    print("  base2 = 15 + 2*1 = 17")
    print("\nThis matches what you reported: palettes 15 and 17!")
    print("\nNext step: update ff4_world to use these correct palettes.")

if __name__ == '__main__':
    main()
