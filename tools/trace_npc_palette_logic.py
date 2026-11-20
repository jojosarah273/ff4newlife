#!/usr/bin/env python3
"""
Trace the exact NPC palette logic from the SNES ASM code.

This tool implements the exact palette selection algorithm from:
- npc.asm InitNPCs (@c198-c28c)
- LoadNPCPal routine
- The relationship between map palettes and NPC palette selection
"""

import json
import base64
from pathlib import Path

# Character palette table from sprite.asm @b2fa
PLAYER_PAL_TBL = [0, 0, 1, 2, 2, 2, 0, 1, 1, 3, 0, 1, 0, 0, 0, 0, 0, 0]
CHAR_NAMES = ['Cecil', 'Kain', 'Rydia', 'Tellah', 'Edward', 'Rosa', 
              'Yang', 'Palom', 'Porom', 'Cid', 'Edge', 'Rosa2']

def decode_snes_colors(raw_bytes):
    """Decode SNES BGR555 palette to RGB tuples"""
    colors = []
    for i in range(0, len(raw_bytes), 2):
        word = raw_bytes[i] | (raw_bytes[i+1] << 8)
        r = (word & 0x1F) * 8
        g = ((word >> 5) & 0x1F) * 8
        b = ((word >> 10) & 0x1F) * 8
        colors.append((r, g, b))
    return colors

def trace_palette_logic():
    json_path = Path(__file__).parent.parent / 'data' / 'ff4-en-data.json'
    
    with open(json_path) as f:
        data = json.load(f)
    
    obj = data['obj']
    npc_gfx_table = obj['npcGraphicsProperties']
    map_props = obj['mapProperties']
    npc_props = obj['npcProperties']
    npc_palettes_raw = obj['npcPalettes']
    
    # Decode all palettes
    npc_palettes = [decode_snes_colors(base64.b64decode(p)) for p in npc_palettes_raw]
    
    print("="*80)
    print("EXACT NPC PALETTE LOGIC TRACE")
    print("="*80)
    print()
    print("From npc.asm InitNPCs (@c198-c28c):")
    print()
    print("For each NPC:")
    print("  1. Load gfx_id from NPCProp byte 0")
    print("  2. Look up sprite_sheet = NPCGfxID[gfx_id]")
    print("  3. Compute preliminary palette based on sprite_sheet:")
    print("     - If sprite_sheet < 0x2E (46):   prelim_pal = 0")
    print("     - If 0x2E ≤ sprite_sheet < 0x30: prelim_pal = 3")
    print("     - If 0x30 ≤ sprite_sheet < 0x46: prelim_pal = 2")
    print("     - If sprite_sheet ≥ 0x46:         prelim_pal = 1")
    print("  4. Final palette selection:")
    print("     - If sprite_sheet < 0x0E (14): CHARACTER")
    print("       final_pal = PlayerPalTbl[sprite_sheet]  (0-3)")
    print("     - Else: NPC")
    print("       final_pal = ((NPCProp_byte3 & 0x0C) >> 2) + 4  (4-7)")
    print()
    print("The NPCProp byte 3 bits 2-3 (masked by 0x0C) select palette 4-7")
    print()
    print("Map palette loading (LoadNPCPal):")
    print("  - Load MapSpritePal[npcPalette1] to CGRAM palettes 4-5")
    print("  - Load MapSpritePal[npcPalette2] to CGRAM palettes 6-7")
    print()
    print("So NPC palette field mapping:")
    print("  palette=0 -> CGRAM palette 4 -> first half of npcPalette1")
    print("  palette=1 -> CGRAM palette 5 -> second half of npcPalette1")
    print("  palette=2 -> CGRAM palette 6 -> first half of npcPalette2")
    print("  palette=3 -> CGRAM palette 7 -> second half of npcPalette2")
    print()
    print("="*80)
    print()
    
    # Now trace through actual maps
    print("TRACING ACTUAL MAPS:")
    print()
    
    for map_id in [0, 36, 100]:
        if map_id >= len(map_props) or map_id >= len(npc_props):
            continue
        
        mp = map_props[map_id]
        npcs = npc_props[map_id]
        
        print(f"Map {map_id}: {mp.get('title', 'Unknown')}")
        print(f"  npcPalette1 = {mp.get('npcPalette1')}")
        print(f"  npcPalette2 = {mp.get('npcPalette2')}")
        print()
        
        for i, npc in enumerate(npcs[:5]):  # First 5 NPCs
            gfx_id = npc.get('switch0', 0)
            sprite_sheet = npc_gfx_table[gfx_id] if gfx_id < len(npc_gfx_table) else 0
            
            # Preliminary palette
            if sprite_sheet < 0x2E:
                prelim = 0
            elif sprite_sheet < 0x30:
                prelim = 3
            elif sprite_sheet < 0x46:
                prelim = 2
            else:
                prelim = 1
            
            # Final palette
            if sprite_sheet < 0x0E:
                # Character
                final_pal = PLAYER_PAL_TBL[sprite_sheet]
                pal_type = "CHARACTER"
                pal_name = CHAR_NAMES[sprite_sheet] if sprite_sheet < len(CHAR_NAMES) else f"Char{sprite_sheet}"
            else:
                # NPC
                pal_field = npc.get('palette', 0)
                final_pal = pal_field + 4
                pal_type = "NPC"
                pal_name = f"palette field={pal_field}"
            
            print(f"  NPC {i}: gfx_id={gfx_id:3d}, sheet={sprite_sheet:3d}, "
                  f"prelim={prelim}, final={final_pal} ({pal_type}: {pal_name})")
        
        print()
    
    print("="*80)
    print("KEY INSIGHT:")
    print("="*80)
    print()
    print("The JSON 'palette' field (0-3) directly maps to NPC palettes 4-7:")
    print("  palette=0 -> CGRAM palette 4 -> npcPalettes[map.npcPalette1][0:8]")
    print("  palette=1 -> CGRAM palette 5 -> npcPalettes[map.npcPalette1][8:16]")
    print("  palette=2 -> CGRAM palette 6 -> npcPalettes[map.npcPalette2][0:8]")
    print("  palette=3 -> CGRAM palette 7 -> npcPalettes[map.npcPalette2][8:16]")
    print()
    print("Each npcPalettes entry has 16 colors (32 bytes).")
    print("The first 8 colors go to one CGRAM palette, the next 8 to another.")
    print()

if __name__ == '__main__':
    trace_palette_logic()
