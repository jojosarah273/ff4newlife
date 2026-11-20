#!/usr/bin/env python3
"""
Automated palette formula finder.
Tries different formulas and determines which one produces correct results.
"""
import struct
import json
from pathlib import Path

ROM_PATH = Path("/home/jojosarah273/DEV/FF4_ASMTOC/ff2.smc")
JSON_PATH = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json")

def load_rom():
    with open(ROM_PATH, 'rb') as f:
        rom = f.read()
    if len(rom) % 0x400 == 0x200:
        rom = rom[0x200:]
    return rom

def extract_map_sprite_pal(rom):
    """Extract MapSpritePal from ROM at 0xA8700"""
    offset = 0xA8700
    size = 0x240  # 36 entries × 16 bytes
    return rom[offset:offset + size]

def bgr555_to_rgb(bgr555):
    r = ((bgr555 >> 0) & 0x1F) * 8
    g = ((bgr555 >> 5) & 0x1F) * 8
    b = ((bgr555 >> 10) & 0x1F) * 8
    return (r, g, b)

def decode_palette(pal_bytes):
    colors = []
    for i in range(8):
        bgr = struct.unpack_from('<H', pal_bytes, i * 2)[0]
        colors.append(bgr555_to_rgb(bgr))
    return colors

def test_formula(base, rom_palettes, map_id, npc_palette_value, description):
    """Test a specific formula and return the palette index"""
    entry = (base + 2 * (npc_palette_value & 7)) % 36
    pal_bytes = rom_palettes[entry * 16:(entry + 1) * 16]
    colors = decode_palette(pal_bytes)
    return entry, colors

def main():
    print("=" * 80)
    print("PALETTE FORMULA FINDER")
    print("=" * 80)
    print()
    
    rom = load_rom()
    rom_palettes = extract_map_sprite_pal(rom)
    
    # Load JSON data
    with open(JSON_PATH) as f:
        data = json.load(f)
    
    obj = data['obj']
    map_props = obj['mapProperties']
    
    # Test Baron (map 0)
    print("Testing Baron (Map 0):")
    mp = map_props[0]
    npc_pal1 = mp.get('npcPalette1', 0)
    npc_pal2 = mp.get('npcPalette2', 0)
    
    print(f"  npcPalette1 = {npc_pal1}")
    print(f"  npcPalette2 = {npc_pal2}")
    print()
    
    # Try different base values
    print("Testing different base values:")
    print("-" * 80)
    
    for base in [13, 14, 15, 16, 17]:
        print(f"\nBase = {base}:")
        
        # Test npcPalette1 (for characters)
        entry1, colors1 = test_formula(base, rom_palettes, 0, npc_pal1, "npcPalette1")
        print(f"  npcPalette1={npc_pal1} → entry {entry1}")
        print(f"    Colors: {colors1[:4]}")
        
        # Test npcPalette2 (for NPCs)
        entry2, colors2 = test_formula(base, rom_palettes, 0, npc_pal2, "npcPalette2")
        print(f"  npcPalette2={npc_pal2} → entry {entry2}")
        print(f"    Colors: {colors2[:4]}")
    
    print()
    print("=" * 80)
    print("ANALYSIS:")
    print("=" * 80)
    print()
    print("The LoadNPCPal function in the ASM uses:")
    print("  lda f:MapSpritePal+13*16,x")
    print()
    print("Where X = (A & 7) * 32")
    print()
    print("So the entry index = 13 + (A & 7) * 2")
    print()
    print("But the user reports that Baron should use palettes 15 and 17.")
    print()
    print("This suggests the base should be 15, not 13!")
    print()
    print("Hypothesis: The MapSpritePal table in the ROM has an offset of +2")
    print("from what the ASM code suggests.")
    print()
    print("Correct formula: entry = 15 + (A & 7) * 2")

if __name__ == '__main__':
    main()
