#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
TMP="$(mktemp -d)"
trap 'rm -rf -- "$TMP"' EXIT
CXX="${CXX:-g++}"
"$CXX" -std=c++20 -O2 -Wall -Wextra -Werror -I"$ROOT/fork/src" \
  "$ROOT/tests/graphics_visibility_regression.cpp" -o "$TMP/visibility"
"$TMP/visibility"
"$CXX" -std=c++20 -O2 -Wall -Wextra -Werror -I"$ROOT/fork/src" \
  "$ROOT/tests/viewmodel_uv_regression.cpp" "$ROOT/fork/src/viewmodel_model.cpp" \
  -o "$TMP/mdl_uv"
MODEL_PATH="${HYPRWALK_TEST_GOLDSRC_MDL:-$ROOT/fork/assets/viewmodels/cs16/v_knife.mdl}"
if [[ -r "$MODEL_PATH" ]]; then
  "$TMP/mdl_uv" "$MODEL_PATH"
else
  echo "SKIP: GoldSrc UV fixture not bundled; set HYPRWALK_TEST_GOLDSRC_MDL to a local v_knife.mdl"
fi
