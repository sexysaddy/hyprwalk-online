#!/usr/bin/env bash
set -Eeuo pipefail
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
FORK=$(CDPATH= cd -- "$HERE/../../.." && pwd -P)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/hyprwalk-knife-test.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
MODEL_PATH="${HYPRWALK_TEST_GOLDSRC_MDL:-$FORK/assets/viewmodels/cs16/v_knife.mdl}"
if [[ ! -r "$MODEL_PATH" ]]; then
    echo "SKIP: transparency runtime fixture is not bundled; set HYPRWALK_TEST_GOLDSRC_MDL to a local v_knife.mdl"
    exit 0
fi
g++ -std=c++23 -O2 -Wall -Wextra -I"$FORK/src" \
    "$HERE/transparency_test.cpp" "$FORK/src/viewmodel_model.cpp" \
    -o "$TMP/transparency_test"
"$TMP/transparency_test" "$MODEL_PATH" "$TMP"
