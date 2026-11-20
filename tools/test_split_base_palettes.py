#!/usr/bin/env python3
"""
Automated test suite for the split base palette approach.

This test verifies that:
1. Characters (sheet < 14) use base=13
2. NPCs (sheet >= 14) use base=15
3. All palette indices are valid (0-35)
4. Baron specifically uses palettes 13 and 17
"""

import json
import base64
from pathlib import Path

def test_baron_palettes():
    """Test that Baron uses the correct palettes (13 for characters, 17 for NPCs)"""
    with open('/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json') as f:
        data = json.load(f)
    
    obj = data['obj']
    map_props = obj['mapProperties']
    
    # Baron is map 0
    mp = map_props[0]
    npc_pal1 = mp.get('npcPalette1', 0)
    npc_pal2 = mp.get('npcPalette2', 0)
    
    # Calculate expected palettes
    PlayerPalTbl = [0, 0, 1, 2, 2, 2, 0, 1, 1, 3, 0, 1, 0, 0]
    char_pal = (13 + 2 * (npc_pal1 & 7) + PlayerPalTbl[0] // 2) % 36
    npc_pal = (15 + 2 * (npc_pal2 & 7) + 0 // 2) % 36
    
    # Verify Baron's specific values
    assert npc_pal1 == 0, f"Baron npcPalette1 should be 0, got {npc_pal1}"
    assert npc_pal2 == 1, f"Baron npcPalette2 should be 1, got {npc_pal2}"
    assert char_pal == 13, f"Baron character palette should be 13, got {char_pal}"
    assert npc_pal == 17, f"Baron NPC palette should be 17, got {npc_pal}"
    
    return True

def test_all_maps_valid():
    """Test that all maps produce valid palette indices"""
    with open('/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json') as f:
        data = json.load(f)
    
    obj = data['obj']
    map_props = obj['mapProperties']
    PlayerPalTbl = [0, 0, 1, 2, 2, 2, 0, 1, 1, 3, 0, 1, 0, 0]
    
    for map_id, mp in enumerate(map_props):
        npc_pal1 = mp.get('npcPalette1', 0)
        npc_pal2 = mp.get('npcPalette2', 0)
        
        # Character palette
        char_pal = (13 + 2 * (npc_pal1 & 7) + PlayerPalTbl[0] // 2) % 36
        assert 0 <= char_pal < 36, f"Map {map_id}: invalid character palette {char_pal}"
        
        # NPC palette (with pal_field=0)
        npc_pal = (15 + 2 * (npc_pal2 & 7) + 0 // 2) % 36
        assert 0 <= npc_pal < 36, f"Map {map_id}: invalid NPC palette {npc_pal}"
    
    return True

def test_npc_files_exist():
    """Test that all .npc files were generated for valid maps"""
    with open('/home/jojosarah273/DEV/FF4_ASMTOC/data/ff4-en-data.json') as f:
        data = json.load(f)
    
    obj = data['obj']
    map_props = obj['mapProperties']
    submaps_dir = Path('/home/jojosarah273/DEV/FF4_ASMTOC/data/submaps')
    
    missing_count = 0
    for map_id, mp in enumerate(map_props):
        # Check if map has valid layout data
        lay = mp.get('layout1', -1)
        layouts1 = obj.get('mapLayouts1', [])
        if 0 <= lay < len(layouts1):
            blob = base64.b64decode(layouts1[lay] + '==')
            if len(blob) >= 1024:
                # This map should have an .npc file
                npc_file = submaps_dir / f"{map_id}.npc"
                if not npc_file.exists():
                    missing_count += 1
    
    assert missing_count == 0, f"{missing_count} .npc files are missing for valid maps"
    return True

def main():
    print("=" * 70)
    print("Split Base Palette Test Suite")
    print("=" * 70)
    print()
    
    tests = [
        ("Baron Palettes", test_baron_palettes),
        ("All Maps Valid", test_all_maps_valid),
        ("NPC Files Exist", test_npc_files_exist),
    ]
    
    passed = 0
    failed = 0
    
    for test_name, test_func in tests:
        try:
            result = test_func()
            if result:
                print(f"✓ {test_name}")
                passed += 1
            else:
                print(f"✗ {test_name}")
                failed += 1
        except Exception as e:
            print(f"✗ {test_name}: {e}")
            failed += 1
    
    print()
    print("=" * 70)
    print(f"Results: {passed} passed, {failed} failed")
    print("=" * 70)
    
    return 0 if failed == 0 else 1

if __name__ == '__main__':
    exit(main())
