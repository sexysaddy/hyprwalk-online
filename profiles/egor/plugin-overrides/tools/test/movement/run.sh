#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
bin="${TMPDIR:-/tmp}/hyprwalk-movement-unit"
${CXX:-c++} -std=c++20 -O2 -Wall -Wextra -Werror -Isrc src/movement.cpp tools/test/movement/movement_unit.cpp -o "$bin"
"$bin"
