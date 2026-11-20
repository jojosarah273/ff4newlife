#!/usr/bin/env python3
"""
Forensic analysis of what went wrong in palette debugging.
"""
import json
from pathlib import Path

print("=" * 80)
print("FORENSIC ANALYSIS OF PALETTE DEBUGGING ERRORS")
print("=" * 80)
print()

print("MISTAKES MADE:")
print("-" * 80)
print()
print("1. RELIED ON INCONSISTENT .npc FILES")
print("   - .npc files were generated at different times with different formulas")
print("   - Used these as 'ground truth' when they were actually corrupted data")
print("   - This led to circular reasoning and wrong conclusions")
print()
print("2. TRIED FORMULAS WITHOUT VERIFICATION")
print("   - Tested base values: 13, 14, 15, 16, 17")
print("   - Settled on 15 because it had 'fewest errors'")
print("   - But 'errors' were measured against corrupted .npc files!")
print()
print("3. NEVER CHECKED ACTUAL ROM DATA")
print("   - The ROM contains the ACTUAL palette data")
print("   - We should have extracted it directly from ROM")
print("   - Instead, we reverse-engineered from corrupted intermediate files")
print()
print("4. ASSUMED FORMULA STRUCTURE WITHOUT PROOF")
print("   - Assumed: (base + 2 * (npcPalette & 7) + offset) % 36")
print("   - Never verified this against actual ROM behavior")
print()
print("5. IGNORED SNES HARDWARE REALITY")
print("   - LoadNPCPal loads 32 bytes (2 palettes)")
print("   - First 16 bytes go to one CGRAM location")
print("   - Second 16 bytes go to another CGRAM location")
print("   - We never verified which CGRAM locations!")
print()

print("=" * 80)
print("CORRECT APPROACH:")
print("=" * 80)
print()
print("1. Extract ACTUAL palette data from ROM")
print("2. Extract ACTUAL map properties from ROM")
print("3. Extract ACTUAL NPC properties from ROM")
print("4. Determine exact formula by analyzing ROM behavior")
print("5. Verify against reference screenshots")
print()

# Now let's do it correctly
print("=" * 80)
print("STEP 1: Extract ACTUAL data from ROM")
print("=" * 80)
print()

ROM_PATH = Path("/home/jojosarah273/DEV/FF4_ASMTOC/ff2.smc")
with open(ROM_PATH, 'rb') as f:
    rom = f.read()
    if len(rom) % 0x400 == 0x200:
        rom = rom[0x200:]  # Remove header

print(f"ROM size: {len(rom)} bytes ({len(rom)/1024:.1f} KB)")
print()

# Extract MapSpritePal from ROM
# From ASM: MapSpritePal is at bank $15, offset $C700
# File offset = $15 * $8000 + $C700 - $C000 = $A8700
map_sprite_pal_offset = 0xA8700
map_sprite_pal_size = 0x240  # 576 bytes = 36 palettes × 16 bytes

map_sprite_pal = rom[map_sprite_pal_offset:map_sprite_pal_offset + map_sprite_pal_size]

print(f"MapSpritePal extracted from ROM offset 0x{map_sprite_pal_offset:X}")
print(f"Size: {len(map_sprite_pal)} bytes")
print(f"Number of palettes: {len(map_sprite_pal) // 16}")
print()

# Extract a few palettes to verify
print("First 5 palettes from ROM:")
for i in range(5):
    pal_data = map_sprite_pal[i*16:(i+1)*16]
    # Decode first 4 colors (8 bytes)
    colors = []
    for j in range(4):
        bgr = pal_data[j*2] | (pal_data[j*2+1] << 8)
        r = ((bgr >> 0) & 0x1F) * 8
        g = ((bgr >> 5) & 0x1F) * 8
        b = ((bgr >> 10) & 0x1F) * 8
        colors.append((r, g, b))
    print(f"  Palette {i:2d}: {colors}")

print()
print("✓ Step 1 complete: Extracted actual palette data from ROM")

