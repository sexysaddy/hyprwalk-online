#!/usr/bin/env bash
# Mocked preflight test; it checks video-only build wiring, not live WebRTC.
set -Eeuo pipefail
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
TMP="$(mktemp -d)"; trap 'rm -rf -- "$TMP"' EXIT
cat > "$TMP/pkg-config" <<'MOCK'
#!/usr/bin/env bash
case "${1:-}" in
  --exists) exit 0;;
  --atleast-version=1.22) exit 0;;
  --modversion) echo 1.24.0;;
  --cflags) echo -I/mock/include;;
  --libs) echo '-lgstwebrtc-1.0 -lgstreamer-1.0';;
  *) exit 0;;
esac
MOCK
cat > "$TMP/gst-inspect-1.0" <<'MOCK'
#!/usr/bin/env bash
[[ "${MOCK_MISSING_FACTORY:-}" != "${1:-}" ]]
MOCK
chmod +x "$TMP/pkg-config" "$TMP/gst-inspect-1.0"
PATH="$TMP:$PATH" "$ROOT/build.sh" --profile stepan --mode video --check > "$TMP/pass.log"
grep -Fq 'CHECK PASS' "$TMP/pass.log"
PATH="$TMP:$PATH" "$ROOT/build.sh" --profile egor --mode offline --check > "$TMP/offline-pass.log"
grep -Fq 'mode=offline' "$TMP/offline-pass.log"
if PATH="$TMP:$PATH" MOCK_MISSING_FACTORY=nicesrc "$ROOT/build.sh" --profile stepan --mode video --check > "$TMP/fail.log" 2>&1; then
  echo 'FAIL: missing nicesrc factory accepted' >&2; exit 1
fi
grep -Fq 'nicesrc' "$TMP/fail.log"
if PATH="$TMP:$PATH" "$ROOT/build.sh" --profile egor --mode video-audio --check > "$TMP/removed-mode.log" 2>&1; then
  echo 'FAIL: removed video-audio mode was accepted' >&2; exit 1
fi
grep -Fq 'unknown mode: video-audio' "$TMP/removed-mode.log"
printf 'PASS: mocked video-only preflight; missing VP8/WebRTC factories refused; offline mode succeeds; audio-stream mode removed
'
