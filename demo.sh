#!/bin/sh
# ff4newlife demo launcher.
#
#   ./demo.sh            build (if needed) and run the headless demo tour
#   ./demo.sh --play     open a window and play the field scene
#
# The headless tour writes /tmp/ff4demo/ (screenshots + an animated GIF
# of the water shimmer) and needs no display.  --play opens an interactive
# window: arrows move, V = the tile reporter, T = the whole map, 1/2/3
# switch worlds, FF4_WARPTO=<map> jumps to any of the 383 sub-maps.
set -e
cd "$(dirname "$0")"

if [ ! -x build/ff4_world ]; then
    echo "== building (cmake) =="
    mkdir -p build
    (cd build && cmake .. -DCMAKE_BUILD_TYPE=Release >/dev/null && make -j4)
fi

if [ "$1" = "--play" ]; then
    exec ./build/ff4_world
fi

echo "== ff4newlife demo (headless screenshot tour) =="
python3 tools/demo_screens.py
