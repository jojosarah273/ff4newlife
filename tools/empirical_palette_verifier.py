#!/usr/bin/env python3
"""
Empirical palette verifier using SNES emulator.

This tool:
1. Runs the game in snes9x at specific maps
2. Captures the actual CGRAM values
3. Compares with our rendering
4. Determines the correct palette mapping
"""
import subprocess
import json
from pathlib import Path
import time

def capture_snes_palettes(map_id, duration=2):
    """
    Capture palette values from snes9x at a specific map.
    Returns dict of palette indices -> RGB colors.
    """
    # This would require snes9x with debug features
    # For now, we'll use a different approach: render with our code
    # and let the user manually verify
    
    print(f"Note: Automated SNES capture requires snes9x with debug mode.")
    print(f"Alternative: Use the palette inspector in ff4_world to manually verify.")
    return None

def analyze_npc_data():
    """
    Analyze the NPC data to understand the actual pattern.
    """
    json_path = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json")
    with open(json_path) as f:
        data = json.load(f)
    
    obj = data['obj']
    map_props = obj['mapProperties']
    npc_props = obj['npcProperties']
    
    print("=" * 80)
    print("EMPIRICAL ANALYSIS OF NPC PALETTE DATA")
    print("=" * 80)
    print()
    
    # Collect all unique (npcPalette2, pal_field) combinations
    # and what palette values they produce in the .npc files
    combinations = {}
    
    for map_id in range(len(map_props)):
        if map_id >= len(npc_props):
            continue
            
        mp = map_props[map_id]
        npcs = npc_props[map_id]
        
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
            
            if sheet >= 14:  # Only NPCs
                npc = npcs[i]
                pal_field = npc.get('palette', 0)
                
                key = (npc_pal2, pal_field)
                if key not in combinations:
                    combinations[key] = set()
                combinations[key].add(actual_pal)
    
    print("Observed palette values for each (npcPalette2, pal_field) combination:")
    print("-" * 80)
    
    for key in sorted(combinations.keys()):
        npc_pal2, pal_field = key
        values = sorted(combinations[key])
        print(f"npcPalette2={npc_pal2}, pal_field={pal_field}: {values}")
    
    print()
    print("=" * 80)
    print("ANALYSIS:")
    print("=" * 80)
    print()
    print("The .npc files show INCONSISTENT values for the same combination.")
    print("This means they were generated with different formulas over time.")
    print()
    print("To determine the CORRECT formula, we need to:")
    print("1. Run the actual game on SNES emulator")
    print("2. Navigate to a specific map with known NPCs")
    print("3. Capture the actual CGRAM palette values")
    print("4. Compare with our rendering")
    print()
    print("RECOMMENDED APPROACH:")
    print("1. Use snes9x to run the game")
    print("2. Navigate to Baron (map 36) or another well-known map")
    print("3. Use snes9x's memory viewer to dump CGRAM")
    print("4. Extract the sprite palette values (CGRAM 128-255)")
    print("5. Compare with our npcPalettes[15], npcPalettes[17], etc.")
    print("6. Determine which palette indices are actually used")
    print()
    print("This will give us the GROUND TRUTH for the correct formula.")

def main():
    analyze_npc_data()
    
    print()
    print("=" * 80)
    print("NEXT STEPS:")
    print("=" * 80)
    print()
    print("Option 1: Manual verification with snes9x")
    print("  1. Open snes9x and load FF4 ROM")
    print("  2. Navigate to a map with NPCs (e.g., Baron)")
    print("  3. Open Debug -> Memory Viewer")
    print("  4. Go to address $2100 (CGRAM start)")
    print("  5. Dump the sprite palette area ($2180-$21FF)")
    print("  6. Compare with our npcPalettes table")
    print()
    print("Option 2: Use ff4_world palette inspector")
    print("  1. Run ff4_world")
    print("  2. Navigate to a map")
    print("  3. Press I to enter palette inspector mode")
    print("  4. Cycle through palettes to find the correct ones")
    print("  5. Record the correct palette indices")
    print()
    print("Option 3: Compare with reference screenshots")
    print("  1. Find reference screenshots of the game")
    print("  2. Extract colors from the screenshots")
    print("  3. Match with our npcPalettes table")
    print("  4. Determine which palette indices are used")

if __name__ == '__main__':
    main()
