#!/usr/bin/env python3
"""
DEFINITIVE NPC PALETTE RESOLVER FOR FF4

This tool resolves the exact palette used by any NPC or character on any map.

PALETTE SYSTEM SUMMARY:
=======================

The SNES code loads TWO palette groups per map:
1. npcPalette1 -> Character palettes (party members)
2. npcPalette2 -> NPC palettes (everyone else)

Each palette group consists of 4 consecutive 8-color palettes from the
npcPalettes table (36 total entries).

CHARACTERS (Party Members):
---------------------------
- Palette selection: PlayerPalTbl[char_id] = 0-3
- Character ID mapping:
  0=Cecil, 1=Kain, 2=Rydia, 3=Tellah, 4=Edward, 5=Rosa
  6=Yang, 7=Palom, 8=Porom, 9=Cid, 10=Edge, 11=Rosa(?), 12-17=others
- PlayerPalTbl = [0, 0, 1, 2, 2, 2, 0, 1, 1, 3, 0, 1, 0, 0, 0, 0, 0, 0]
- Base palette entry = (npcPalette1 & 7) * 2 + 13
- Final palette entry = base + PlayerPalTbl[char_id]

NPCs (Non-Character Sprites):
-----------------------------
- Palette selection: npc.palette field = 0-3
- Base palette entry = (npcPalette2 & 7) * 2 + 13
- Final palette entry = base + npc.palette

TECHNICAL DETAILS:
==================
The LoadNPCPal routine at npc.asm:c2bc performs:
1. Address calculation: base_offset = (palette_index & 7) * 32 + 208
2. Copies 16 bytes (8 colors) from MapSpritePal
3. Zeros next 16 bytes (for 16-color SNES palette slots)
4. Repeats 4 times to load all 4 palettes

The buffer layout at $0E5B (128 bytes = 8 palettes × 16 colors each):
- Offset 0x00-0x3F: npcPalette1 group (4 palettes for characters)
- Offset 0x40-0x7F: npcPalette2 group (4 palettes for NPCs)

OAM attribute bits 1-3 select palette 0-7:
- Palettes 0-3 -> characters
- Palettes 4-7 -> NPCs
"""

import json
import base64
from pathlib import Path

# Character palette assignments from sprite.asm:b2fa
PLAYER_PAL_TBL = [0, 0, 1, 2, 2, 2, 0, 1, 1, 3, 0, 1, 0, 0, 0, 0, 0, 0]
CHAR_NAMES = {
    0: "Cecil", 1: "Kain", 2: "Rydia", 3: "Tellah", 4: "Edward", 5: "Rosa",
    6: "Yang", 7: "Palom", 8: "Porom", 9: "Cid", 10: "Edge", 11: "Rosa(?)",
    12: "Kain(?)", 13: "Cecil(?)", 14: "Golbez", 15: "Fusoya", 16: "Edge(?)", 17: "Rosa(?)"
}

def load_data():
    """Load FF4 data from JSON"""
    json_path = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json")
    with open(json_path, 'r') as f:
        data = json.load(f)
    return data['obj']

def decode_palette(base64_str):
    """Decode base64 palette to list of RGB tuples"""
    raw = base64.b64decode(base64_str)
    colors = []
    for i in range(0, len(raw), 2):
        if i + 1 < len(raw):
            # BGR555 format: 5 bits blue, 5 bits green, 5 bits red
            bgr = raw[i] | (raw[i+1] << 8)
            r = ((bgr >> 0) & 0x1F) * 8
            g = ((bgr >> 5) & 0x1F) * 8
            b = ((bgr >> 10) & 0x1F) * 8
            colors.append((r, g, b))
    return colors

def resolve_character_palette(map_id, char_id, obj_data):
    """
    Resolve the palette for a character on a specific map.
    
    Args:
        map_id: Map ID (0-383)
        char_id: Character ID (0-17)
        obj_data: The 'obj' dict from ff4-en-data.json
    
    Returns:
        dict with palette_index, palette_entry, and colors
    """
    if map_id >= len(obj_data['mapProperties']):
        raise ValueError(f"Invalid map_id: {map_id}")
    
    if char_id >= len(PLAYER_PAL_TBL):
        raise ValueError(f"Invalid char_id: {char_id}")
    
    map_props = obj_data['mapProperties'][map_id]
    npcPalette1 = map_props['npcPalette1']
    
    # Base palette entry calculation
    base_entry = (npcPalette1 & 7) * 2 + 13
    pal_offset = PLAYER_PAL_TBL[char_id]
    final_entry = base_entry + pal_offset
    
    # Get palette colors
    if final_entry < len(obj_data['npcPalettes']):
        colors = decode_palette(obj_data['npcPalettes'][final_entry])
    else:
        colors = []
    
    return {
        'type': 'character',
        'map_id': map_id,
        'char_id': char_id,
        'char_name': CHAR_NAMES.get(char_id, f"Char{char_id}"),
        'npcPalette1': npcPalette1,
        'pal_offset': pal_offset,
        'base_entry': base_entry,
        'palette_entry': final_entry,
        'colors': colors
    }

