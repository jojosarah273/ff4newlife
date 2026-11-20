#!/usr/bin/env python3
"""
Export map palette assignments from ff4-en-data.json for use by ff4_world.

This creates data/map_palettes.bin with npcPalette1 and npcPalette2 for each map.
"""

import json
import struct
from pathlib import Path

def main():
    json_path = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json")
    output_path = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/map_palettes.bin")
    
    print("Loading FF4 data...")
    with open(json_path, 'r') as f:
        data = json.load(f)
    
    obj = data['obj']
    map_props = obj['mapProperties']
    
    print(f"Exporting palette data for {len(map_props)} maps...")
    
    with open(output_path, 'wb') as f:
        for map_id, mp in enumerate(map_props):
            npc_pal1 = mp.get('npcPalette1', 0)
            npc_pal2 = mp.get('npcPalette2', 0)
            
            # Write as two bytes per map
            f.write(struct.pack('BB', npc_pal1 & 0xFF, npc_pal2 & 0xFF))
    
    print(f"Written {output_path} ({len(map_props) * 2} bytes)")
    
    # Also create a human-readable version
    txt_path = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/map_palettes.txt")
    with open(txt_path, 'w') as f:
        f.write("# Map NPC Palette Assignments\n")
        f.write("# Format: map_id npcPalette1 npcPalette2\n")
        for map_id, mp in enumerate(map_props):
            npc_pal1 = mp.get('npcPalette1', 0)
            npc_pal2 = mp.get('npcPalette2', 0)
            f.write(f"{map_id} {npc_pal1} {npc_pal2}\n")
    
    print(f"Written {txt_path}")

if __name__ == '__main__':
    main()
