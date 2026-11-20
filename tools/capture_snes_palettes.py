#!/usr/bin/env python3
"""
Tool to help capture palette data from snes9x and determine the correct formula.

This tool provides:
1. Instructions for capturing CGRAM data from snes9x
2. A parser for the captured data
3. Formula determination logic
4. Automatic .npc file regeneration
"""
import json
from pathlib import Path

def print_instructions():
    print("=" * 80)
    print("SNES PALETTE CAPTURE INSTRUCTIONS")
    print("=" * 80)
    print()
    print("Step 1: Install and run snes9x")
    print("  sudo apt-get install snes9x")
    print("  snes9x /path/to/ff4.smc")
    print()
    print("Step 2: Navigate to a map with NPCs (e.g., Baron, Kaipo)")
    print("  - Use save states or cheats to quickly navigate")
    print("  - Note the map ID (you can find this in the game's memory)")
    print()
    print("Step 3: Open the memory viewer")
    print("  - Press F1 or go to Debug -> Memory Viewer")
    print("  - Navigate to address $2100 (CGRAM start)")
    print()
    print("Step 4: Dump sprite palette data")
    print("  - Sprite palettes are at $2180-$21FF (colors 128-255)")
    print("  - Export 128 bytes (64 colors × 2 bytes)")
    print("  - Save as hex dump or use snes9x's export feature")
    print()
    print("Step 5: Record the following information:")
    print("  - Map ID")
    print("  - npcPalette1 value (from map properties)")
    print("  - npcPalette2 value (from map properties)")
    print("  - For each NPC:")
    print("    * Position (x, y)")
    print("    * Sprite sheet ID")
    print("    * Palette field (0-3)")
    print("    * Actual colors displayed (from CGRAM dump)")
    print()
    print("Step 6: Save the data to a JSON file")
    print("  Format: tools/snes_palette_capture.json")
    print("  See template below")
    print()

def create_template():
    template = {
        "captures": [
            {
                "map_id": 36,
                "map_name": "Baron",
                "npcPalette1": 0,
                "npcPalette2": 1,
                "cgram_dump": "0000FF7F...",  # Hex string of 128 bytes
                "npcs": [
                    {
                        "x": 14,
                        "y": 17,
                        "sheet": 101,
                        "pal_field": 0,
                        "actual_palette_index": 17  # To be determined
                    }
                ]
            }
        ]
    }
    
    template_path = Path("/home/jojosarah273/DEV/FF4_ASMTOC/tools/snes_palette_capture.json")
    if not template_path.exists():
        with open(template_path, 'w') as f:
            json.dump(template, f, indent=2)
        print(f"Created template: {template_path}")
    else:
        print(f"Template already exists: {template_path}")

