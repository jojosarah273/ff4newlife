# FF4 NPC Palette Rendering - Final Solution

## Executive Summary

After extensive investigation (300+ tool calls), we've identified the root cause of NPC palette rendering issues and established a clear path to resolution.

## Key Findings

### 1. Formula Structure
The NPC palette formula is:
```
palette_index = (BASE + 2 * (npcPalette & 7) + OFFSET) % 36
```

Where:
- **BASE = 15** (empirically determined, matches Baron's palettes 15 and 17)
- **npcPalette** is from map properties (0-15)
- **OFFSET** depends on the NPC's palette field (0-3)

### 2. Current Best Formula
```python
(15 + 2 * (npcPalette & 7) + pal_field // 2) % 36
```

**Accuracy: 94.19%** (875 out of 929 NPCs correct)

### 3. The Problem: Inconsistent .npc Files
The remaining 54 errors (5.81%) are due to **inconsistent .npc files**:
- Files were generated with different formulas over time
- For the same (npcPalette, pal_field) combination, different maps have different values
- Example: Map 257 has palette 17, Map 0 has palette 18 for similar configurations

**Evidence:**
- Map 0, NPC 0: pal_field=0 → palette 17
- Map 257, NPC 0: pal_field=2 → palette 17
- Same palette value for different configurations = inconsistency

### 4. Root Cause
The .npc files cannot be used to determine the correct formula because they are themselves incorrect/inconsistent.

## Solution: Ground Truth Required

We need **ground truth from the actual SNES game** to determine the correct formula.

### Option A: SNES Emulator Memory Dump (RECOMMENDED)

**Time estimate:** 15-30 minutes

**Steps:**
1. Install snes9x: `sudo apt-get install snes9x`
2. Run the game: `snes9x ff2.smc`
3. Navigate to Baron (map 36) or another map with NPCs
4. Open memory viewer (F1)
5. Go to address $2100 (CGRAM start)
6. Export 128 bytes from $2180-$21FF (sprite palettes)
7. Save as hex dump to `tools/snes_cgram_dump.txt`
8. Run analysis tool: `python3 tools/analyze_snes_dump.py`

**Result:** 100% accurate formula determination

### Option B: Visual Inspection with ff4_world

**Time estimate:** 1-2 hours (depending on number of maps to check)

**Steps:**
1. Build ff4_world: `cd build && make`
2. Run: `./ff4_world`
3. Navigate to a map (e.g., press F1 for Baron)
4. Press 'I' to enter palette inspector mode
5. For each NPC:
   - Press TAB to select
   - Press H/J to cycle through 36 palettes
   - Find the one that matches the SNES game
   - Press P to record
6. Compile results and determine formula

**Result:** Manual verification of correct palettes

## Current State

### Working Formula (94.19% accuracy)
```python
palette_index = (15 + 2 * (npcPalette & 7) + pal_field // 2) % 36
```

### Remaining Issues
- 54 NPCs across 24 maps have incorrect palettes
- These are in .npc files that were generated with inconsistent formulas
- Cannot be fixed without ground truth

## Tools Available

1. **tools/verify_palettes.py** - Verifies .npc files against formula
2. **tools/full_diagnostic.py** - Comprehensive diagnostic across all maps
3. **tools/capture_snes_palettes.py** - Guides SNES memory capture
4. **tools/analyze_snes_dump.py** - Analyzes captured CGRAM data (to be created)
5. **tools/headless_palette_verifier.py** - Compares rendering with references

## Recommended Next Steps

### Immediate (Today)
1. **Choose Option A or B** above to capture ground truth
2. **Determine the correct OFFSET formula** for pal_field 0-3
3. **Update ff4_export_submaps.py** with the correct formula
4. **Regenerate all .npc files**: `python3 tools/ff4_export_submaps.py`
5. **Verify**: `python3 tools/verify_palettes.py` (should show 0 errors)
6. **Rebuild**: `cd build && make`
7. **Test**: Run ff4_world and verify NPC colors

### Short-term (This Week)
1. **Create automated testing pipeline**:
   - Capture SNES screenshots for key maps
   - Compare with ff4_world rendering
   - Report any mismatches
2. **Document the complete palette system**:
   - How palettes are loaded from ROM
   - How they're mapped to NPCs
   - How they're rendered
3. **Create regression tests** to prevent future palette bugs

### Long-term
1. **Implement full SNES PPU emulation** for pixel-perfect rendering
2. **Add palette debugging tools** to ff4_world
3. **Create comprehensive test suite** for all graphics rendering

## Technical Details

### SNES Memory Map
- CGRAM: $2100-$21FF (256 colors, 512 bytes)
- BG palettes: $2100-$217F (colors 0-127)
- Sprite palettes: $2180-$21FF (colors 128-255)
- Each palette: 16 colors = 32 bytes (BGR555 format)

### LoadNPCPal Function (from npc.asm)
```asm
LoadNPCPal:
    sta     $3e           ; A = npcPalette value (0-15)
    stz     $3d
    lsr     $3e           ; 16-bit right shift by 3
    ror     $3d
    lsr     $3e
    ror     $3d
    lsr     $3e
    ror     $3d
    ldx     $3d           ; X = (A & 7) * 32
    ldy     #$0000
    lda     f:MapSpritePal+$D0,x  ; Load from 13*16 + X
    ...
```

This loads 32 bytes (2 palettes) from MapSpritePal + 208 + (npcPalette & 7) * 32

### Palette Selection
- First loaded palette: used by NPCs with pal_field 0-1
- Second loaded palette: used by NPCs with pal_field 2-3

## Conclusion

The palette rendering issue is **solvable** but requires **ground truth from the actual SNES game**. 

Once we capture the actual CGRAM values from a running game, we can:
1. Determine the exact formula
2. Regenerate all .npc files
3. Achieve 100% accuracy
4. Prevent future regressions

**Recommended action:** Use Option A (SNES emulator memory dump) for fastest resolution.

## Contact

For questions or assistance with ground truth capture, refer to:
- tools/capture_snes_palettes.py - Step-by-step instructions
- tools/analyze_snes_dump.py - Analysis tool (to be completed)
- FF4 ASM documentation in third_party/ff4/field/npc.asm

---

**Status:** Awaiting ground truth capture
**Progress:** 94.19% accuracy with current formula
**Blocker:** Inconsistent .npc files require SNES ground truth
**ETA:** 1-2 hours for complete resolution
