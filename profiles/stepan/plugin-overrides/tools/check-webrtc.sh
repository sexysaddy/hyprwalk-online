#!/usr/bin/env bash
# Fail *before* compiling a video-capable plugin when its SDK or GStreamer
# factories are missing. This checks the same factory names as MediaSession::probe.
set -euo pipefail

die() { printf 'hyprwalk-stepan WebRTC: ERROR: %s\n' "$*" >&2; exit 2; }

if [[ "${HYPRWALK_MULTIPLAYER:-1}" != "1" ]]; then
    [[ "${HYPRWALK_WEBRTC:-1}" == "0" ]] || die 'WebRTC requires multiplayer (set HYPRWALK_WEBRTC=0 with HYPRWALK_MULTIPLAYER=0)'
    echo ':: multiplayer disabled explicitly; window streaming unavailable'
    exit 0
fi
if [[ "${HYPRWALK_WEBRTC:-1}" == "0" ]]; then
    echo ':: WARNING: WebRTC explicitly disabled; remote windows will NOT stream'
    exit 0
fi
[[ "${HYPRWALK_WEBRTC:-1}" == "1" ]] || die 'HYPRWALK_WEBRTC must be 0 or 1'

command -v pkg-config >/dev/null 2>&1 || die 'pkg-config missing (install pkgconf)'
media_pkgs=(gstreamer-1.0 gstreamer-app-1.0 gstreamer-sdp-1.0 gstreamer-webrtc-1.0)
if ! pkg-config --atleast-version=1.22 gstreamer-1.0 || ! pkg-config --exists "${media_pkgs[@]}"; then
    die 'GStreamer >=1.22 development SDK missing (gstreamer-1.0, app, sdp, webrtc .pc files). Arch: install gstreamer gst-plugins-base gst-plugins-bad-libs.'
fi
command -v gst-inspect-1.0 >/dev/null 2>&1 || die 'gst-inspect-1.0 not found; install GStreamer runtime (Arch: gstreamer)'

elements=(webrtcbin nicesrc nicesink dtlssrtpenc dtlssrtpdec appsrc appsink queue videoconvert vp8enc vp8dec rtpvp8pay rtpvp8depay)
missing=()
for element in "${elements[@]}"; do
    gst-inspect-1.0 "$element" >/dev/null 2>&1 || missing+=("$element")
done
if (( ${#missing[@]} )); then
    die "missing GStreamer runtime factories: ${missing[*]}. Arch: install gst-plugins-base gst-plugins-good gst-plugins-bad libnice (nicesrc/nicesink come from libnice). Check: gst-inspect-1.0 <element>"
fi
printf ':: WebRTC build enabled: GStreamer %s; all %d runtime factories available\n' "$(pkg-config --modversion gstreamer-1.0)" "${#elements[@]}"
