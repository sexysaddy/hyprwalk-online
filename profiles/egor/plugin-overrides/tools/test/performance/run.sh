#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out="$(mktemp)"
trap 'rm -f "$out"' EXIT
${CXX:-c++} -std=c++20 -O2 -Wall -Wextra -Werror -Isrc tools/test/performance/performance_unit.cpp -o "$out"
"$out"
