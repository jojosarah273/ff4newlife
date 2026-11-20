#!/usr/bin/env python3
"""
Direct ROM palette extraction - no formula guessing needed.
Extracts MapSpritePal directly from ROM and creates palette files.
"""
import struct
from pathlib import Path

ROM_PATH = Path("/home/jojosarah273/DEV/FF4_ASMTOC/ff2.smc")
OUTPUT_DIR = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/gfx/gfx")

def load_rom():
    with open(ROM_PATH, 'rb') as f:
        rom = f.read()
    if len(rom) % 0x400 == 0x200:
        rom = rom[0x200:]
    return rom

def extract_map_sprite_pal(rom):
    """Extract MapSpritePal from ROM at offset 0xA8700"""
    offset = 0xA8700
    size = 0x240  # 576 bytes = 36 palettes × 16 bytes
    return rom[offset:offset + size]

def main():
    print("=" * 80)
    print("DIRECT ROM PALETTE EXTRACTION")
    print("=" * 80)
    
    rom = load_rom()
    print(f"Loaded ROM: {len(rom)} bytes")
    
    # Extract MapSpritePal
    pal_data = extract_map_sprite_pal(rom)
    print(f"Extracted MapSpritePal: {len(pal_data)} bytes")
    
    # Verify we got valid data (shouldn't be all zeros)
    if all(b == 0 for b in pal_data):
        print("✗ ERROR: Extracted data is all zeros!")
        return 1
    
    # Save each palette
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    
    print(f"\nExporting 36 palette files to {OUTPUT_DIR}")
    for i in range(36):
        pal_bytes = pal_data[i * 16:(i + 1) * 16]
        output_file = OUTPUT_DIR / f"npcPalettes[{i}].bin"
        
        with open(output_file, 'wb') as f:
            f.write(pal_bytes)
        
        # Show first color as verification
        if len(pal_bytes) >= 2:
            bgr = struct.unpack_from('<H', pal_bytes, 0)[0]
            r = ((bgr >> 0) & 0x1F) * 8
            g = ((bgr >> 5) & 0x1F) * 8
            b = ((bgr >> 10) & 0x1F) * 8
            print(f"  Palette {i:2d}: {len(pal_bytes):2d} bytes, color0=RGB({r:3d},{g:3d},{b:3d})")
    
    print("\n✓ Exported 36 palettes directly from ROM")
    print("These are the ACTUAL palette values from the game, no formula needed!")
    
    return 0

if __name__ == '__main__':
    exit(main())
