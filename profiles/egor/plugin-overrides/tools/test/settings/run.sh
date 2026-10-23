#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
bin="${TMPDIR:-/tmp}/hyprwalk-settings-unit"
${CXX:-c++} -std=c++20 -O2 -Wall -Wextra -Werror -Isrc tools/test/settings/settings_unit.cpp -o "$bin"
"$bin"
