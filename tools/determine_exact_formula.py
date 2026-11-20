#!/usr/bin/env python3
"""
Determine the EXACT palette formula using ground truth from JSON.
The JSON data was extracted from the ROM disassembly, so it's accurate.
"""
import json
from pathlib import Path
from collections import defaultdict

JSON_PATH = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json")

print("=" * 80)
print("DETERMINING EXACT FORMULA FROM GROUND TRUTH")
print("=" * 80)
print()

with open(JSON_PATH) as f:
    data = json.load(f)

obj = data['obj']
map_props = obj['mapProperties']
npc_props = obj['npcProperties']
npc_gfx = obj['npcGraphicsProperties']

# Collect all (npcPalette1/2, pal_field) -> actual palette mappings
# from the JSON data (which is ground truth from ROM)

# We need to know what palette each NPC SHOULD have
# The JSON doesn't directly say this, but we can infer it from the
# ASM code behavior and the MapSpritePal table

# From ASM: LoadNPCPal loads from MapSpritePal + 13*16 + X
# where X = (npcPalette & 7) * 32

# This means:
# - npcPalette=0: loads from entry 13
# - npcPalette=1: loads from entry 15
# - npcPalette=2: loads from entry 17
# etc.

# Each LoadNPCPal call loads 32 bytes (2 palettes)
# First 16 bytes -> CGRAM slot 0 (palette index base)
# Second 16 bytes -> CGRAM slot 1 (palette index base+1)

# For characters: use npcPalette1, load to CGRAM 0-3
# For NPCs: use npcPalette2, load to CGRAM 4-7

# Within each group:
# pal_field=0 -> first palette (base)
# pal_field=1 -> second palette (base+1)
# pal_field=2 -> first palette (base)
# pal_field=3 -> second palette (base+1)

print("HYPOTHESIS: pal_field selects between base and base+1")
print("-" * 80)
print()

# Test this hypothesis against the current .npc files
# to see if it matches

PlayerPalTbl = [0, 0, 1, 2, 2, 2, 0, 1, 1, 3, 0, 1, 0, 0, 0, 0, 0, 0]

def calculate_palette(npcPalette, pal_field, is_character=False):
    """Calculate palette index based on hypothesis."""
    # Base entry from MapSpritePal
    base = 15 + 2 * (npcPalette & 7)  # Start at 15 (empirical)
    
    if is_character:
        # Characters use PlayerPalTbl to select within their range
        # But the exact mapping is unclear
        return base + pal_field // 2
    else:
        # NPCs: pal_field selects between base and base+1
        # pal_field=0,2 -> base
        # pal_field=1,3 -> base+1
        # But wait, let me check the pattern...
        
        # From earlier analysis:
        # pal_field=0,1: use base+1
        # pal_field=2,3: use base+0
        # This is: (1 - pal_field // 2)
        
        return (base + (1 - pal_field // 2)) % 36

# Now let's check what the current .npc files have
# and see if they match our hypothesis

npc_file = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/submaps/0.npc")
with open(npc_file) as f:
    lines = f.readlines()

print("Checking Baron (map 0) NPCs:")
print()
print("npcPalette1=0, npcPalette2=1")
print("Expected base for NPCs: 15 + 2*1 = 17")
print()

baron_map = map_props[0]
baron_npcs = npc_props[0]

print(f"{'NPC':>3} | {'sheet':>5} | {'pal_field':>9} | {'actual':>6} | {'hyp1':>6} | {'hyp2':>6}")
print("-" * 70)

for i, line in enumerate(lines[:10]):
    if i >= len(baron_npcs):
        break
    
    parts = line.strip().split('\t')
    if len(parts) < 1:
        continue
    
    fields = parts[0].split()
    if len(fields) < 5:
        continue
    
    actual_pal = int(fields[3])
    sheet = int(fields[4])
    
    npc = baron_npcs[i]
    pal_field = npc.get('palette', 0)
    
    # Calculate with both hypotheses
    base = 15 + 2 * (baron_map['npcPalette2'] & 7)
    hyp1 = (base + pal_field // 2) % 36  # Current formula
    hyp2 = (base + (1 - pal_field // 2)) % 36  # Alternative
    
    print(f"{i:3d} | {sheet:5d} | {pal_field:9d} | {actual_pal:6d} | {hyp1:6d} | {hyp2:6d}")

print()
print("Analysis:")
print("-" * 80)

# Count matches for each hypothesis
matches_hyp1 = 0
matches_hyp2 = 0
total = 0

for map_id in range(min(50, len(map_props))):
    if map_id >= len(npc_props):
        continue
    
    mp = map_props[map_id]
    npcs = npc_props[map_id]
    
    npc_file = Path(f'/home/jojosarah273/DEV/FF4_ASMTOC/data/submaps/{map_id}.npc')
    if not npc_file.exists():
        continue
    
    with open(npc_file) as f:
        lines = f.readlines()
    
    for i, line in enumerate(lines):
        if i >= len(npcs):
            break
        
        parts = line.strip().split('\t')
        if len(parts) < 1 or len(parts[0].split()) < 5:
            continue
        
        fields = parts[0].split()
        actual_pal = int(fields[3])
        sheet = int(fields[4])
        
        if sheet >= 14:  # Only NPCs
            npc = npcs[i]
            pal_field = npc.get('palette', 0)
            
            base = 15 + 2 * (mp['npcPalette2'] & 7)
            hyp1 = (base + pal_field // 2) % 36
            hyp2 = (base + (1 - pal_field // 2)) % 36
            
            if actual_pal == hyp1:
                matches_hyp1 += 1
            if actual_pal == hyp2:
                matches_hyp2 += 1
            total += 1

print(f"Tested {total} NPCs across 50 maps")
print()
print(f"Hypothesis 1 (pal_field // 2):          {matches_hyp1:3d}/{total} matches ({100*matches_hyp1/total:.1f}%)")
print(f"Hypothesis 2 (1 - pal_field // 2):      {matches_hyp2:3d}/{total} matches ({100*matches_hyp2/total:.1f}%)")
print()

if matches_hyp1 > matches_hyp2:
    print("✓ Hypothesis 1 is BETTER (current formula)")
    print("  Formula: (15 + 2*(npcPalette2 & 7) + pal_field // 2) % 36")
else:
    print("✓ Hypothesis 2 is BETTER")
    print("  Formula: (15 + 2*(npcPalette2 & 7) + (1 - pal_field // 2)) % 36")

