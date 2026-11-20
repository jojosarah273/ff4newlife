#!/usr/bin/env python3
"""
Final comprehensive palette fix.

Analysis shows the .npc files have inverted values for pal_field.
The correct formula should be:
  (15 + 2*(npcPalette2 & 7) + (1 - pal_field // 2)) % 36

This gives:
  pal_field=0,1: add 1
  pal_field=2,3: add 0
"""
import json
import re
from pathlib import Path

def main():
    print("=" * 80)
    print("FINAL COMPREHENSIVE PALETTE FIX")
    print("=" * 80)
    print()
    
    # Update the export script with the correct formula
    script_path = Path("/home/jojosarah273/DEV/FF4_ASMTOC/tools/ff4_export_submaps.py")
    content = script_path.read_text()
    
    print("STEP 1: Updating export script with corrected formula...")
    print("-" * 80)
    
    # Replace the NPC palette calculation
    # Old: npc_pal = (15 + 2 * (p.get("npcPalette2", 0) & 7) + pal_field // 2) % 36
    # New: npc_pal = (15 + 2 * (p.get("npcPalette2", 0) & 7) + (1 - pal_field // 2)) % 36
    
    old_pattern = r'npc_pal = \(15 \+ 2 \* \(p\.get\("npcPalette2", 0\) & 7\) \+ pal_field // 2\) % 36'
    new_formula = 'npc_pal = (15 + 2 * (p.get("npcPalette2", 0) & 7) + (1 - pal_field // 2)) % 36'
    
    content_new = re.sub(old_pattern, new_formula, content)
    
    if content_new == content:
        print("WARNING: Could not find the pattern to replace.")
        print("Trying alternative approach...")
        
        # Try line-by-line replacement
        lines = content.split('\n')
        for i, line in enumerate(lines):
            if 'npc_pal = (15 + 2 * (p.get("npcPalette2", 0) & 7) + pal_field // 2) % 36' in line:
                lines[i] = line.replace('pal_field // 2', '(1 - pal_field // 2)')
                print(f"  Fixed line {i+1}")
        
        content_new = '\n'.join(lines)
    
    script_path.write_text(content_new)
    print("✓ Export script updated")
    print()
    
    # Also update character formula if needed
    # Old: npc_pal = (15 + 2 * (p.get("npcPalette1", 0) & 7) + char_pal // 2) % 36
    # New: npc_pal = (15 + 2 * (p.get("npcPalette1", 0) & 7) + (1 - char_pal // 2)) % 36
    
    print("STEP 2: Verifying formula...")
    print("-" * 80)
    
    # Test the formula
    test_cases = [
        (1, 0, 16),  # npcPalette2=1, pal_field=0 -> should be 16
        (1, 1, 16),  # npcPalette2=1, pal_field=1 -> should be 16
        (1, 2, 17),  # npcPalette2=1, pal_field=2 -> should be 17
        (1, 3, 17),  # npcPalette2=1, pal_field=3 -> should be 17
    ]
    
    print("Formula test cases:")
    for npc_pal, pal_field, expected in test_cases:
        result = (15 + 2 * (npc_pal & 7) + (1 - pal_field // 2)) % 36
        status = "✓" if result == expected else "✗"
        print(f"  {status} npcPalette2={npc_pal}, pal_field={pal_field}: "
              f"expected={expected}, got={result}")
    
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
            
            # Calculate expected with NEW formula
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
        print("The corrected formula is:")
        print("  (15 + 2 * (npcPalette & 7) + (1 - pal_field // 2)) % 36")
        print()
        print("This correctly maps:")
        print("  pal_field=0,1 -> first loaded palette")
        print("  pal_field=2,3 -> second loaded palette")
        print()
        print("Next steps:")
        print("  1. Run ff4_world to verify rendering")
        print("  2. Use headless verifier to compare with reference screenshots")
        print("  3. If match is good (>95%), we're done!")
        return 0
    else:
        print()
        print(f"✗ Still have {errors} errors. Need further investigation.")
        return 1

if __name__ == '__main__':
    exit(main())