def resolve_npc_palette(map_id, npc_index, obj_data):
    """
    Resolve the palette for an NPC on a specific map.
    
    Args:
        map_id: Map ID (0-383)
        npc_index: NPC index within the map (0-15)
        obj_data: The 'obj' dict from ff4-en-data.json
    
    Returns:
        dict with palette_index, palette_entry, and colors
    """
    if map_id >= len(obj_data['mapProperties']):
        raise ValueError(f"Invalid map_id: {map_id}")
    
    if map_id >= len(obj_data['npcProperties']):
        raise ValueError(f"No NPC data for map_id: {map_id}")
    
    npcs = obj_data['npcProperties'][map_id]
    if npc_index >= len(npcs):
        raise ValueError(f"Invalid npc_index: {npc_index} (map has {len(npcs)} NPCs)")
    
    npc = npcs[npc_index]
    map_props = obj_data['mapProperties'][map_id]
    npcPalette2 = map_props['npcPalette2']
    
    # Base palette entry calculation
    base_entry = (npcPalette2 & 7) * 2 + 13
    pal_offset = npc['palette']  # 0-3 from NPC property byte 3 bits 2-3
    final_entry = base_entry + pal_offset
    
    # Get palette colors
    if final_entry < len(obj_data['npcPalettes']):
        colors = decode_palette(obj_data['npcPalettes'][final_entry])
    else:
        colors = []
    
    return {
        'type': 'npc',
        'map_id': map_id,
        'npc_index': npc_index,
        'npc_type': npc.get('switch0', 'unknown'),
        'npcPalette2': npcPalette2,
        'pal_offset': pal_offset,
        'base_entry': base_entry,
        'palette_entry': final_entry,
        'colors': colors
    }

def print_palette_info(result):
    """Pretty-print palette resolution result"""
    if result['type'] == 'character':
        print(f"\nCharacter: {result['char_name']} (ID {result['char_id']})")
        print(f"Map: {result['map_id']}")
        print(f"npcPalette1: {result['npcPalette1']}")
        print(f"PlayerPalTbl offset: {result['pal_offset']}")
    else:
        print(f"\nNPC Index: {result['npc_index']}")
        print(f"NPC Type: {result['npc_type']}")
        print(f"Map: {result['map_id']}")
        print(f"npcPalette2: {result['npcPalette2']}")
        print(f"NPC palette field: {result['pal_offset']}")
    
    print(f"Base palette entry: {result['base_entry']}")
    print(f"Final palette entry: {result['palette_entry']}")
    
    if result['colors']:
        print(f"Palette colors ({len(result['colors'])} colors):")
        for i, (r, g, b) in enumerate(result['colors']):
            print(f"  [{i}] RGB({r:3d}, {g:3d}, {b:3d})")
    else:
        print("ERROR: Palette entry out of range!")

def main():
    """Interactive palette resolver"""
    obj_data = load_data()
    
    print("=" * 70)
    print("FF4 NPC PALETTE RESOLVER")
    print("=" * 70)
    
    # Example 1: Cecil on map 0
    print("\n" + "=" * 70)
    print("EXAMPLE 1: Cecil on Map 0 (Baron)")
    print("=" * 70)
    result = resolve_character_palette(0, 0, obj_data)
    print_palette_info(result)
    
    # Example 2: First NPC on map 0
    print("\n" + "=" * 70)
    print("EXAMPLE 2: First NPC on Map 0")
    print("=" * 70)
    result = resolve_npc_palette(0, 0, obj_data)
    print_palette_info(result)
    
    # Interactive mode
    print("\n" + "=" * 70)
    print("INTERACTIVE MODE")
    print("=" * 70)
    
    while True:
        print("\nSelect resolution type:")
        print("1. Resolve character palette")
        print("2. Resolve NPC palette")
        print("3. Show all NPCs on a map")
        print("4. Exit")
        
        choice = input("\nChoice (1-4): ").strip()
        
        if choice == '1':
            map_id = int(input("Map ID (0-383): "))
            char_id = int(input("Character ID (0-17): "))
            result = resolve_character_palette(map_id, char_id, obj_data)
            print_palette_info(result)
        
        elif choice == '2':
            map_id = int(input("Map ID (0-383): "))
            npc_index = int(input("NPC index (0-15): "))
            result = resolve_npc_palette(map_id, npc_index, obj_data)
            print_palette_info(result)
        
        elif choice == '3':
            map_id = int(input("Map ID (0-383): "))
            npcs = obj_data['npcProperties'][map_id]
            print(f"\nAll NPCs on Map {map_id}:")
            print(f"npcPalette1: {obj_data['mapProperties'][map_id]['npcPalette1']}")
            print(f"npcPalette2: {obj_data['mapProperties'][map_id]['npcPalette2']}")
            
            for i in range(len(npcs)):
                result = resolve_npc_palette(map_id, i, obj_data)
                print(f"\n--- NPC {i} ---")
                print(f"Type: {result['npc_type']}")
                print(f"Palette field: {result['pal_offset']}")
                print(f"Final palette entry: {result['palette_entry']}")
                if result['colors']:
                    print(f"First color: RGB{result['colors'][0]}")
        
        elif choice == '4':
            print("\nGoodbye!")
            break
        
        else:
            print("Invalid choice!")

if __name__ == '__main__':
    main()
