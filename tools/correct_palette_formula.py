#!/usr/bin/env python3
"""
Correct palette formula based on empirical analysis.

The pattern shows:
  pal_field=0,1: use base+1 (second loaded palette)
  pal_field=2,3: use base+0 (first loaded palette)

Formula: (15 + 2 * (npcPalette & 7) + (1 - pal_field // 2)) % 36
"""
import json
from pathlib import Path
import re

def main():
    print("=" * 80)
    print("CORRECTING PALETTE FORMULA")
    print("=" * 80)
    print()
    
    # Update the export script
    script_path = Path("/home/jojosarah273/DEV/FF4_ASMTOC/tools/ff4_export_submaps.py")
    content = script_path.read_text()
    
    print("STEP 1: Updating export script with correct formula...")
    print("-" * 80)
    
    # The correct formula
    correct_npc_formula = 'npc_pal = (15 + 2 * (p.get("npcPalette2", 0) & 7) + (1 - pal_field // 2)) % 36'
    correct_char_formula = 'npc_pal = (15 + 2 * (p.get("npcPalette1", 0) & 7) + (1 - char_pal // 2)) % 36'
    
    # Replace NPC formula
    lines = content.split('\n')
    updated = False
    
    for i, line in enumerate(lines):
        # Update NPC formula
        if 'npc_pal = (15 + 2 * (p.get("npcPalette2", 0) & 7)' in line:
            indent = len(line) - len(line.lstrip())
            lines[i] = ' ' * indent + correct_npc_formula
            print(f"  Updated NPC formula at line {i+1}")
            updated = True
        
        # Update character formula
        if 'npc_pal = (15 + 2 * (p.get("npcPalette1", 0) & 7)' in line:
            indent = len(line) - len(line.lstrip())
            lines[i] = ' ' * indent + correct_char_formula
            print(f"  Updated character formula at line {i+1}")
            updated = True
    
    if updated:
        content = '\n'.join(lines)
        script_path.write_text(content)
        print("✓ Export script updated")
    else:
        print("✗ Could not find formulas to update")
        return 1
    
    print()
    print("STEP 2: Verifying the formula...")
    print("-" * 80)
    
    # Test the formula
    test_cases = [
        (5, 0, 26),  # npcPalette2=5, pal_field=0 -> 26
        (5, 1, 26),  # npcPalette2=5, pal_field=1 -> 26
        (5, 2, 25),  # npcPalette2=5, pal_field=2 -> 25
        (5, 3, 25),  # npcPalette2=5, pal_field=3 -> 25
        (6, 0, 28),  # npcPalette2=6, pal_field=0 -> 28
        (6, 1, 28),  # npcPalette2=6, pal_field=1 -> 28
        (6, 2, 28),  # npcPalette2=6, pal_field=2 -> 28
        (6, 3, 27),  # npcPalette2=6, pal_field=3 -> 27
    ]
    
    print("Formula test cases:")
    all_correct = True
    for npc_pal, pal_field, expected in test_cases:
        result = (15 + 2 * (npc_pal & 7) + (1 - pal_field // 2)) % 36
        status = "✓" if result == expected else "✗"
        if result != expected:
            all_correct = False
        print(f"  {status} npcPalette2={npc_pal}, pal_field={pal_field}: "
              f"expected={expected}, got={result}")
    
    if not all_correct:
        print("\n✗ Formula verification failed!")
        return 1
    
    print("\n✓ All test cases passed!")
    
    print()
    print("STEP 3: Regenerating all .npc files...")
    print("-" * 80)
    
    # Run the export script
    import subprocess
    result = subprocess.run(
        ['python3', 'tools/ff4_export_submaps.py'],
        cwd='/home/jojosarah273/DEV/FF4_ASMTOC',
        capture_output=True,
        text=True
    )
    
    print(result.stdout)
    if result.stderr:
        print("STDERR:", result.stderr)
    
    print()
    print("STEP 4: Verifying regenerated files...")
    print("-" * 80)
    
    # Load data for verification
    with open('/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json') as f:
        data = json.load(f)
    
    obj = data['obj']
    map_props = obj['mapProperties']
    npc_props = obj['npcProperties']
    PlayerPalTbl = [0, 0, 1, 2, 2, 2, 0, 1, 1, 3, 0, 1, 0, 0, 0, 0, 0, 0]
    
    errors = 0
    total = 0
    
    for map_id in range(len(map_props)):
        if map_id >= len(npc_props):
            continue
            
        mp = map_props[map_id]
        npcs = npc_props[map_id]
        
        npc_pal1 = mp.get('npcPalette1', 0)
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
            
            npc = npcs[i]
            pal_field = npc.get('palette', 0)
            
            # Calculate expected with CORRECT formula
            if sheet < 14:
                # Character
                char_pal = PlayerPalTbl[sheet] if sheet < len(PlayerPalTbl) else 0
                expected_pal = (15 + 2 * (npc_pal1 & 7) + (1 - char_pal // 2)) % 36
            else:
                # Regular NPC
                expected_pal = (15 + 2 * (npc_pal2 & 7) + (1 - pal_field // 2)) % 36
            
            if actual_pal != expected_pal:
                errors += 1
                if errors <= 5:
                    print(f"  ERROR: Map {map_id}, NPC {i}: expected={expected_pal}, actual={actual_pal}")
            
            total += 1
    
    print()
    print(f"Total NPCs: {total}")
    print(f"Errors: {errors}")
    
    if errors == 0:
        print()
        print("=" * 80)
        print("✓ SUCCESS! All .npc files now use the correct formula!")
        print("=" * 80)
        print()
        print("The correct formula is:")
        print("  (15 + 2 * (npcPalette & 7) + (1 - pal_field // 2)) % 36")
        print()
        print("This correctly maps:")
        print("  pal_field=0,1 -> second loaded palette (base+1)")
        print("  pal_field=2,3 -> first loaded palette (base+0)")
        print()
        print("Next steps:")
        print("  1. Rebuild ff4_world: cd build && make")
        print("  2. Test in ff4_world and verify NPC colors")
        print("  3. Use headless verifier to compare with reference screenshots")
        return 0
    else:
        print()
        print(f"✗ Still have {errors} errors. The formula may still need adjustment.")
        return 1

if __name__ == '__main__':
    exit(main())
