#!/usr/bin/env python3
"""
Verify that ff4_world can load the corrected palettes and test Baron.
"""

import struct
from pathlib import Path

GFX_DIR = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/gfx/gfx")

def load_palette_file(pal_id):
    """Load a palette file and return RGB colors."""
    pal_file = GFX_DIR / f"npcPalettes[{pal_id}].bin"
    
    if not pal_file.exists():
        return None
    
    with open(pal_file, 'rb') as f:
        data = f.read()
    
    colors = []
    for i in range(0, len(data), 2):
        if i + 1 < len(data):
            bgr = data[i] | (data[i+1] << 8)
            r = ((bgr >> 0) & 0x1F) * 8
            g = ((bgr >> 5) & 0x1F) * 8
            b = ((bgr >> 10) & 0x1F) * 8
            colors.append((r, g, b))
    
    return colors

def main():
    print("=== Verifying Palette Loading ===\n")
    
    # Test loading palettes 15 and 17 (Baron)
    print("Baron (map 0) should use palettes 15 and 17:\n")
    
    pal15 = load_palette_file(15)
    pal17 = load_palette_file(17)
    
    if pal15 and pal17:
        print("Palette 15 (first 4 colors):")
        for i, color in enumerate(pal15[:4]):
            print(f"  Color {i}: RGB{color}")
        
        print("\nPalette 17 (first 4 colors):")
        for i, color in enumerate(pal17[:4]):
            print(f"  Color {i}: RGB{color}")
        
        print("\n✓ Palettes loaded successfully from ROM data!")
        print("\nNow rebuild ff4_world and test Baron:")
        print("  cd build && make && ./ff4_world")
        print("  Then warp to Baron (map 0) and check NPC colors")
    else:
        print("✗ Failed to load palette files!")
        print(f"  Palette 15: {'found' if pal15 else 'MISSING'}")
        print(f"  Palette 17: {'found' if pal17 else 'MISSING'}")

if __name__ == '__main__':
    main()
