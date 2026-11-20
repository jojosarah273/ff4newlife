# NPC Palette Fix - Complete Resolution

## Problem Identified

The JSON `npcPalettes` table was completely wrong - it didn't match the ROM data at all. All 36 palette entries were mismatched, causing incorrect NPC colors throughout the game.

## Root Cause

The palette data was being loaded from the wrong source. The correct palette data is in the ROM at offset `0xA8700` (MapSpritePal table), but the JSON export had different/incorrect data.

## Solution Implemented

1. **Extracted correct palettes from ROM**: Created `tools/export_correct_palettes.py` to read MapSpritePal directly from the ROM at offset 0xA8700

2. **Fixed the palette formula**: Updated the base offset from 13 to 15:
   - Old (incorrect): `base = 13 + 2*(npcPalette & 7)`
   - New (correct): `base = 15 + 2*(npcPalette & 7)`

3. **Regenerated all .npc files**: All 361 submap NPC files now use the correct palette indices

4. **Updated export script**: Modified `ff4_export_submaps.py` to use the correct formula

## Verification

The automated test (`tools/test_npc_palettes.py`) confirms:
- ✓ Baron (map 0) correctly uses palettes 15 and 17 (user-verified)
- ✓ All 383 maps have valid palette indices
- ✓ ROM palettes are being used instead of incorrect JSON data

## Testing Instructions

```bash
# Run automated verification
python3 tools/test_npc_palettes.py

# Test in ff4_world
cd build
./ff4_world

# In the viewer:
# - Press F1 to warp to Baron (map 0)
# - NPCs should now display with correct colors
# - Use palette inspector (I key) to verify: should show 15 and 17
```

## Formula Reference

For each map:
- **Characters** (sprite sheet < 14): 
  - Uses `npcPalette1` from map properties
  - Formula: `(15 + 2*(npcPalette1 & 7) + PlayerPalTbl[char_id]//2) % 36`
  
- **NPCs** (sprite sheet >= 14):
  - Uses `npcPalette2` from map properties  
  - Formula: `(15 + 2*(npcPalette2 & 7) + palette_field//2) % 36`

## Files Modified

- `tools/ff4_export_submaps.py` - Updated palette formula (13→15)
- `tools/export_correct_palettes.py` - NEW: Extract palettes from ROM
- `tools/test_npc_palettes.py` - NEW: Automated verification
- `data/submaps/*.npc` - All 361 files regenerated
- `data/gfx/gfx/npcPalettes[0-35].bin` - All 36 palette files regenerated from ROM

## Key Discovery

The "magic number 13" mentioned in ASM comments was actually 15. The MapSpritePal table at 0xA8700 contains the correct palette data, and the proper base offset is 15, not 13.
