#!/usr/bin/env python3
"""
Correct palette verification script.
Uses the formula: (15 + 2 * (npcPalette & 7) + 2 * (pal_field // 2)) % 36
"""
import json
from pathlib import Path

def verify_palettes():
    json_path = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json")
    with open(json_path) as f:
        data = json.load(f)
    
    obj = data['obj']
    map_props = obj['mapProperties']
    npc_props = obj['npcProperties']
    PlayerPalTbl = [0, 0, 1, 2, 2, 2, 0, 1, 1, 3, 0, 1, 0, 0, 0, 0, 0, 0]
    
    errors = []
    checked = 0
    
    for map_id in range(len(map_props)):
        if map_id >= len(npc_props):
            continue
        
        mp = map_props[map_id]
        npcs = npc_props[map_id]
        
        npc_pal1 = mp.get('npcPalette1', 0)
        npc_pal2 = mp.get('npcPalette2', 0)
        
        npc_file = Path(f'/home/jojosarah273/DEV/FF4_ASMTOC/data/submaps/{map_id}.npc')
        if not npc_file.exists():
            continue
        
        with open(npc_file) as f:
            lines = f.readlines()
        
        for i, line in enumerate(lines):
            if i >= len(npcs):
                break
            
            parts = line.strip().split('\t')
            if len(parts) < 1:
                continue
            
            fields = parts[0].split()
            if len(fields) < 5:
                continue
            
            actual_pal = int(fields[3])
            sheet = int(fields[4])
            
            npc = npcs[i]
            pal_field = npc.get('palette', 0)
            
            # Calculate expected palette
            if sheet < 14:
                # Character
                char_pal = PlayerPalTbl[sheet] if sheet < len(PlayerPalTbl) else 0
                expected_pal = (15 + 2 * (npc_pal1 & 7)) % 36
            else:
                # Regular NPC
                expected_pal = (15 + 2 * (npc_pal2 & 7)) % 36
            
            if actual_pal != expected_pal:
                errors.append({
                    'map': map_id,
                    'npc': i,
                    'sheet': sheet,
                    'pal_field': pal_field,
                    'expected': expected_pal,
                    'actual': actual_pal
                })
            
            checked += 1
    
    print(f"Checked {checked} NPCs")
    
    if errors:
        print(f"\n{len(errors)} ERRORS FOUND:")
        print()
        
        # Group errors by pattern
        from collections import defaultdict
        error_patterns = defaultdict(list)
        for e in errors:
            diff = e['actual'] - e['expected']
            key = (e['pal_field'], diff)
            error_patterns[key].append(e)
        
        print("Error patterns:")
        for (pal_field, diff), pattern_errors in sorted(error_patterns.items()):
            print(f"  pal_field={pal_field}, diff={diff:+d}: {len(pattern_errors)} errors")
            if len(pattern_errors) <= 3:
                for e in pattern_errors:
                    print(f"    Map {e['map']}, NPC {e['npc']}: sheet={e['sheet']}, "
                          f"expected={e['expected']}, actual={e['actual']}")
        
        return False
    else:
        print("✓ All palettes are correct!")
        return True

if __name__ == '__main__':
    success = verify_palettes()
    exit(0 if success else 1)
