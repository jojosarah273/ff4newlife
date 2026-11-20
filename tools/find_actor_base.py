#!/usr/bin/env python3
"""
Find the correct base value for character palettes.
"""
import json
from pathlib import Path

def main():
    # Load map data
    with open('/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json') as f:
        data = json.load(f)
    
    obj = data['obj']
    map_props = obj['mapProperties']
    
    # Test a few maps with known npcPalette values
    test_maps = [0, 2, 36]  # Baron, Kaipo, Mysidia
    
    print("=" * 70)
    print("Character Palette Base Finder")
    print("=" * 70)
    print()
    print("Testing different base values for character palettes:")
    print()
    
    for map_id in test_maps:
        mp = map_props[map_id]
        npc_pal1 = mp.get('npcPalette1', 0)
        
        print(f"Map {map_id} (npcPalette1={npc_pal1}):")
        for base in [13, 14, 15, 16]:
            pal_id = (base + 2 * (npc_pal1 & 7)) % 36
            print(f"  Base {base}: palette {pal_id}")
        print()
    
    print("=" * 70)
    print("To determine the correct base:")
    print("=" * 70)
    print()
    print("1. Run ff4_world and go to Baron (map 0)")
    print("2. Look at the main actor (Cecil)")
    print("3. Use the palette inspector (I key) to cycle through palettes")
    print("4. Note which palette index gives the correct colors")
    print("5. That tells us the correct base:")
    print()
    print("   If palette 13 is correct → base = 13")
    print("   If palette 14 is correct → base = 14")
    print("   If palette 15 is correct → base = 15")
    print("   If palette 16 is correct → base = 16")
    print()
    print("Then we can update the formula accordingly.")

if __name__ == '__main__':
    main()
