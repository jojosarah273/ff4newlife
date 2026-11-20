# NPC Palette Issue - Current Status & Resolution Path

## Summary

After extensive investigation (297+ tool calls), we've determined:

### What We Know
1. **Base offset is 15** (empirically verified)
2. **Formula structure**: `(15 + 2 * (npcPalette & 7) + OFFSET) % 36`
3. **Current best formula**: `OFFSET = pal_field // 2`
   - Success rate: 94.2% (875 out of 929 NPCs correct)
   - Errors: 54 NPCs

### The Problem
The 54 remaining errors are due to **INCONSISTENT .npc files**:
- Files were generated with different formulas over time
- For the same (npcPalette, pal_field) combination, different .npc files have different values
- We CANNOT determine the correct formula from the .npc files alone

### The Solution
We need **GROUND TRUTH** from the actual SNES game. Two options:

## Option A: Automated Ground Truth Capture (RECOMMENDED)

Use snes9x to capture actual palette data:

```bash
# 1. Install snes9x
sudo apt-get install snes9x

# 2. Run the game
snes9x ff2.smc

# 3. Navigate to a map with NPCs (e.g., Baron, map 36)

# 4. Open memory viewer (F1 or Debug -> Memory Viewer)

# 5. Navigate to CGRAM at $2100
#    Sprite palettes are at $2180-$21FF (128 bytes)

# 6. Export the 128 bytes as hex

# 7. Update tools/snes_palette_capture.json with the data

# 8. Run the analysis tool
python3 tools/capture_snes_palettes.py
```

This will give us the CORRECT formula with 100% certainty.

## Option B: Manual Verification with ff4_world

Use the palette inspector to manually find correct palettes:

```bash
# 1. Run ff4_world
cd build && ./ff4_world

# 2. Navigate to a map (e.g., press F1 for Baron)

# 3. Press 'I' to enter palette inspector mode

# 4. For each NPC:
#    - Press TAB to select the NPC
#    - Press H/J to cycle through all 36 palettes
#    - Find the palette that looks correct
#    - Press P to record it

# 5. After checking all NPCs, compile the results
#    - Create a mapping: (npcPalette, pal_field) -> correct_palette
#    - Use this to determine the formula
```

This is more time-consuming but doesn't require snes9x.

## Current Best Formula

**For now**, use: `(15 + 2 * (npcPalette & 7) + pal_field // 2) % 36`

This gives 94.2% accuracy. The 54 errors are in specific maps/NPCs that need manual correction.

## Tools Available

1. **tools/verify_palettes.py** - Check .npc files against formula
2. **tools/headless_palette_verifier.py** - Compare rendering with references
3. **tools/capture_snes_palettes.py** - Capture and analyze SNES palette data
4. **tools/empirical_palette_verifier.py** - Analyze .npc file patterns

## Next Steps

1. **Choose Option A or B** above to get ground truth
2. **Determine the correct formula** with 100% certainty
3. **Regenerate all .npc files** with the correct formula
4. **Verify** with headless_palette_verifier.py
5. **Rebuild ff4_world** and test

## Conclusion

The palette issue is SOLVABLE, but requires ground truth from the actual game. 
Once we have that, we can determine the correct formula and regenerate all files.

The current formula (94.2% accuracy) is a good starting point, but the 54 errors
need to be resolved for a perfect match with the original game.
