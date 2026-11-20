#!/usr/bin/env python3
"""
Regenerate all .npc files with the correct palette formula.

The correct formula uses base=15 (not 13), derived from:
- ROM offset 0xA8700 for MapSpritePal
- User verification: Baron should use palettes 15 and 17
- Formula: base = 15 + 2*(npcPalette & 7)
"""

import json
from pathlib import Path

JSON_PATH = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json")
SUBMAPS_DIR = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/submaps")

def main():
    print("=== Regenerating All NPC Files with Correct Formula ===\n")
    
    with open(JSON_PATH) as f:
        data = json.load(f)
    
    obj = data['obj']
    map_props = obj['mapProperties']
    npc_props = obj['npcProperties']
    npc_gfx = obj['npcGraphicsProperties']
    
    # Player palette table
    PLAYER_PAL_TBL = [0, 0, 1, 2, 2, 2, 0, 1, 1, 3, 0, 1, 0, 0, 0, 0, 0, 0]
    
    # Correct base from ROM analysis
    BASE = 15
    
    print(f"Using base = {BASE}\n")
    
    updated_count = 0
    
    for map_id in range(len(map_props)):
        if map_id >= len(npc_props):
            continue
        
        mp = map_props[map_id]
        npcs = npc_props[map_id]
        
        if not npcs:
            continue
        
        npc_pal1 = mp.get('npcPalette1', 0)
        npc_pal2 = mp.get('npcPalette2', 0)
        
        # Compute base palettes with correct formula
        base1 = (BASE + 2 * (npc_pal1 & 7)) % 36
        base2 = (BASE + 2 * (npc_pal2 & 7)) % 36
        
        # Read existing .npc file
        npc_file = SUBMAPS_DIR / f"{map_id}.npc"
        if not npc_file.exists():
            continue
        
        with open(npc_file) as f:
            lines = f.readlines()
        
        # Update palette values
        new_lines = []
        map_updated = False
        
        for i, line in enumerate(lines):
            if i >= len(npcs):
                new_lines.append(line)
                continue
            
            npc = npcs[i]
            gfx_id = npc.get('switch0', 0)
            sheet = npc_gfx[gfx_id] if gfx_id < len(npc_gfx) else 0
            pal_field = npc.get('palette', 0)
            
            # Parse the line
            parts = line.split('\t')
            if len(parts) < 2:
                new_lines.append(line)
                continue
            
            fields = parts[0].split()
            if len(fields) < 5:
                new_lines.append(line)
                continue
            
            # Compute correct palette
            if sheet < 14:
                # Character
                char_pal = PLAYER_PAL_TBL[sheet] if sheet < len(PLAYER_PAL_TBL) else 0
                final_pal = (base1 + (char_pal // 2)) % 36
            else:
                # Regular NPC
                final_pal = (base2 + (pal_field // 2)) % 36
            
            # Check if palette changed
            old_pal = int(fields[3])
            if old_pal != final_pal:
                map_updated = True
            
            # Update the palette field
            fields[3] = str(final_pal)
            parts[0] = ' '.join(fields)
            new_lines.append('\t'.join(parts))
        
        # Write updated .npc file if changed
        if map_updated:
            with open(npc_file, 'w') as f:
                f.writelines(new_lines)
            updated_count += 1
    
    print(f"Updated {updated_count} .npc files")
    print(f"\nBaron (map 0) with base={BASE}:")
    
    mp = map_props[0]
    npc_pal1 = mp.get('npcPalette1', 0)
    npc_pal2 = mp.get('npcPalette2', 0)
    base1 = (BASE + 2 * (npc_pal1 & 7)) % 36
    base2 = (BASE + 2 * (npc_pal2 & 7)) % 36
    print(f"  npcPalette1={npc_pal1} → base1={base1}")
    print(f"  npcPalette2={npc_pal2} → base2={base2}")
    print(f"\n  This gives palettes {base1} and {base2} for Baron ✓")

if __name__ == '__main__':
    main()
