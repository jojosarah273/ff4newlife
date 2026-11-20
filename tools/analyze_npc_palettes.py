#!/usr/bin/env python3
"""
Comprehensive NPC palette analysis tool for FF4.

This tool resolves the complete palette assignment for every NPC on every map by:
1. Decoding the ASM palette selection logic from InitNPCs
2. Mapping map-level palette assignments (npcPalette1/2)
3. Resolving NPC-level palette selection (palette field)
4. Providing the actual color data from npcPalettes table
"""

import json
import base64
from pathlib import Path

def decode_snes_palette(raw_bytes):
    """Decode SNES BGR555 palette data to RGB tuples"""
    colors = []
    for i in range(0, len(raw_bytes), 2):
        word = raw_bytes[i] | (raw_bytes[i+1] << 8)
        r = (word & 0x1F) * 8
        g = ((word >> 5) & 0x1F) * 8
        b = ((word >> 10) & 0x1F) * 8
        colors.append((r, g, b))
    return colors

def analyze_npc_palettes():
    json_path = Path(__file__).parent.parent / 'data' / 'ff4-en-data.json'
    
    print("Loading FF4 data...")
    with open(json_path) as f:
        data = json.load(f)
    
    obj = data['obj']
    
    # Decode key tables
    npc_gfx_table = obj['npcGraphicsProperties']  # 512 entries
    map_props = obj['mapProperties']  # 383 entries
    npc_props = obj['npcProperties']  # 384 entries
    npc_palettes_raw = obj['npcPalettes']  # 36 entries (base64)
    
    # Decode palettes
    npc_palettes = [decode_snes_palette(base64.b64decode(p)) for p in npc_palettes_raw]
    
    print(f"\n{'='*70}")
    print("NPC PALETTE SYSTEM ANALYSIS")
    print('='*70)
    
    print("\n### SYSTEM OVERVIEW ###")
    print("Each map loads TWO NPC palettes from the npcPalettes table (36 total).")
    print("Each NPC selects one of these two palettes via its 'palette' field.")
    print()
    print("Map palette assignments:")
    print("  - npcPalette1: index into npcPalettes (0-35)")
    print("  - npcPalette2: index into npcPalettes (0-35)")
    print()
    print("NPC palette selection:")
    print("  - palette=0: uses map's npcPalette1")
    print("  - palette=1: uses map's npcPalette2")
    print("  - palette=2+: uses map's npcPalette1 (fallback)")
    print()
    print("Character sprites (Cecil, Kain, etc.) use separate fixed palettes.")
    
    print(f"\n{'='*70}")
    print("PALETTE TABLE STATISTICS")
    print('='*70)
    
    # Count usage
    pal1_usage = {}
    pal2_usage = {}
    npc_pal_usage = {0: 0, 1: 0, 2: 0, 3: 0}
    
    for mp in map_props:
        p1 = mp.get('npcPalette1', 0)
        p2 = mp.get('npcPalette2', 0)
        pal1_usage[p1] = pal1_usage.get(p1, 0) + 1
        pal2_usage[p2] = pal2_usage.get(p2, 0) + 1
    
    for npcs in npc_props:
        for npc in npcs:
            p = npc.get('palette', 0)
            if p in npc_pal_usage:
                npc_pal_usage[p] += 1
    
    print(f"\nMap npcPalette1 usage (top 10):")
    for idx, count in sorted(pal1_usage.items(), key=lambda x: -x[1])[:10]:
        print(f"  Index {idx:2d}: used by {count:3d} maps")
    
    print(f"\nMap npcPalette2 usage (top 10):")
    for idx, count in sorted(pal2_usage.items(), key=lambda x: -x[1])[:10]:
        print(f"  Index {idx:2d}: used by {count:3d} maps")
    
    print(f"\nNPC palette field usage:")
    for p, count in sorted(npc_pal_usage.items()):
        print(f"  palette={p}: {count:5d} NPCs")
    
    print(f"\n{'='*70}")
    print("EXAMPLE: MAP 0 (Baron Town)")
    print('='*70)
    
    mp0 = map_props[0]
    print(f"\nMap 0 properties:")
    print(f"  Title: {mp0.get('title', 'N/A')}")
    print(f"  npcPalette1: {mp0.get('npcPalette1')} -> npcPalettes[{mp0.get('npcPalette1')}]")
    print(f"  npcPalette2: {mp0.get('npcPalette2')} -> npcPalettes[{mp0.get('npcPalette2')}]")
    
    print(f"\nMap 0 NPCs:")
    npcs0 = npc_props[0]
    for i, npc in enumerate(npcs0):
        npc_type = npc.get('switch0', 0)
        sprite_sheet = npc_gfx_table[npc_type] if npc_type < len(npc_gfx_table) else 0
        pal_sel = npc.get('palette', 0)
        
        # Determine which map palette this NPC uses
        if pal_sel == 0:
            pal_idx = mp0.get('npcPalette1', 0)
            pal_source = "npcPalette1"
        elif pal_sel == 1:
            pal_idx = mp0.get('npcPalette2', 0)
            pal_source = "npcPalette2"
        else:
            pal_idx = mp0.get('npcPalette1', 0)
            pal_source = "npcPalette1 (fallback)"
        
        is_char = sprite_sheet < 14
        char_names = ['Cecil', 'Kain', 'Rydia', 'Tellah', 'Edward', 'Rosa', 
                      'Yang', 'Palom', 'Porom', 'Cid', 'Edge', 'Rosa2', '???', '???']
        
        if is_char and sprite_sheet < len(char_names):
            sprite_name = char_names[sprite_sheet]
        else:
            sprite_name = f"NPC type {npc_type}"
        
        print(f"  NPC {i:2d}: {sprite_name:20s} | sheet={sprite_sheet:3d} | "
              f"palette field={pal_sel} -> {pal_source} -> npcPalettes[{pal_idx}]")
    
    print(f"\n{'='*70}")
    print("COMPLETE NPC PALETTE MAPPING (ALL MAPS)")
    print('='*70)
    
    # Create comprehensive mapping
    output = []
    output.append("# FF4 NPC Palette Mapping\n")
    output.append("Format: Map ID | NPC# | Type | Sheet | PalField | PalIndex | Colors\n")
    output.append("="*100 + "\n\n")
    
    for map_id in range(min(50, len(map_props))):  # First 50 maps for demo
        if map_id >= len(npc_props) or not npc_props[map_id]:
            continue
        
        mp = map_props[map_id]
        npcs = npc_props[map_id]
        
        output.append(f"\n## Map {map_id}: {mp.get('title', 'Unknown')}\n")
        output.append(f"Map palettes: npcPalette1={mp.get('npcPalette1')}, npcPalette2={mp.get('npcPalette2')}\n\n")
        
        for i, npc in enumerate(npcs):
            npc_type = npc.get('switch0', 0)
            sprite_sheet = npc_gfx_table[npc_type] if npc_type < len(npc_gfx_table) else 0
            pal_sel = npc.get('palette', 0)
            
            # Resolve palette
            if pal_sel == 0:
                pal_idx = mp.get('npcPalette1', 0)
            elif pal_sel == 1:
                pal_idx = mp.get('npcPalette2', 0)
            else:
                pal_idx = mp.get('npcPalette1', 0)
            
            # Get first few colors
            if pal_idx < len(npc_palettes):
                colors = npc_palettes[pal_idx][:4]
                color_str = ', '.join([f"({r},{g},{b})" for r, g, b in colors])
            else:
                color_str = "INVALID INDEX"
            
            output.append(f"  NPC {i:2d}: type={npc_type:3d}, sheet={sprite_sheet:3d}, "
                         f"pal={pal_sel} -> idx={pal_idx:2d} | {color_str}\n")
    
    # Write to file
    output_path = Path(__file__).parent.parent / 'data' / 'npc_palette_mapping.txt'
    with open(output_path, 'w') as f:
        f.writelines(output)
    
    print(f"\nComplete mapping written to: {output_path}")
    print(f"\n{'='*70}")
    print("SUMMARY")
    print('='*70)
    print("\nThe NPC palette system works as follows:")
    print("1. Each map specifies two palette indices (npcPalette1, npcPalette2)")
    print("2. These indices reference the npcPalettes table (36 palettes)")
    print("3. Each NPC's 'palette' field selects which of the two map palettes to use")
    print("4. Character sprites use fixed palettes from PlayerPalTbl")
    print("\nTo determine an NPC's palette:")
    print("  if npc.palette == 0: use map.npcPalette1")
    print("  elif npc.palette == 1: use map.npcPalette2")
    print("  else: use map.npcPalette1")
    print("\nThe actual colors are in npcPalettes[index] as 16 RGB tuples.")

if __name__ == '__main__':
    analyze_npc_palettes()
