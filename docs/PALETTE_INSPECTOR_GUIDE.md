# FF4 NPC Palette Inspector Guide

## Overview

The palette inspector is an empirical tool for determining the correct NPC palettes by visual inspection. Instead of trying to reverse-engineer the exact palette formula from the SNES ASM, this tool lets you cycle through all 36 palettes for each NPC and record the correct ones.

## How to Use

### Starting the Inspector

1. Launch ff4_world with a map:
   ```bash
   FF4_WARPTO=<map_id> ./build/ff4_world
   ```
   
   Example - Load Baron (map 0):
   ```bash
   FF4_WARPTO=0 ./build/ff4_world
   ```

2. Press **I** to enter palette inspector mode
   - The display will show "NPC PAL: INSPECTOR"
   - The first NPC will be automatically selected

### Navigating NPCs

- **TAB** - Cycle to the next NPC
  - Shows the selected NPC's position, sprite sheet, and current palette
  - Indicates if a palette has been recorded for this NPC

### Cycling Palettes

- **H** - Cycle palette backward (decrement)
- **J** - Cycle palette forward (increment)
- These keys change the palette for the **currently selected NPC only**
- The NPC sprite updates immediately to show the new palette

### Recording Correct Palettes

- **RETURN/ENTER** - Record the current palette for the selected NPC
  - The console will show: `[palette_inspect] RECORDED NPC <n> = palette <p>`
  - The NPC selection display will show "[RECORDED]"

### Saving Results

- **F10** - Save all recorded palettes to a file
  - Saves to: `data/submaps/<map_id>.pal_recorded`
  - Format: `npc_index x y sheet palette`
  - Only saves NPCs where you pressed RETURN to record

### Other Controls

- **K** - Toggle palette readout display
- **F** - Freeze/unfreeze NPC movement (easier to inspect)
- **Q** or **ESC** - Quit

## Output Format

The `.pal_recorded` file contains:
```
# Recorded NPC palettes for map <id>
# Format: npc_index x y sheet palette
0 3 19 101 15
2 11 27 17 16
...
```

Each line represents:
- `npc_index` - Index in the map's NPC list (0-based)
- `x y` - NPC position on the map
- `sheet` - Sprite sheet ID
- `palette` - The correct palette index (0-35)

## Workflow

1. Load a map you want to inspect
2. Press **I** to enter inspector mode
3. For each NPC:
   - Press **TAB** to select it
   - Press **H/J** to cycle through palettes until it looks correct
   - Press **RETURN** to record it
4. After inspecting all NPCs, press **F10** to save
5. Repeat for other maps

## Tips

- Compare with the original SNES game running in an emulator side-by-side
- Use **F** to freeze NPCs so they don't move while you're inspecting
- The palette cycles through all 36 available palettes - some may look similar
- Color 0 (transparent) is always the same, so focus on colors 1-7

## Next Steps

Once you've recorded palettes for multiple maps, we can:
1. Analyze the patterns to understand the palette selection formula
2. Update the `.npc` files with the correct palettes
3. Implement the correct formula in the export script
4. Eventually derive the exact SNES behavior

## Example Session

```bash
# Load Town of Baron
FF4_WARPTO=0 ./build/ff4_world

# In the viewer:
# 1. Press I
# 2. Press TAB to select NPC 0
# 3. Press J several times until the NPC looks right
# 4. Press RETURN to record
# 5. Press TAB to select NPC 1
# 6. Press H/J to find the right palette
# 7. Press RETURN to record
# 8. Continue for all NPCs...
# 9. Press F10 to save
# 10. Press Q to quit

# Check the recorded palettes
cat data/submaps/0.pal_recorded
```

## Troubleshooting

- If no NPCs are selected, the map might not have any NPCs
- If palettes don't change, make sure you're in inspector mode (display shows "INSPECTOR")
- If F10 doesn't save, check that you pressed RETURN for at least one NPC
- The recorded file is only created if you have recorded at least one palette
