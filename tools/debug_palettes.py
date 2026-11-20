#!/usr/bin/env python3
"""
Debug tool to analyze NPC palettes and find the correct formula.

This tool:
1. Loads the actual palette colors from the JSON
2. Shows what each palette looks like
3. Helps identify patterns in the palette selection
"""

import json
import base64
from pathlib import Path

def decode_palette(b64_data):
    """Decode base64 palette data to RGB tuples."""
    raw = base64.b64decode(b64_data)
    colors = []
    for i in range(0, len(raw), 2):
        if i + 1 < len(raw):
            # BGR555 format
            bgr = raw[i] | (raw[i+1] << 8)
            r = ((bgr >> 0) & 0x1F) * 8
            g = ((bgr >> 5) & 0x1F) * 8
            b = ((bgr >> 10) & 0x1F) * 8
            colors.append((r, g, b))
    return colors

def main():
    json_path = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json")
    
    with open(json_path) as f:
        data = json.load(f)
    
    obj = data['obj']
    npc_palettes = obj['npcPalettes']
    map_props = obj['mapProperties']
    
    print("=== NPC Palette Debug Tool ===\n")
    print(f"Total palettes in table: {len(npc_palettes)}\n")
    
    # Show first few palettes
    print("First 10 palettes (showing first 4 colors each):")
    print("-" * 80)
    for i in range(min(10, len(npc_palettes))):
        colors = decode_palette(npc_palettes[i])
        print(f"Palette {i:2d}: {colors[:4]}")
    
    print("\n" + "=" * 80)
    print("Analyzing Baron (map 0) and nearby maps:")
    print("=" * 80)
    
    for map_id in [0, 1, 2, 36]:
        if map_id >= len(map_props):
            continue
        
        mp = map_props[map_id]
        npc_pal1 = mp.get('npcPalette1', 0)
        npc_pal2 = mp.get('npcPalette2', 0)
        
        print(f"\nMap {map_id}: npcPalette1={npc_pal1}, npcPalette2={npc_pal2}")
        
        # Current formula
        base1 = (13 + 2 * (npc_pal1 & 7)) % 36
        base2 = (13 + 2 * (npc_pal2 & 7)) % 36
        
        print(f"  Current formula: base1={base1}, base2={base2}")
        
        # With offset 19 for base2
        base2_offset = (base2 + 19) % 36
        print(f"  With offset +19: base1={base1}, base2={base2_offset}")
        
        # Show the actual palette colors
        print(f"\n  Palette {base1} (base1):")
        colors1 = decode_palette(npc_palettes[base1])
        print(f"    First 8 colors: {colors1[:8]}")
        
        print(f"\n  Palette {base2} (base2 current):")
        colors2 = decode_palette(npc_palettes[base2])
        print(f"    First 8 colors: {colors2[:8]}")
        
        print(f"\n  Palette {base2_offset} (base2 with offset +19):")
        colors2_off = decode_palette(npc_palettes[base2_offset])
        print(f"    First 8 colors: {colors2_off[:8]}")

if __name__ == '__main__':
    main()
