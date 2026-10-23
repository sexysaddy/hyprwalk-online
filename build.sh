#!/usr/bin/env bash
# Unified build entry point: common source + one profile's explicit overrides.
set -Eeuo pipefail
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)"
PROFILE=""; MODE="video"; JOBS="${HYPRWALK_JOBS:-$(nproc 2>/dev/null || echo 2)}"; OUTPUT=""; CHECK_ONLY=0; FULL_CORE=0; KEEP=0
usage(){ cat <<'EOF'
Usage: ./build.sh --profile egor|stepan [--mode video|offline|core] [options]
  --profile NAME      Required profile: egor or stepan
  --mode MODE         video (VP8/WebRTC), offline (plugin without multiplayer), core (standalone multiplayer tests)
  --jobs N            Parallel compilation jobs
  --output PATH       Destination for plugin .so; refuses to overwrite existing file
  --check             Check profile/dependencies only; does not compile
  --full-core-tests   Also run standalone process/socket harnesses in core mode
  --keep-worktree     Keep the generated temporary source overlay for debugging
EOF
}
die(){ printf 'Hyprwalk unified: ERROR: %s\n' "$*" >&2; exit 2; }
while (($#)); do case "$1" in
 --profile) [[ $# -ge 2 ]] || die '--profile needs value'; PROFILE="$2"; shift 2;;
 --mode) [[ $# -ge 2 ]] || die '--mode needs value'; MODE="$2"; shift 2;;
 --jobs) [[ $# -ge 2 ]] || die '--jobs needs value'; JOBS="$2"; shift 2;;
 --output) [[ $# -ge 2 ]] || die '--output needs value'; OUTPUT="$2"; shift 2;;
 --check) CHECK_ONLY=1; shift;; --full-core-tests) FULL_CORE=1; shift;; --keep-worktree) KEEP=1; shift;;
 -h|--help) usage; exit 0;; *) die "unknown option: $1";; esac; done
[[ "$PROFILE" == egor || "$PROFILE" == stepan ]] || { usage >&2; die 'select profile egor or stepan'; }
[[ "$MODE" == video || "$MODE" == offline || "$MODE" == core ]] || die "unknown mode: $MODE"
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || die 'jobs must be a positive integer'
[[ -d "$ROOT/shared/plugin" && -d "$ROOT/multiplayer" && -d "$ROOT/profiles/$PROFILE" ]] || die 'unified source tree is incomplete'
# Dependency preflight never needs the generated ~source overlay: avoid a huge
# filesystem copy on every `--check` invocation, especially in CI.
export HYPRWALK_MULTIPLAYER=1 HYPRWALK_WEBRTC=0
case "$MODE" in
  offline) HYPRWALK_MULTIPLAYER=0; HYPRWALK_WEBRTC=0 ;;
  video) HYPRWALK_MULTIPLAYER=1; HYPRWALK_WEBRTC=1 ;;
  core) HYPRWALK_MULTIPLAYER=1; HYPRWALK_WEBRTC=0 ;;
esac
export HYPRWALK_MULTIPLAYER HYPRWALK_WEBRTC
if ((CHECK_ONLY)); then
  command -v make >/dev/null || die 'make missing'
  command -v "${CXX:-g++}" >/dev/null || die 'C++ compiler missing'
  if [[ "$MODE" == core ]]; then echo "CHECK PASS: profile=$PROFILE mode=core (no compilation)"; exit 0; fi
  command -v pkg-config >/dev/null || die 'pkg-config missing'
  pkg-config --exists hyprland || die 'Hyprland SDK missing: pkg-config cannot find hyprland.pc'
  if [[ "$MODE" == video ]]; then
    "$ROOT/tools/check-webrtc.sh"
  fi
  echo "CHECK PASS: profile=$PROFILE mode=$MODE (no compilation)"; exit 0
fi
# Only actual builds require the assembled profile source overlay.
TMP="$(mktemp -d "${TMPDIR:-/tmp}/hyprwalk-unified-${PROFILE}.XXXXXX")"
cleanup(){ rc=$?; if ((KEEP)); then printf 'Temporary source view retained: %s\n' "$TMP" >&2; else rm -rf -- "$TMP"; fi; exit "$rc"; }
trap cleanup EXIT INT TERM
python3 "$ROOT/tools/assemble_profile.py" --root "$ROOT" --profile "$PROFILE" --dest "$TMP/view"
PLUGIN_DIR="$TMP/view/plugin"; MODULE_DIR="$TMP/view/multiplayer"
export HYPRWALK_MULTIPLAYER_DIR="$MODULE_DIR"
if [[ "$MODE" == core ]]; then
  command -v make >/dev/null || die 'make missing'; command -v "${CXX:-g++}" >/dev/null || die 'C++ compiler missing'
  echo "== Core build: $PROFILE =="
  make -C "$MODULE_DIR" clean
  make -C "$MODULE_DIR" -j"$JOBS" all
  make -C "$MODULE_DIR" test
  if ((FULL_CORE)); then make -C "$MODULE_DIR" process-test player-process-test windows-process-test media-driver-process-test; fi
  echo "CORE PASS: $PROFILE (not a Hyprland plugin build)"; exit 0
fi
command -v pkg-config >/dev/null || die 'pkg-config missing'; command -v make >/dev/null || die 'make missing'
export CXX="${CXX:-g++}"
pkg-config --exists hyprland || die 'Hyprland SDK missing: install headers/pkg-config matching your running Hyprland'
if [[ "$MODE" == video ]]; then
  "$ROOT/tools/check-webrtc.sh"
fi
if [[ -z "$OUTPUT" ]]; then mkdir -p "$ROOT/dist"; OUTPUT="$ROOT/dist/hyprwalk-$PROFILE-$MODE-$(date -u +%Y%m%dT%H%M%SZ).so"; elif [[ "$OUTPUT" != /* ]]; then OUTPUT="$ROOT/$OUTPUT"; fi
mkdir -p "$(dirname -- "$OUTPUT")"; [[ ! -e "$OUTPUT" ]] || die "refusing to overwrite existing output: $OUTPUT"
HYPRWALK_MULTIPLAYER_DIR="$MODULE_DIR" HYPRWALK_MULTIPLAYER="$HYPRWALK_MULTIPLAYER" HYPRWALK_WEBRTC="$HYPRWALK_WEBRTC" bash "$PLUGIN_DIR/build.sh" "PLUGIN=$PLUGIN_DIR/hyprwalk.so" -j"$JOBS"
[[ -s "$PLUGIN_DIR/hyprwalk.so" ]] || die 'build returned success without producing a nonempty .so'
if [[ "$MODE" == video ]]; then
  command -v readelf >/dev/null || die 'readelf missing'
  readelf -d "$PLUGIN_DIR/hyprwalk.so" | grep -Fq 'libgstwebrtc-1.0.so' || die 'built artifact does not link libgstwebrtc; refusing to label as video build'
fi
cp -- "$PLUGIN_DIR/hyprwalk.so" "$OUTPUT"; sha256sum "$OUTPUT"; echo "PLUGIN BUILD PASS: $OUTPUT"; echo 'This confirms compile/link only, not live loading or WebRTC operation.'
