#!/usr/bin/env python3
"""
Headless palette verification system using PIL only.
Renders scenes and compares against reference screenshots pixel-by-pixel.
"""
import json
import subprocess
import sys
from pathlib import Path
from PIL import Image

ROM_PATH = Path("/home/jojosarah273/DEV/FF4_ASMTOC/ff2.smc")
REFS_DIR = Path("/home/jojosarah273/DEV/FF4_ASMTOC/refs")
BUILD_DIR = Path("/home/jojosarah273/DEV/FF4_ASMTOC/build")

def render_scene(map_id, output_path):
    """Render a scene using ff4_world and save screenshot"""
    env = {
        'FF4_WARPTO': str(map_id),
        'FF4_SHOT': str(output_path),
        'FF4_MAPX': '16',
        'FF4_MAPY': '16',
    }
    
    cmd = [str(BUILD_DIR / 'ff4_world')]
    try:
        result = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=5)
        return result.returncode == 0 and output_path.exists()
    except:
        return False

def compare_images(img1_path, img2_path):
    """Compare two images pixel-by-pixel using PIL"""
    img1 = Image.open(img1_path).convert('RGB')
    img2 = Image.open(img2_path).convert('RGB')
    
    # Resize to same dimensions
    size = min(img1.size[0], img2.size[0]), min(img1.size[1], img2.size[1])
    img1 = img1.resize(size)
    img2 = img2.resize(size)
    
    pixels1 = list(img1.getdata())
    pixels2 = list(img2.getdata())
    
    # Calculate difference
    total_diff = 0
    max_diff = len(pixels1) * 255 * 3
    
    for p1, p2 in zip(pixels1, pixels2):
        total_diff += abs(p1[0] - p2[0]) + abs(p1[1] - p2[1]) + abs(p1[2] - p2[2])
    
    match_pct = 100.0 * (1.0 - total_diff / max_diff)
    return match_pct

def main():
    print("=" * 80)
    print("HEADLESS PALETTE VERIFICATION SYSTEM")
    print("=" * 80)
    
    # Check for reference images
    refs = {
        'baron': REFS_DIR / 'baron_fixed.png',
        'baron_out': REFS_DIR / 'baron_out_anchored.png',
    }
    
    available_refs = {k: v for k, v in refs.items() if v.exists()}
    print(f"\nAvailable reference images: {list(available_refs.keys())}")
    
    if not available_refs:
        print("\n✗ No reference images found!")
        return 1
    
    # Test current implementation
    print("\n" + "=" * 80)
    print("TESTING CURRENT PALETTE IMPLEMENTATION")
    print("=" * 80)
    
    # Map IDs
    test_maps = {
        'baron': 36,
        'baron_out': 0,
    }
    
    results = {}
    for ref_name, map_id in test_maps.items():
        if ref_name in available_refs:
            print(f"\nTesting {ref_name} (map {map_id})...")
            
            # Render scene
            output = Path(f"/tmp/test_render_map{map_id}.png")
            if not render_scene(map_id, output):
                print(f"  ✗ Failed to render")
                continue
            
            # Compare with reference
            match_pct = compare_images(available_refs[ref_name], output)
            print(f"  Match: {match_pct:.2f}%")
            
            results[ref_name] = {
                'match': match_pct,
                'render': output,
                'reference': available_refs[ref_name],
            }
    
    # Summary
    print("\n" + "=" * 80)
    print("RESULTS SUMMARY")
    print("=" * 80)
    
    for ref_name, data in results.items():
        print(f"\n{ref_name}: {data['match']:.2f}% match")
        print(f"  Reference: {data['reference']}")
        print(f"  Rendered:  {data['render']}")
        
        if data['match'] < 95:
            print(f"  ⚠ Low match - palettes likely incorrect")
    
    # Recommendations
    print("\n" + "=" * 80)
    print("RECOMMENDATIONS")
    print("=" * 80)
    
    avg_match = sum(r['match'] for r in results.values()) / len(results) if results else 0
    
    if avg_match >= 98:
        print("\n✓ Palettes appear correct!")
        return 0
    else:
        print("\n✗ Palette issues detected")
        print("\nTo fix this:")
        print("1. Open the reference and rendered images side-by-side")
        print("2. Identify which NPCs have wrong colors")
        print("3. Use the palette inspector (I key) to find correct palette for each NPC")
        print("4. Update the palette formula in ff4_export_submaps.py")
    
    return 1 if avg_match < 95 else 0

if __name__ == '__main__':
    sys.exit(main())
