#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out="$(mktemp)"
trap 'rm -f "$out"' EXIT
g++ -std=c++20 -O2 -Wall -Wextra -Werror -Isrc tools/test/aspect/aspect_unit.cpp -o "$out"
"$out"
python3 tools/test/aspect/camera.py
