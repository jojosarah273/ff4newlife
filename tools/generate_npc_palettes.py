#!/usr/bin/env python3
"""
Generate correct NPC palettes for all maps.

Based on SNES ASM analysis with offset correction:
- Characters (sheet < 14): use npcPalette1
  final = (13 + 2*(npcPalette1 & 7) + PlayerPalTbl[char_id]//2) % 36
- NPCs (sheet >= 14): use npcPalette2 with offset +19
  final = (13 + 2*(npcPalette2 & 7) + 19 + palette_field//2) % 36

The +19 offset for base2 was determined empirically by comparing
rendered palettes to the original game.
"""

import json
from pathlib import Path

def main():
    json_path = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json")
    submaps_dir = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/submaps")
    
    with open(json_path) as f:
        data = json.load(f)
    
    obj = data['obj']
    map_props = obj['mapProperties']
    npc_props = obj['npcProperties']
    npc_gfx = obj['npcGraphicsProperties']
    
    # Player palette table from sprite.asm
    PLAYER_PAL_TBL = [0, 0, 1, 2, 2, 2, 0, 1, 1, 3, 0, 1, 0, 0, 0, 0, 0, 0]
    
    # Offset for base2 (determined empirically)
    BASE2_OFFSET = 19
    
    print(f"Generating NPC palettes with base2 offset = +{BASE2_OFFSET}...\n")
    
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
        
        # Compute base palettes
        base1 = (13 + 2 * (npc_pal1 & 7)) % 36
        base2 = (13 + 2 * (npc_pal2 & 7) + BASE2_OFFSET) % 36
        
        # Read existing .npc file
        npc_file = submaps_dir / f"{map_id}.npc"
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
            
            if map_id < 10:  # Print first 10 maps for verification
                print(f"Map {map_id}: base1={base1}, base2={base2}")
                for i, npc in enumerate(npcs[:3]):
                    gfx_id = npc.get('switch0', 0)
                    sheet = npc_gfx[gfx_id] if gfx_id < len(npc_gfx) else 0
                    pal_field = npc.get('palette', 0)
                    
                    if sheet < 14:
                        char_pal = PLAYER_PAL_TBL[sheet] if sheet < len(PLAYER_PAL_TBL) else 0
                        final_pal = (base1 + (char_pal // 2)) % 36
                        print(f"  NPC {i}: Character (sheet={sheet}), final={final_pal}")
                    else:
                        final_pal = (base2 + (pal_field // 2)) % 36
                        print(f"  NPC {i}: NPC (sheet={sheet}), pal_field={pal_field}, final={final_pal}")
                print()
    
    print(f"\nUpdated {updated_count} .npc files")

if __name__ == '__main__':
    main()
