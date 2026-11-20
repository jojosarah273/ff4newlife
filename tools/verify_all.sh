#!/bin/bash
# FF4 field-scene self-test: every guard, one command, bounded time.
#
#   ./tools/verify_all.sh          # run everything
#   ./tools/verify_all.sh quick     # skip the slow sweeps (verify_all_maps)
#
# Each check is independent; a failure names itself.  "slow" runs the
# full-map sprite sweep (~10 min) - everything else finishes in ~5 min.
cd "$(dirname "$0")/.." || exit 1

FAIL=0
run() {   # name, command
    printf '%-28s' "$1"
    if timeout "${TMO:-600}" "${@:2}" >/tmp/selftest_$1.log 2>&1; then
        echo "PASS"
    else
        echo "FAIL  (log: /tmp/selftest_$1.log)"
        FAIL=1
    fi
}

TMO=120 run unit_tests       ./build/test_dialog data/gfx/window
TMO=300 run ctest            bash -c 'cd build && ctest --output-on-failure'
TMO=120 run palette_tables   python3 tools/ff4_palette_truth.py --verify
TMO=120 run window_font      python3 tools/ff4_window_font.py --verify
TMO=120 run anim_gfx_data    python3 tools/ff4_bg_anim.py --verify
TMO=120 run message_box      python3 tools/verify_messagebox.py
TMO=300 run layouts_from_rom python3 tools/ff4_layout_rom.py
TMO=300 run ff4data_packs    python3 tools/ff4data.py --verify
TMO=300 run warp_audit       python3 tools/ff4_warp_audit.py
TMO=400 run field_animation  python3 tools/verify_animation.py
TMO=600 run submap_view      python3 tools/verify_submap_view.py
TMO=400 run lava_palette     python3 tools/verify_lava_pal.py
TMO=300 run layout_group     python3 tools/verify_layout_group.py
TMO=300 run bridge_z         python3 tools/verify_bridge_z.py
TMO=300 run world_tile_report python3 tools/verify_world_report.py

if [ "${1:-}" != "quick" ]; then
    TMO=3600 run sprite_sweep    python3 tools/verify_all_maps.py 0 383
else
    echo "(quick mode: skipping the full sprite sweep)"
fi

if [ "$FAIL" -eq 0 ]; then
    echo
    echo "ALL GUARDS GREEN"
    exit 0
else
    echo
    echo "SOME GUARDS FAILED - see /tmp/selftest_*.log"
    exit 1
fi