def analyze_captured_data():
    template_path = Path("/home/jojosarah273/DEV/FF4_ASMTOC/tools/snes_palette_capture.json")
    
    if not template_path.exists():
        print("✗ No capture data found. Please follow the instructions above.")
        return None
    
    with open(template_path) as f:
        data = json.load(f)
    
    print("=" * 80)
    print("ANALYZING CAPTURED DATA")
    print("=" * 80)
    print()
    
    # Parse CGRAM dump and match with npcPalettes table
    json_path = Path("/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json")
    with open(json_path) as f:
        ff4_data = json.load(f)
    
    npc_palettes = ff4_data['obj']['npcPalettes']
    
    # For each capture, determine which palette indices are used
    formula_votes = {}
    
    for capture in data['captures']:
        map_id = capture['map_id']
        npc_pal1 = capture['npcPalette1']
        npc_pal2 = capture['npcPalette2']
        
        print(f"Map {map_id} ({capture.get('map_name', 'Unknown')}):")
        print(f"  npcPalette1 = {npc_pal1}")
        print(f"  npcPalette2 = {npc_pal2}")
        print()
        
        # TODO: Parse CGRAM dump and match with npcPalettes
        # For now, just show the structure
        
        for npc in capture['npcs']:
            print(f"  NPC at ({npc['x']}, {npc['y']}):")
            print(f"    sheet = {npc['sheet']}")
            print(f"    pal_field = {npc['pal_field']}")
            print(f"    actual palette = {npc.get('actual_palette_index', 'UNKNOWN')}")
            print()
            
            # Determine which formula this matches
            if 'actual_palette_index' in npc:
                actual = npc['actual_palette_index']
                pal_field = npc['pal_field']
                
                # Test different formulas
                for base in [13, 14, 15, 16, 17]:
                    for offset_formula in ['pal_field // 2', '1 - pal_field // 2', 'pal_field & 1']:
                        # Calculate expected
                        if pal_field == 0:
                            if offset_formula == 'pal_field // 2':
                                offset = 0
                            elif offset_formula == '1 - pal_field // 2':
                                offset = 1
                            else:
                                offset = 0
                        elif pal_field == 1:
                            if offset_formula == 'pal_field // 2':
                                offset = 0
                            elif offset_formula == '1 - pal_field // 2':
                                offset = 1
                            else:
                                offset = 1
                        elif pal_field == 2:
                            if offset_formula == 'pal_field // 2':
                                offset = 1
                            elif offset_formula == '1 - pal_field // 2':
                                offset = 0
                            else:
                                offset = 0
                        else:  # pal_field == 3
                            if offset_formula == 'pal_field // 2':
                                offset = 1
                            elif offset_formula == '1 - pal_field // 2':
                                offset = 0
                            else:
                                offset = 1
                        
                        expected = (base + 2 * (npc_pal2 & 7) + offset) % 36
                        
                        if expected == actual:
                            key = f"base={base}, offset={offset_formula}"
                            formula_votes[key] = formula_votes.get(key, 0) + 1
    
    print("=" * 80)
    print("FORMULA ANALYSIS")
    print("=" * 80)
    print()
    
    if formula_votes:
        print("Formula votes:")
        for formula, votes in sorted(formula_votes.items(), key=lambda x: -x[1]):
            print(f"  {formula}: {votes} votes")
        
        best_formula = max(formula_votes, key=formula_votes.get)
        print()
        print(f"✓ Best formula: {best_formula}")
        print()
        
        # Extract formula parameters
        base = int(best_formula.split(',')[0].split('=')[1])
        offset_formula = best_formula.split(',')[1].split('=')[1].strip()
        
        return base, offset_formula
    else:
        print("✗ Not enough data to determine formula.")
        print("  Please capture more palette data from snes9x.")
        return None

def main():
    print_instructions()
    create_template()
    
    print()
    print("=" * 80)
    print("NEXT STEPS")
    print("=" * 80)
    print()
    print("1. Follow the instructions above to capture palette data from snes9x")
    print("2. Fill in the template: tools/snes_palette_capture.json")
    print("3. Run this tool again to analyze the data")
    print("4. Use the determined formula to regenerate .npc files")
    print()
    
    # Check if there's captured data to analyze
    template_path = Path("/home/jojosarah273/DEV/FF4_ASMTOC/tools/snes_palette_capture.json")
    if template_path.exists():
        with open(template_path) as f:
            data = json.load(f)
        
        # Check if any actual_palette_index values are filled in
        has_data = False
        for capture in data['captures']:
            for npc in capture['npcs']:
                if 'actual_palette_index' in npc and npc['actual_palette_index'] != 'UNKNOWN':
                    has_data = True
                    break
        
        if has_data:
            print("Detected captured data. Analyzing...")
            print()
            result = analyze_captured_data()
            
            if result:
                base, offset_formula = result
                print(f"✓ Determined formula:")
                print(f"  base = {base}")
                print(f"  offset = {offset_formula}")
                print()
                print("To regenerate .npc files with this formula:")
                print(f"  1. Update tools/ff4_export_submaps.py")
                print(f"  2. Run: python3 tools/ff4_export_submaps.py")
                print(f"  3. Verify: python3 tools/verify_palettes.py")
    
    print()
    print("=" * 80)
    print("ALTERNATIVE APPROACH")
    print("=" * 80)
    print()
    print("If capturing from snes9x is too difficult, use the ff4_world palette")
    print("inspector to manually determine the correct palettes:")
    print()
    print("  1. Run: cd build && ./ff4_world")
    print("  2. Navigate to a map")
    print("  3. Press I to enter palette inspector mode")
    print("  4. For each NPC, cycle through palettes (H/J) to find the correct one")
    print("  5. Record the correct palette indices")
    print("  6. Use this data to determine the formula")
    print()

if __name__ == '__main__':
    main()
