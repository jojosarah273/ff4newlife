#!/usr/bin/env python3
"""
Resolve NPC palettes for FF4 maps.

This tool decodes the palette assignment logic from the SNES ASM (npc.asm InitNPCs):
1. Each NPC has a gfx_id (byte 0 of npcProperties)
2. gfx_id maps to a sprite_sheet via NPCGfxID table
3. Palette is determined by:
   - If sprite_sheet < 14: use PlayerPalTbl[sprite_sheet] (character palettes)
   - Else: use (NPCProp[sprite_sheet+3] & 0x0C) >> 2 + 4 (NPC palettes 4-7)
4. Map has two NPC palettes (npcPalette1/2) for palettes 4-7
"""

import json
import sys
from pathlib import Path

# Player palette table from sprite.asm
PLAYER_PAL_TBL = [0, 0, 1, 2, 2, 2, 0, 1, 1, 3, 0, 1, 0, 0, 0, 0, 0, 0]
PLAYER_NAMES = ['Cecil', 'Kain', 'Rydia', 'Tellah', 'Edward', 'Rosa', 
                'Yang', 'Palom', 'Porom', 'Cid', 'Edge', 'Rosa2']

def load_npc_gfx_id(data):
    """Load NPCGfxID table from JSON data"""
    if 'npcGraphicsProperties' in data:
        return data['npcGraphicsProperties']
    return None

def resolve_npc_palette(npc_gfx_id, sprite_sheet_id, npc_gfx_table, map_npc_pal1, map_npc_pal2):
    """
    Resolve which palette (0-7) an NPC uses.
    
    Args:
        npc_gfx_id: The NPC's gfx_id from npcProperties (0-511)
        sprite_sheet_id: The sprite sheet from NPCGfxID table
        npc_gfx_table: Full NPCGfxID table for palette lookup
        map_npc_pal1: Map's first NPC palette index
        map_npc_pal2: Map's second NPC palette index
    
    Returns:
        tuple: (palette_number, palette_source, palette_index)
            - palette_number: 0-7 (which CGRAM palette)
            - palette_source: 'character' or 'npc'
            - palette_index: index into npcPalettes array
    """
    if sprite_sheet_id < 14:
        # Character sprite
        pal = PLAYER_PAL_TBL[sprite_sheet_id]
        char_name = PLAYER_NAMES[sprite_sheet_id] if sprite_sheet_id < len(PLAYER_NAMES) else f"Char{sprite_sheet_id}"
        return (pal, 'character', char_name)
    else:
        # NPC sprite - palette from NPCProp[gfx_id+3]
        # In the JSON, this is the 'palette' field of the NPC type
        # The formula is: (byte3 & 0x0C) >> 2 + 4
        # This gives us palette 4, 5, 6, or 7
        
        # For now, we need to look this up from the actual NPC properties
        # The JSON should have this info
        return (None, 'npc', None)

def main():
    json_path = Path(__file__).parent.parent / 'data' / 'ff4-en-data.json'
    
    if not json_path.exists():
        print(f"Error: {json_path} not found")
        sys.exit(1)
    
    print("Loading FF4 data...")
    with open(json_path) as f:
        data = json.load(f)
    
    # Check what data we have
    print("\n=== Available Data Keys ===")
    relevant_keys = [k for k in data.keys() if 'npc' in k.lower() or 'pal' in k.lower()]
    for key in sorted(relevant_keys):
        val = data[key]
        if isinstance(val, list):
            print(f"  {key}: list with {len(val)} items")
            if len(val) > 0:
                print(f"    First item type: {type(val[0])}")
                if isinstance(val[0], dict):
                    print(f"    First item keys: {list(val[0].keys())[:10]}")
        elif isinstance(val, dict):
            print(f"  {key}: dict with {len(val)} keys")
            sample_keys = list(val.keys())[:5]
            print(f"    Sample keys: {sample_keys}")
    
    # Look for NPC graphics table
    print("\n=== Looking for NPC Graphics Table ===")
    for key in ['npcGraphicsProperties', 'npcGraphics', 'NPCGfxID']:
        if key in data:
            print(f"Found {key} with {len(data[key])} entries")
            if len(data[key]) > 0:
                print(f"  First few entries: {data[key][:10]}")
    
    # Look for NPC properties
    print("\n=== Looking for NPC Properties ===")
    for key in ['npcProperties', 'npcProps']:
        if key in data:
            val = data[key]
            print(f"Found {key}")
            if isinstance(val, dict):
                print(f"  Maps: {len(val)}")
                sample_map = list(val.keys())[0]
                print(f"  Sample map {sample_map}: {val[sample_map][:3] if isinstance(val[sample_map], list) else val[sample_map]}")
    
    # Look for palettes
    print("\n=== Looking for Palette Data ===")
    for key in ['npcPalettes', 'mapPalettes', 'palettes']:
        if key in data:
            val = data[key]
            print(f"Found {key} with {len(val)} entries")
            if len(val) > 0:
                print(f"  First palette: {val[0]}")

if __name__ == '__main__':
    main()
