#!/usr/bin/env python3
"""
Automated palette formula finder.
Tests different base values and finds which one gives 0 verification errors.
"""
import subprocess
import sys
from pathlib import Path
import json

def test_formula(base_char, base_npc, desc):
    """Test a specific palette formula"""
    print(f"\nTesting: {desc}")
    print(f"  Characters: base={base_char}, NPCs: base={base_npc}")
    
    # Update export script
    script_path = Path("/home/jojosarah273/DEV/FF4_ASMTOC/tools/ff4_export_submaps.py")
    content = script_path.read_text()
    
    # Update character formula
    content = content.replace(
        "char_base = 13",
        f"char_base = {base_char}"
    ).replace(
        "char_base = 14",
        f"char_base = {base_char}"
    ).replace(
        "char_base = 15",
        f"char_base = {base_char}"
    ).replace(
        "char_base = 16",
        f"char_base = {base_char}"
    ).replace(
        "char_base = 17",
        f"char_base = {base_char}"
    )
    
    # Update NPC formula
    content = content.replace(
        "npc_base = 13",
        f"npc_base = {base_npc}"
    ).replace(
        "npc_base = 14",
        f"npc_base = {base_npc}"
    ).replace(
        "npc_base = 15",
        f"npc_base = {base_npc}"
    ).replace(
        "npc_base = 16",
        f"npc_base = {base_npc}"
    ).replace(
        "npc_base = 17",
        f"npc_base = {base_npc}"
    )
    
    script_path.write_text(content)
    
    # Regenerate .npc files
    result = subprocess.run(
        ["python3", "tools/ff4_export_submaps.py"],
        cwd="/home/jojosarah273/DEV/FF4_ASMTOC",
        capture_output=True,
        text=True
    )
    
    # Run verification
    result = subprocess.run(
        ["python3", "tools/verify_palettes.py"],
        cwd="/home/jojosarah273/DEV/FF4_ASMTOC",
        capture_output=True,
        text=True
    )
    
    # Parse results
    if "0 ERRORS FOUND" in result.stdout or "✓ All palettes are correct!" in result.stdout:
        print(f"  ✓ SUCCESS - 0 errors!")
        return True
    else:
        # Extract error count
        for line in result.stdout.split('\n'):
            if 'ERRORS FOUND' in line:
                print(f"  {line.strip()}")
                break
        return False

def main():
    print("=" * 80)
    print("AUTOMATED PALETTE FORMULA FINDER")
    print("=" * 80)
    
    # Test various combinations
    test_cases = [
        (13, 13, "Base 13 for both (original)"),
        (13, 14, "Characters=13, NPCs=14"),
        (13, 15, "Characters=13, NPCs=15"),
        (13, 16, "Characters=13, NPCs=16"),
        (13, 17, "Characters=13, NPCs=17"),
        (14, 13, "Characters=14, NPCs=13"),
        (14, 14, "Base 14 for both"),
        (14, 15, "Characters=14, NPCs=15"),
        (14, 16, "Characters=14, NPCs=16"),
        (14, 17, "Characters=14, NPCs=17"),
        (15, 13, "Characters=15, NPCs=13"),
        (15, 14, "Characters=15, NPCs=14"),
        (15, 15, "Base 15 for both"),
        (15, 16, "Characters=15, NPCs=16"),
        (15, 17, "Characters=15, NPCs=17"),
        (16, 13, "Characters=16, NPCs=13"),
        (16, 14, "Characters=16, NPCs=14"),
        (16, 15, "Characters=16, NPCs=15"),
        (16, 16, "Base 16 for both"),
        (16, 17, "Characters=16, NPCs=17"),
        (17, 13, "Characters=17, NPCs=13"),
        (17, 14, "Characters=17, NPCs=14"),
        (17, 15, "Characters=17, NPCs=15"),
        (17, 16, "Characters=17, NPCs=16"),
        (17, 17, "Base 17 for both"),
    ]
    
    results = []
    for base_char, base_npc, desc in test_cases:
        success = test_formula(base_char, base_npc, desc)
        results.append((base_char, base_npc, desc, success))
    
    print("\n" + "=" * 80)
    print("RESULTS SUMMARY")
    print("=" * 80)
    
    successful = [r for r in results if r[3]]
    
    if successful:
        print(f"\n✓ Found {len(successful)} working formula(s):")
        for base_char, base_npc, desc, _ in successful:
            print(f"  - {desc}")
    else:
        print("\n✗ No working formula found in tested range")
        print("\nPossible issues:")
        print("1. The formula itself is wrong (not just the base values)")
        print("2. The palette FIELD calculation needs adjustment")
        print("3. There's a different issue with palette indexing")
    
    return 0 if successful else 1

if __name__ == '__main__':
    sys.exit(main())
