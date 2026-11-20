#!/usr/bin/env python3
"""
Analyze which specific NPCs have wrong palette assignments.
"""
from pathlib import Path
from PIL import Image
import json

REFS_DIR = Path("/home/jojosarah273/DEV/FF4_ASMTOC/refs")
RENDER_DIR = Path("/tmp")

def load_npc_data(map_id):
    """Load NPC data for a specific map"""
    npc_file = Path(f"/home/jojosarah273/DEV/FF4_ASMTOC/data/submaps/{map_id}.npc")
    if not npc_file.exists():
        return []
    
    npcs = []
    with open(npc_file) as f:
        for line in f:
            parts = line.strip().split('\t')
            if len(parts) >= 1:
                fields = parts[0].split()
                if len(fields) >= 5:
                    x, y, dir, pal, sheet = int(fields[0]), int(fields[1]), int(fields[2]), int(fields[3]), int(fields[4])
                    npcs.append({
                        'x': x,
                        'y': y,
                        'dir': dir,
                        'palette': pal,
                        'sheet': sheet,
                        'text': parts[1] if len(parts) > 1 else ''
                    })
    return npcs

def extract_npc_region(image, npc, tile_size=16):
    """Extract the region around an NPC from an image"""
    px = npc['x'] * tile_size
    py = npc['y'] * tile_size
    
    margin = 8
    x1 = max(0, px - margin)
    y1 = max(0, py - margin)
    x2 = min(image.width, px + tile_size + margin)
    y2 = min(image.height, py + tile_size + margin)
    
    # Ensure valid coordinates
    if x2 <= x1 or y2 <= y1:
        return None
    
    return image.crop((x1, y1, x2, y2))

def compare_npc_colors(ref_img, render_img, npc):
    """Compare colors of a specific NPC between reference and rendered"""
    ref_region = extract_npc_region(ref_img, npc)
    render_region = extract_npc_region(render_img, npc)
    
    if ref_region is None or render_region is None:
        return 999.0  # Can't compare
    
    # Resize to same dimensions
    ref_region = ref_region.resize(render_region.size)
    
    ref_pixels = list(ref_region.getdata())
    render_pixels = list(render_region.getdata())
    
    total_diff = 0
    for ref_p, rend_p in zip(ref_pixels, render_pixels):
        diff = abs(ref_p[0] - rend_p[0]) + abs(ref_p[1] - rend_p[1]) + abs(ref_p[2] - rend_p[2])
        total_diff += diff
    
    avg_diff = total_diff / len(ref_pixels) if ref_pixels else 999.0
    return avg_diff

def main():
    print("=" * 80)
    print("NPC PALETTE MISMATCH ANALYSIS")
    print("=" * 80)
    
    ref_baron = Image.open(REFS_DIR / 'baron_fixed.png')
    render_baron = Image.open(RENDER_DIR / 'test_render_map36.png')
    
    npcs = load_npc_data(36)
    
    print(f"\nAnalyzing {len(npcs)} NPCs in Baron (map 36)...")
    print(f"Reference image: {ref_baron.size}")
    print(f"Rendered image: {render_baron.size}")
    print()
    
    mismatches = []
    for i, npc in enumerate(npcs):
        avg_diff = compare_npc_colors(ref_baron, render_baron, npc)
        
        if avg_diff > 50:
            mismatches.append({
                'index': i,
                'npc': npc,
                'diff': avg_diff
            })
            print(f"NPC {i:2d} at ({npc['x']:2d},{npc['y']:2d}): "
                  f"palette={npc['palette']:2d}, sheet={npc['sheet']:3d}, "
                  f"avg_diff={avg_diff:6.2f} ⚠ MISMATCH")
        else:
            print(f"NPC {i:2d} at ({npc['x']:2d},{npc['y']:2d}): "
                  f"palette={npc['palette']:2d}, sheet={npc['sheet']:3d}, "
                  f"avg_diff={avg_diff:6.2f} ✓ OK")
    
    print()
    print("=" * 80)
    print("SUMMARY")
    print("=" * 80)
    print(f"\nTotal NPCs: {len(npcs)}")
    print(f"Mismatches: {len(mismatches)}")
    
    if mismatches:
        print("\nNPCs with wrong palettes:")
        for m in mismatches[:5]:  # Show first 5
            npc = m['npc']
            print(f"  - NPC at ({npc['x']},{npc['y']}): "
                  f"current palette={npc['palette']}, sheet={npc['sheet']}")
        
        print("\nThis suggests the palette INDEX formula needs adjustment.")
        print("Try different base values (13, 14, 15, 16, 17) in the export formula.")

if __name__ == '__main__':
    main()
