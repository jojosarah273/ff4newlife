#!/usr/bin/env python3
"""
Palette Offset Finder - Helps determine the correct palette offset by
analyzing the relationship between map properties and palette indices.

This tool tests different offset formulas and checks for consistency.
"""

import json
from pathlib import Path

def test_formula(formula_name, base1_func, base2_func):
    """Test a specific formula across multiple maps."""
    json_path = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json")
    
    with open(json_path) as f:
        data = json.load(f)
    
    obj = data['obj']
    map_props = obj['mapProperties']
    
    results = []
    
    for map_id in range(50):
        if map_id >= len(map_props):
            continue
        
        mp = map_props[map_id]
        npc_pal1 = mp.get('npcPalette1', 0)
        npc_pal2 = mp.get('npcPalette2', 0)
        
        base1 = base1_func(npc_pal1, npc_pal2)
        base2 = base2_func(npc_pal1, npc_pal2)
        
        results.append({
            'map_id': map_id,
            'npc_pal1': npc_pal1,
            'npc_pal2': npc_pal2,
            'base1': base1,
            'base2': base2
        })
    
    return results

def main():
    print("=== Testing Different Palette Offset Formulas ===\n")
    
    # Test various formulas
    formulas = [
        ("Current (no offset)", 
         lambda p1, p2: (13 + 2 * (p1 & 7)) % 36,
         lambda p1, p2: (13 + 2 * (p2 & 7)) % 36),
        
        ("Offset +19 for base2",
         lambda p1, p2: (13 + 2 * (p1 & 7)) % 36,
         lambda p1, p2: (13 + 2 * (p2 & 7) + 19) % 36),
        
        ("Offset +21 for base2",
         lambda p1, p2: (13 + 2 * (p1 & 7)) % 36,
         lambda p1, p2: (13 + 2 * (p2 & 7) + 21) % 36),
        
        ("Both offset +19",
         lambda p1, p2: (13 + 2 * (p1 & 7) + 19) % 36,
         lambda p1, p2: (13 + 2 * (p2 & 7) + 19) % 36),
        
        ("Base 32 instead of 13",
         lambda p1, p2: (32 + 2 * (p1 & 7)) % 36,
         lambda p1, p2: (32 + 2 * (p2 & 7)) % 36),
        
        ("Base 0, step 1",
         lambda p1, p2: (0 + 1 * (p1 & 7)) % 36,
         lambda p1, p2: (0 + 1 * (p2 & 7)) % 36),
    ]
    
    for name, base1_func, base2_func in formulas:
        print(f"\n{name}:")
        print("-" * 80)
        results = test_formula(name, base1_func, base2_func)
        
        # Show first 10 maps
        for r in results[:10]:
            print(f"  Map {r['map_id']:2d}: pal1={r['npc_pal1']}, pal2={r['npc_pal2']} -> "
                  f"base1={r['base1']:2d}, base2={r['base2']:2d}")
    
    print("\n" + "=" * 80)
    print("Analysis:")
    print("=" * 80)
    print("\nThe user reports that in Baron (map 0):")
    print("  - Current formula gives base2=15")
    print("  - They need to cycle to palette 34")
    print("  - 'The other one, the base 0 is right'")
    print("\nThis suggests:")
    print("  - base1 (13) is correct")
    print("  - base2 needs offset: 15 + offset = 34, so offset = 19")
    print("\nBut we need to verify this is consistent across all maps.")

if __name__ == '__main__':
    main()
