#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
TMP="$(mktemp -d)"
trap 'rm -rf -- "$TMP"' EXIT
CXX="${CXX:-g++}"
"$CXX" -std=c++20 -O2 -Wall -Wextra -Werror -I"$ROOT/fork/src" \
  "$ROOT/tests/viewmodel_regression.cpp" "$ROOT/fork/src/viewmodel_model.cpp" \
  -o "$TMP/viewmodel_regression"
MODEL_PATH="${HYPRWALK_TEST_GOLDSRC_MDL:-$ROOT/fork/assets/viewmodels/cs16/v_knife.mdl}"
if [[ -r "$MODEL_PATH" ]]; then
  MODEL_DIR="$(dirname -- "$MODEL_PATH")"
  "$TMP/viewmodel_regression" "$MODEL_DIR"
else
  echo "SKIP: model parser/animation fixture tests; set HYPRWALK_TEST_GOLDSRC_MDL to a local v_knife.mdl"
fi
python3 "$ROOT/tests/test-viewmodel-orientation.py"

"$CXX" -std=c++20 -O2 -Wall -Wextra -Werror -I"$ROOT/fork/src" \
  "$ROOT/tests/viewmodel_rotation_regression.cpp" "$ROOT/fork/src/viewmodel_model.cpp" \
  -o "$TMP/viewmodel_rotation_regression"
if [[ -r "$MODEL_PATH" ]]; then
  "$TMP/viewmodel_rotation_regression" "$(dirname -- "$MODEL_PATH")"
else
  echo "SKIP: MDL-dependent rotation test; set HYPRWALK_TEST_GOLDSRC_MDL to a local v_knife.mdl"
fi
python3 "$ROOT/tests/test-viewmodel-settings.py"

# Dedicated regression for the GoldSrc additive knife material and opaque gloves.
bash "$ROOT/fork/tools/test/viewmodel/run_transparency_test.sh"
