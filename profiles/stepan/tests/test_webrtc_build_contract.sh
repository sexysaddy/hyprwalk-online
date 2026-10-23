#!/usr/bin/env bash
# Offline regression test for the build wiring; mocks do not simulate GStreamer.
set -euo pipefail
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
TEMP="$(mktemp -d)"
trap 'rm -rf "$TEMP"' EXIT
cat > "$TEMP/pkg-config" <<'SH'
#!/usr/bin/env bash
case "${1:-}" in
  --atleast-version=1.22) exit 0 ;;
  --exists) [[ " $* " == *' libpipewire-0.3 '* ]] && exit 1; exit 0 ;;
  --modversion) echo 1.28.8 ;;
  --cflags) echo -I/mock/sdk ;;
  --libs) echo '-lgstwebrtc-1.0 -lgstreamer-1.0 -lgstapp-1.0 -lgstsdp-1.0' ;;
  *) exit 0 ;;
esac
SH
cat > "$TEMP/gst-inspect-1.0" <<'SH'
#!/usr/bin/env bash
[[ "${MOCK_MISSING_FACTORY:-}" != "${1:-}" ]]
SH
chmod +x "$TEMP/pkg-config" "$TEMP/gst-inspect-1.0"
export PATH="$TEMP:$PATH"

# Default path must include media cpp files and -DHYPRWALK_WEBRTC.
(cd "$ROOT/stepan" && make -n HYPRWALK_MULTIPLAYER_DIR=../multiplayer > "$TEMP/online.make")
grep -Fq -- '-DHYPRWALK_WEBRTC' "$TEMP/online.make"
grep -Fq 'media/gstreamer/MediaSession.cpp' "$TEMP/online.make"
grep -Fq 'media/gl/Readback.cpp' "$TEMP/online.make"

# Explicit offline mode omits the video compilation.
(cd "$ROOT/stepan" && make -n HYPRWALK_WEBRTC=0 HYPRWALK_MULTIPLAYER_DIR=../multiplayer > "$TEMP/offline.make")
if grep -Fq 'media/gstreamer/MediaSession.cpp' "$TEMP/offline.make"; then
    echo 'FAIL: explicitly offline build still compiled WebRTC media' >&2; exit 1
fi

# Preflight must accept all factories but refuse any missing factory.
"$ROOT/build.sh" --check > "$TEMP/check-ok.log"
MOCK_MISSING_FACTORY=nicesrc "$ROOT/build.sh" --check > "$TEMP/check-fail.log" 2>&1 && {
    echo 'FAIL: missing nicesrc was incorrectly accepted' >&2; exit 1
}
grep -Fq 'nicesrc' "$TEMP/check-fail.log"
# The legacy Makefile-argument form must be interpreted as an explicit offline
# mode rather than publishing an offline binary under the WebRTC filename.
"$ROOT/build.sh" HYPRWALK_WEBRTC=0 --check > "$TEMP/check-offline.log"
grep -Fq 'explicitly disabled' "$TEMP/check-offline.log"
printf 'PASS: default WebRTC / explicit offline / missing-factory regression\n'
