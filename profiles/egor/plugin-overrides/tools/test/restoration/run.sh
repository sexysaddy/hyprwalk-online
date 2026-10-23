#!/usr/bin/env bash
# Standalone regression suite. This does NOT build or load the Hyprland plugin.
# Usage: bash tools/test/restoration/run.sh [absolute-log-directory]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
OUT="${1:-$(mktemp -d /tmp/hyprwalk-regression-logs.XXXXXX)}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
cd "$ROOT"
TMP="$(mktemp -d /tmp/hyprwalk-regression-bin.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT
run() { local name="$1"; shift; echo "=== $name ==="; "$@" 2>&1 | tee "$OUT/$name.log"; }
run movement bash tools/test/movement/run.sh
run walker bash tools/test/walker/run.sh
run settings bash tools/test/settings/run.sh
run aspect bash tools/test/aspect/run.sh
run cadence bash tools/test/performance/run.sh
run navigation python3 tools/test/navigation/run.py
run frame-loop python3 tools/test/frame-loop/run.py
run render-target python3 tools/test/render-target/run.py
run graphics-structural-only python3 tools/test/graphics/check.py
run tiling-compile "${CXX:-g++}" -std=c++23 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie -Isrc \
    src/tiling.cpp src/walker.cpp src/collision.cpp tools/test/tiling/tile_unit.cpp -o "$TMP/tiling"
run tiling env ASAN_OPTIONS=detect_leaks=1 "$TMP/tiling"
run video-fallback-compile "${CXX:-g++}" -std=c++20 -Wall -Wextra -Werror \
    -c src/viewmodel_video.cpp -o "$TMP/video-fallback.o"
echo "Standalone suite PASS. Logs: $OUT"
echo "Not a substitute for clean ./build.sh or live Hyprland verification."
