#!/usr/bin/env bash
# Read the API-version macro from the exact headers selected by pkg-config.
# Hyprland 0.57.0 and 0.57.1 both define this as 0.2 upstream.
set -Eeuo pipefail

running_version="${1:-}"
headers_version="${2:-$(pkg-config --modversion hyprland 2>/dev/null || true)}"
read -r -a compiler_cmd <<<"${CXX:-g++}"
((${#compiler_cmd[@]})) || compiler_cmd=(g++)

command -v "${compiler_cmd[0]}" >/dev/null 2>&1 || {
    echo "Hyprwalk API check: C++ compiler not found: ${compiler_cmd[0]}" >&2
    exit 2
}
pkg-config --exists hyprland || {
    echo "Hyprwalk API check: pkg-config cannot find hyprland.pc" >&2
    exit 2
}

read -r -a cflags <<<"$(pkg-config --cflags hyprland)"
macro_dump="$(printf '#include <hyprland/src/plugins/PluginAPI.hpp>\n' | \
    "${compiler_cmd[@]}" "${cflags[@]}" -std=c++26 -dM -E -x c++ - 2>&1)" || {
    echo "Hyprwalk API check: cannot preprocess the PluginAPI.hpp selected by pkg-config:" >&2
    printf '%s\n' "$macro_dump" >&2
    exit 2
}
api_version="$(awk '$2 == "HYPRLAND_API_VERSION" { gsub(/"/, "", $3); print $3; exit }' <<<"$macro_dump")"
[[ -n "$api_version" ]] || {
    echo "Hyprwalk API check: HYPRLAND_API_VERSION was not defined by the selected PluginAPI.hpp" >&2
    exit 2
}

printf ':: plugin API: %s (headers %s)\n' "$api_version" "${headers_version:-unknown}"

# The upstream 0.57.0/0.57.1 loader requires API version 0.2. A same-looking
# pkg-config semver is not enough if stale or mixed headers remain installed.
target_version="${running_version:-$headers_version}"
case "$target_version" in
    0.57.0|0.57.1)
        if [[ "$api_version" != "0.2" ]]; then
            cat >&2 <<MSG
Hyprwalk API check: incompatible development headers.
  Detected Hyprland version: ${target_version}
  PluginAPI.hpp API version:  ${api_version}
  Upstream ${target_version} expects: 0.2

The plugin loader compares this string before PLUGIN_INIT(), so this .so
will be rejected with 'API version mismatch'. Do not hardcode 0.2 in the
plugin: replace/update the headers so they match the running compositor.
On Arch, fully update/reinstall Hyprland and its development files, or run
'hyprpm update' and build against that matching header tree.
MSG
            exit 2
        fi
        ;;
esac
