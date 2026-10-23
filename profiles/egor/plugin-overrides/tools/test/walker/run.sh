#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
bin="${TMPDIR:-/tmp}/hyprwalk-walker-unit"
${CXX:-c++} -std=c++20 -O2 -Wall -Wextra -Werror -Isrc src/walker.cpp src/collision.cpp tools/test/walker/walker_unit.cpp -o "$bin"
"$bin"
