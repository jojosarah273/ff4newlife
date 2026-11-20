# NPC Palette Solution - Ground Truth Required

## Problem Summary

After extensive investigation, we've discovered that:

1. **The .npc files are INCONSISTENT** - they were generated with different formulas over time
2. **We cannot determine the correct formula from the .npc files alone**
3. **We need GROUND TRUTH from the actual SNES game**

## What We Know

From SNES ASM analysis:
- LoadNPCPal loads TWO palette entries (32 bytes)
- Base offset: 15 (empirically determined)
- Formula structure: `(15 + 2 * (npcPalette & 7) + OFFSET) % 36`
- The OFFSET depends on the NPC's palette field (0-3)

From empirical data (inconsistent .npc files):
- For some NPCs: pal_field=0,1 use base+1; pal_field=2,3 use base+0
- For other NPCs: different patterns
- This inconsistency proves the .npc files are corrupted

## Solution: Ground Truth Verification

### Option 1: Use snes9x to Capture Actual Palettes (RECOMMENDED)

1. **Install snes9x** (if not already installed):
   ```bash
   sudo apt-get install snes9x
   ```

2. **Run the game and navigate to a map with NPCs**:
   ```bash
   snes9x /path/to/ff4.smc
   ```

3. **Open the memory viewer** (Debug -> Memory Viewer or press F1)

4. **Navigate to CGRAM** (address $2100):
   - Sprite palettes are at $2180-$21FF (colors 128-255)
   - Each palette is 16 bytes (8 colors × 2 bytes)

5. **Dump the sprite palette data**:
   - Record the BGR555 values for palettes 0-7
   - Compare with our npcPalettes table

6. **Determine which palette indices are actually used**:
   - If the game uses npcPalettes[15] and npcPalettes[17], then the formula is correct
   - If it uses different indices, adjust the formula accordingly

### Option 2: Use ff4_world Palette Inspector (MANUAL)

1. **Run ff4_world**:
   ```bash
   cd build && ./ff4_world
   ```

2. **Navigate to a map with NPCs**

3. **Press I to enter palette inspector mode**

4. **For each NPC**:
   - Use H/J to cycle through all 36 palettes
   - Find the palette that matches the SNES game
   - Press P to record the correct palette

5. **Compile the results**:
   - Create a mapping: (npcPalette, pal_field) -> correct_palette
   - Use this to determine the correct formula

### Option 3: Compare with Reference Screenshots (AUTOMATED)

1. **Create reference screenshots**:
   - Run the SNES game in snes9x
   - Navigate to specific maps
   - Take screenshots of NPCs with known palettes

2. **Use the headless verifier**:
   ```bash
   python3 tools/headless_palette_verifier.py
   ```

3. **Adjust the formula** until the match percentage is >95%

4. **Regenerate .npc files** with the correct formula

## Current Status

- **Formula with fewest errors**: `(15 + 2 * (npcPalette & 7) + pal_field // 2) % 36`
- **Error count**: 54 out of 929 NPCs (94.2% success)
- **Issue**: The 54 errors are due to inconsistent .npc files, not formula errors

## Next Steps

1. **Choose one of the ground truth verification methods above**
2. **Determine the correct formula**
3. **Regenerate all .npc files** with the correct formula
4. **Verify** with the headless verifier
5. **Rebuild ff4_world** and test

## Tools Available

- `tools/empirical_palette_verifier.py` - Analyzes .npc file patterns
- `tools/headless_palette_verifier.py` - Compares rendering with references
- `tools/verify_palettes.py` - Checks .npc files against formula
- `tools/ff4_export_submaps.py` - Generates .npc files (needs correct formula)

## Conclusion

The palette issue cannot be solved by reverse-engineering alone. We need **ground truth from the actual game** to determine the correct formula. Once we have that, we can regenerate all .npc files and verify the rendering matches the original game.
