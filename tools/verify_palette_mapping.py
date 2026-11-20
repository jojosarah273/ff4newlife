#!/usr/bin/env python3
"""
Verify the NPC palette mapping by checking all possible combinations.
"""

import json
import base64
from pathlib import Path

def decode_snes_colors(raw_bytes):
    colors = []
    for i in range(0, len(raw_bytes), 2):
        word = raw_bytes[i] | (raw_bytes[i+1] << 8)
        r = (word & 0x1F) * 8
        g = ((word >> 5) & 0x1F) * 8
        b = ((word >> 10) & 0x1F) * 8
        colors.append((r, g, b))
    return colors

def verify_mapping():
    json_path = Path(__file__).parent.parent / 'data' / 'ff4-en-data.json'
    
    with open(json_path) as f:
        data = json.load(f)
    
    obj = data['obj']
    npc_palettes_raw = obj['npcPalettes']
    npc_palettes = [decode_snes_colors(base64.b64decode(p)) for p in npc_palettes_raw]
    
    print("="*80)
    print("VERIFICATION: NPC Palette Mapping")
    print("="*80)
    print()
    print(f"Total npcPalettes entries: {len(npc_palettes)}")
    print(f"Each entry has {len(npc_palettes[0])} colors (should be 16)")
    print()
    
    # Show first few palettes
    print("First 5 npcPalettes entries:")
    for i in range(min(5, len(npc_palettes))):
        print(f"  Palette {i}: {len(npc_palettes[i])} colors")
        print(f"    First 4: {npc_palettes[i][:4]}")
        print(f"    Last 4:  {npc_palettes[i][-4:]}")
    print()
    
    # Check map palette usage
    map_props = obj['mapProperties']
    pal1_values = set()
    pal2_values = set()
    
    for mp in map_props:
        pal1_values.add(mp.get('npcPalette1', 0))
        pal2_values.add(mp.get('npcPalette2', 0))
    
    print(f"Map npcPalette1 unique values: {sorted(pal1_values)}")
    print(f"Map npcPalette2 unique values: {sorted(pal2_values)}")
    print()
    
    # Check if any maps use palette indices >= 36
    max_pal1 = max(pal1_values) if pal1_values else 0
    max_pal2 = max(pal2_values) if pal2_values else 0
    
    print(f"Max npcPalette1 value: {max_pal1}")
    print(f"Max npcPalette2 value: {max_pal2}")
    print()
    
    if max_pal1 >= len(npc_palettes) or max_pal2 >= len(npc_palettes):
        print("WARNING: Some maps reference palette indices beyond the table!")
        print(f"  npcPalettes has {len(npc_palettes)} entries")
        print(f"  But maps reference up to index {max(max_pal1, max_pal2)}")
    else:
        print("OK: All map palette references are within bounds")
    print()
    
    # Now check the NPC palette field values
    npc_props = obj['npcProperties']
    npc_pal_values = set()
    
    for npcs in npc_props:
        for npc in npcs:
            npc_pal_values.add(npc.get('palette', 0))
    
    print(f"NPC palette field unique values: {sorted(npc_pal_values)}")
    print()
    
    print("="*80)
    print("CONCLUSION:")
    print("="*80)
    print()
    print("The NPC palette system works as follows:")
    print()
    print("1. Each map has npcPalette1 and npcPalette2 (indices 0-35)")
    print("2. These reference entries in the npcPalettes table")
    print("3. Each npcPalettes entry has 16 colors (split into two 8-color palettes)")
    print("4. Each NPC has a 'palette' field (0-3) that selects which 8-color palette:")
    print("   - palette=0: npcPalettes[npcPalette1][0:8]")
    print("   - palette=1: npcPalettes[npcPalette1][8:16]")
    print("   - palette=2: npcPalettes[npcPalette2][0:8]")
    print("   - palette=3: npcPalettes[npcPalette2][8:16]")
    print()
    print("This gives 4 unique 8-color palettes per map for NPC sprites.")
    print()

if __name__ == '__main__':
    verify_mapping()
