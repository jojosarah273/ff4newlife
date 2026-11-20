#!/usr/bin/env python3
"""
Automated palette verification using ff4kster's ROM data.

This tool:
1. Reads MapSpritePal directly from the ROM
2. Reads NPC properties from the ROM for specific maps
3. Computes what palettes SHOULD be used according to the ROM
4. Compares to what we're currently rendering
5. Reports any mismatches
"""

import struct
import json
from pathlib import Path

ROM_PATH = Path("/home/jojosarah273/DEV/FF4_ASMTOC/ff2.smc")
JSON_PATH = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json")

def read_rom():
    """Read ROM file, handling header if present."""
    with open(ROM_PATH, 'rb') as f:
        data = f.read()
    
    # Remove header if present (first 512 bytes)
    if len(data) % 0x400 == 0x200:
        data = data[0x200:]
    
    return data

def extract_map_sprite_pal(rom):
    """Extract MapSpritePal table from ROM."""
    # From the ASM: MapSpritePal is at bank $15, address $C700
    # File offset = (bank << 15) + address - 0xC000
    # = ($15 << 15) + $C700 - $C000
    # = 0xA8000 + 0x700
    # = 0xA8700
    
    offset = 0xA8700
    size = 0x240  # 576 bytes = 36 entries × 16 bytes
    
    pal_data = rom[offset:offset + size]
    
    # Parse as 16-byte entries
    palettes = []
    for i in range(36):
        entry = pal_data[i*16:(i+1)*16]
        # Parse as 8 BGR555 colors
        colors = []
        for j in range(8):
            bgr = entry[j*2] | (entry[j*2+1] << 8)
            r = ((bgr >> 0) & 0x1F) * 8
            g = ((bgr >> 5) & 0x1F) * 8
            b = ((bgr >> 10) & 0x1F) * 8
            colors.append((r, g, b))
        palettes.append(colors)
    
    return palettes

def extract_npc_properties(rom, map_id):
    """Extract NPC properties for a specific map from ROM."""
    # NPCPropPtrs is at $13:8000, NPCProp data at $13:8300
    # File offset for NPCPropPtrs = ($13 << 15) + $8000 - $C000 = 0x98000
    # File offset for NPCProp = ($13 << 15) + $8300 - $C000 = 0x98300
    
    ptr_offset = 0x98000 + map_id * 2
    ptr = struct.unpack_from('<H', rom, ptr_offset)[0]
    
    # ptr is relative to NPCProp base
    npc_offset = 0x98300 + ptr
    
    # Read NPCs until we hit a 0x00 byte (end marker)
    npcs = []
    i = 0
    while True:
        byte0 = rom[npc_offset + i]
        if byte0 == 0x00:
            break
        
        byte1 = rom[npc_offset + i + 1]
        byte2 = rom[npc_offset + i + 2]
        byte3 = rom[npc_offset + i + 3]
        
        npcs.append({
            'gfx_id': byte0,
            'x': byte1 & 0x3F,
            'y': byte2,
            'palette_field': (byte3 & 0x0C) >> 2
        })
        
        i += 4
    
    return npcs

def compute_palette_indices(map_id, npc_palettes_rom):
    """Compute which palette indices should be used for a map."""
    with open(JSON_PATH) as f:
        data = json.load(f)
    
    obj = data['obj']
    map_props = obj['mapProperties'][map_id]
    
    npc_pal1 = map_props.get('npcPalette1', 0)
    npc_pal2 = map_props.get('npcPalette2', 0)
    
    # Based on the ASM analysis:
    # First call (Y=0): loads for characters (base1)
    # Second call (Y=$40): loads for NPCs (base2)
    #
    # Entry = base + 2*(A & 7)
    #
    # The question is: what is the base?
    # ASM says 13, but user says it should be 15 for Baron.
    
    # Let's try different bases and see which one matches
    for base in [13, 14, 15, 16]:
        base1 = (base + 2 * (npc_pal1 & 7)) % 36
        base2 = (base + 2 * (npc_pal2 & 7)) % 36
        
        print(f"  Base {base}: base1={base1}, base2={base2}")
    
    return None

def main():
    print("=== Automated Palette Verification ===\n")
    
    rom = read_rom()
    print(f"Loaded ROM: {len(rom)} bytes\n")
    
    # Extract MapSpritePal from ROM
    npc_palettes_rom = extract_map_sprite_pal(rom)
    print(f"Extracted {len(npc_palettes_rom)} palettes from ROM\n")
    
    # Load JSON palettes for comparison
    with open(JSON_PATH) as f:
        data = json.load(f)
    
    npc_palettes_json = data['obj']['npcPalettes']
    
    # Compare ROM vs JSON palettes
    print("Comparing ROM vs JSON palettes:")
    print("-" * 80)
    
    import base64
    
    mismatches = 0
    for i in range(min(36, len(npc_palettes_rom))):
        rom_colors = npc_palettes_rom[i]
        
        # Decode JSON palette
        json_b64 = npc_palettes_json[i]
        json_raw = base64.b64decode(json_b64)
        json_colors = []
        for j in range(0, min(16, len(json_raw)), 2):
            bgr = json_raw[j] | (json_raw[j+1] << 8)
            r = ((bgr >> 0) & 0x1F) * 8
            g = ((bgr >> 5) & 0x1F) * 8
            b = ((bgr >> 10) & 0x1F) * 8
            json_colors.append((r, g, b))
        
        # Compare first 8 colors
        match = rom_colors == json_colors[:8]
        
        if not match:
            mismatches += 1
            print(f"Palette {i}: MISMATCH")
            print(f"  ROM:  {rom_colors[:4]}")
            print(f"  JSON: {json_colors[:4]}")
    
    print(f"\nTotal mismatches: {mismatches}/{36}")
    
    if mismatches > 0:
        print("\nThe JSON npcPalettes table does NOT match the ROM MapSpritePal!")
        print("This explains why the palettes are wrong.")
        print("\nWe need to use the ROM data directly, not the JSON data.")
    
    # Test Baron (map 0)
    print("\n" + "=" * 80)
    print("Testing Baron (Map 0):")
    print("=" * 80)
    
    compute_palette_indices(0, npc_palettes_rom)
    
    # Extract NPC properties for Baron
    npcs = extract_npc_properties(rom, 0)
    print(f"\nBaron has {len(npcs)} NPCs")
    
    if npcs:
        print("\nFirst 5 NPCs:")
        for i, npc in enumerate(npcs[:5]):
            print(f"  NPC {i}: gfx_id={npc['gfx_id']}, pos=({npc['x']},{npc['y']}), pal_field={npc['palette_field']}")

if __name__ == '__main__':
    main()
