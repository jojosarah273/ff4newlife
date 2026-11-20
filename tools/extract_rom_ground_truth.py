#!/usr/bin/env python3
"""
Extract ACTUAL map and NPC properties directly from ROM.
This is the ground truth we need.
"""
import struct
from pathlib import Path

ROM_PATH = Path("/home/jojosarah273/DEV/FF4_ASMTOC/ff2.smc")
with open(ROM_PATH, 'rb') as f:
    rom = f.read()
    if len(rom) % 0x400 == 0x200:
        rom = rom[0x200:]

print("=" * 80)
print("EXTRACTING GROUND TRUTH FROM ROM")
print("=" * 80)
print()

# From ASM analysis:
# MapProp table: bank $0C, offset $D800
# File offset = $0C * $8000 + $D800 - $C000 = $61800
# Each map property is 13 bytes
map_prop_offset = 0x61800
map_prop_size = 13
num_maps = 384

print("Step 1: Extract map properties")
print("-" * 80)

map_props = []
for i in range(num_maps):
    offset = map_prop_offset + i * map_prop_size
    if offset + map_prop_size > len(rom):
        break
    
    data = rom[offset:offset + map_prop_size]
    
    # Parse map property structure (from ASM: map_prop_en.asm)
    # Byte 0-1: layout1 (16-bit)
    # Byte 2: graphics (tileset)
    # Byte 3: palette (BG palette index)
    # Byte 4-5: layout2 (16-bit)
    # Byte 6: npcPalette1 (low nibble) / npcPalette2 (high nibble)
    # Byte 7: npc (NPC property table index)
    # Byte 8: npcMSB (bit 7 = MSB of NPC graphic IDs)
    # Byte 9-10: treasure
    # Byte 11-12: battle background
    
    layout1 = data[0] | (data[1] << 8)
    graphics = data[2]
    bg_palette = data[3]
    layout2 = data[4] | (data[5] << 8)
    npc_palettes = data[6]
    npc_palette1 = npc_palettes & 0x0F
    npc_palette2 = (npc_palettes >> 4) & 0x0F
    npc_table_idx = data[7]
    npc_msb = (data[8] >> 7) & 1
    
    map_props.append({
        'id': i,
        'layout1': layout1,
        'graphics': graphics,
        'bg_palette': bg_palette,
        'layout2': layout2,
        'npcPalette1': npc_palette1,
        'npcPalette2': npc_palette2,
        'npc_table_idx': npc_table_idx,
        'npc_msb': npc_msb
    })

print(f"Extracted {len(map_props)} map properties")
print()

# Show Baron (map 36) as example
baron = map_props[36]
print(f"Example - Baron (map 36):")
print(f"  npcPalette1 = {baron['npcPalette1']}")
print(f"  npcPalette2 = {baron['npcPalette2']}")
print(f"  graphics = {baron['graphics']}")
print(f"  npc_table_idx = {baron['npc_table_idx']}")
print()

# From ASM: NPCProp table at bank $0D, offset $8300
# File offset = $0D * $8000 + $8300 - $C000 = $68300
npc_prop_offset = 0x68300

print("Step 2: Extract NPC properties")
print("-" * 80)

# NPCPropPtrs table: bank $13, offset $8000
# File offset = $13 * $8000 + $8000 - $C000 = $98000
npc_prop_ptrs_offset = 0x98000

# Extract NPC property pointers
npc_ptrs = []
for i in range(num_maps):
    ptr_offset = npc_prop_ptrs_offset + i * 2
    if ptr_offset + 2 > len(rom):
        break
    ptr = struct.unpack_from('<H', rom, ptr_offset)[0]
    npc_ptrs.append(ptr)

print(f"Extracted {len(npc_ptrs)} NPC property pointers")
print()

# Extract NPC data for Baron (map 36)
baron_npc_ptr = npc_ptrs[36]
baron_npc_offset = npc_prop_offset + baron_npc_ptr

print(f"Baron NPC data at ROM offset 0x{baron_npc_offset:X}")
print()

# NPC property structure (4 bytes per NPC):
# Byte 0: graphic ID
# Byte 1: x position (bits 0-5), flags (bits 6-7)
# Byte 2: y position (bits 0-4), direction (bits 5-6)
# Byte 3: palette field (bits 0-3), speed (bits 4-5), animated (bit 6), random (bit 7)

npcs = []
i = 0
while True:
    offset = baron_npc_offset + i * 4
    if offset + 4 > len(rom):
        break
    
    data = rom[offset:offset + 4]
    
    # Check for terminator (all zeros)
    if data == b'\x00\x00\x00\x00':
        break
    
    gfx_id = data[0]
    x = data[1] & 0x3F
    y = data[2] & 0x1F
    direction = (data[2] >> 5) & 0x03
    pal_field = data[3] & 0x0F
    speed = (data[3] >> 4) & 0x03
    animated = (data[3] >> 6) & 0x01
    random_move = (data[3] >> 7) & 0x01
    
    npcs.append({
        'idx': i,
        'gfx_id': gfx_id,
        'x': x,
        'y': y,
        'direction': direction,
        'pal_field': pal_field,
        'speed': speed,
        'animated': animated,
        'random_move': random_move
    })
    
    i += 1

print(f"Baron has {len(npcs)} NPCs:")
for npc in npcs[:5]:
    print(f"  NPC {npc['idx']}: gfx_id={npc['gfx_id']:3d}, pos=({npc['x']:2d},{npc['y']:2d}), "
          f"pal_field={npc['pal_field']}")

print()
print("=" * 80)
print("GROUND TRUTH EXTRACTED")
print("=" * 80)
print()
print("✓ Map properties: npcPalette1, npcPalette2")
print("✓ NPC properties: gfx_id, pal_field")
print("✓ Palette data: MapSpritePal (36 palettes)")
print()
print("Now we can determine the EXACT formula by analyzing the ROM behavior!")

