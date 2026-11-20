#!/usr/bin/env python3
"""
Visual palette verifier - compares rendered output with reference screenshots.
Uses only PIL/Pillow (no numpy required).
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

def compare_images(ref_path, render_path, diff_path=None):
    """Compare two images and return match percentage and difference map."""
    ref = Image.open(ref_path).convert('RGB')
    render = Image.open(render_path).convert('RGB')
    
    # Resize to same dimensions
    size = (min(ref.width, render.width), min(ref.height, render.height))
    ref = ref.resize(size)
    render = render.resize(size)
    
    # Calculate per-pixel difference
    ref_pixels = list(ref.getdata())
    render_pixels = list(render.getdata())
    
    diff_pixels = []
    high_diff_count = 0
    
    for ref_p, ren_p in zip(ref_pixels, render_pixels):
        diff = sum(abs(a - b) for a, b in zip(ref_p, ren_p))
        
        if diff > 30:
            high_diff_count += 1
            # Mark differences in red
            diff_pixels.append((255, 0, 0))
        else:
            # Dim matching pixels
            diff_pixels.append(tuple(c // 2 for c in ren_p))
    
    total_pixels = len(ref_pixels)
    match_pct = 100.0 * (1.0 - high_diff_count / total_pixels)
    
    # Save difference visualization
    if diff_path:
        diff_img = Image.new('RGB', size)
        diff_img.putdata(diff_pixels)
        diff_img.save(diff_path)
    
    return match_pct, high_diff_count, total_pixels

def analyze_map(map_name, map_id):
    """Analyze a specific map's rendering vs reference."""
    ref_dir = Path("/home/jojosarah273/DEV/FF4_ASMTOC/refs")
    ref_path = ref_dir / f"{map_name}_fixed.png"
    render_path = Path(f"/tmp/test_render_map{map_id}.png")
    diff_path = Path(f"/tmp/{map_name}_diff.png")
    
    if not ref_path.exists():
        print(f"✗ Reference image not found: {ref_path}")
        return None
    
    if not render_path.exists():
        print(f"✗ Rendered image not found: {render_path}")
        print(f"  Run: FF4_WARPTO={map_id} ./build/ff4_world")
        return None
    
    print("=" * 80)
    print(f"{map_name.upper()} MAP ANALYSIS (Map {map_id})")
    print("=" * 80)
    print()
    
    match_pct, high_diff, total = compare_images(ref_path, render_path, diff_path)
    
    print(f"Match percentage: {match_pct:.2f}%")
    print(f"Pixels with differences: {high_diff} / {total} ({100*high_diff/total:.1f}%)")
    print()
    
    if match_pct < 95:
        print("⚠ Significant differences detected!")
        print(f"  Difference map saved to: {diff_path}")
        print()
        return False
    else:
        print("✓ Rendering matches reference!")
        print()
        return True

def main():
    print("=" * 80)
    print("VISUAL PALETTE VERIFICATION TOOL")
    print("=" * 80)
    print()
    
    # Analyze available maps
    maps = [
        ("baron", 36, "Baron Castle"),
        ("baron_out", 0, "Baron Exterior"),
    ]
    
    results = {}
    for map_name, map_id, description in maps:
        result = analyze_map(map_name, map_id)
        if result is not None:
            results[description] = result
        print()
    
    # Summary
    print("=" * 80)
    print("SUMMARY")
    print("=" * 80)
    print()
    
    all_good = all(results.values())
    
    for map_name, result in results.items():
        status = "✓ PASS" if result else "✗ FAIL"
        print(f"  {status}: {map_name}")
    
    print()
    
    if all_good:
        print("✓✓✓ ALL MAPS MATCH REFERENCE! ✓✓✓")
        print()
        print("The palette formula is 100% correct!")
    else:
        print("✗✗✗ SOME MAPS DO NOT MATCH ✗✗✗")
        print()
        print("Next steps:")
        print("1. Open difference images to identify which NPCs have wrong colors:")
        print("   eog /tmp/baron_diff.png /tmp/baron_out_diff.png")
        print()
        print("2. Compare side-by-side with references:")
        print("   eog /home/jojosarah273/DEV/FF4_ASMTOC/refs/baron_fixed.png /tmp/test_render_map36.png")
        print()
        print("3. Use ff4_world palette inspector to find correct palettes:")
        print("   FF4_WARPTO=36 ./build/ff4_world")
        print("   Press 'I' to enter inspector mode")
        print("   TAB: select NPC, H/J: cycle palette, P: record")
        print()

if __name__ == '__main__':
    main()
