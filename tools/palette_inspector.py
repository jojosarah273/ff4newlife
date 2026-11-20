#!/usr/bin/env python3
"""
NPC Palette Inspector - Empirical palette discovery tool

This tool helps you determine the correct palettes by:
1. Loading a map
2. Cycling through all possible palettes for each NPC
3. Recording which palette looks correct
4. Building a database of correct palettes

Usage:
  python3 tools/palette_inspector.py <map_id>
  
Then in the viewer:
  - TAB: Select next NPC
  - H/J: Cycle palette for selected NPC
  - ENTER: Mark current palette as correct
  - S: Save all recorded palettes to data/correct_palettes.json
  - Q: Quit
"""

import json
import sys
from pathlib import Path

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 palette_inspector.py <map_id>")
        print("  This generates a script to run ff4_world with palette inspection enabled")
        sys.exit(1)
    
    map_id = int(sys.argv[1])
    
    # Load map data
    json_path = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json")
    with open(json_path) as f:
        data = json.load(f)
    
    obj = data['obj']
    map_props = obj['mapProperties'][map_id]
    npcs = obj['npcProperties'][map_id]
    npc_gfx = obj['npcGraphicsProperties']
    
    print(f"Map {map_id}: {map_props.get('title', 'Unknown')}")
    print(f"npcPalette1: {map_props.get('npcPalette1')}")
    print(f"npcPalette2: {map_props.get('npcPalette2')}")
    print(f"NPCs: {len(npcs)}")
    print()
    
    for i, npc in enumerate(npcs):
        gfx_id = npc.get('switch0', 0)
        sheet = npc_gfx[gfx_id] if gfx_id < len(npc_gfx) else 0
        pal_field = npc.get('palette', 0)
        char_name = f"Char{sheet}" if sheet < 14 else f"NPC{gfx_id}"
        print(f"  NPC {i}: {char_name} (sheet={sheet}, pal_field={pal_field}, pos={npc.get('x')},{npc.get('y')})")
    
    print()
    print("To inspect palettes, run:")
    print(f"  FF4_WARPTO={map_id} FF4_PALINSPECT=1 ./build/ff4_world")
    print()
    print("Controls:")
    print("  TAB     - Select next NPC")
    print("  H/J     - Cycle palette for selected NPC")
    print("  ENTER   - Mark current palette as correct")
    print("  S       - Save recorded palettes")
    print("  Q       - Quit")

if __name__ == '__main__':
    main()
