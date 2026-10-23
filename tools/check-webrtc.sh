#!/usr/bin/env bash
# Check both development SDK and runtime factories needed by the real WebRTC media backend.
set -euo pipefail
die() { printf 'Hyprwalk WebRTC preflight: ERROR: %s\n' "$*" >&2; exit 2; }
[[ "${HYPRWALK_MULTIPLAYER:-1}" == 1 ]] || { [[ "${HYPRWALK_WEBRTC:-1}" == 0 ]] && exit 0; die 'WebRTC requires multiplayer'; }
[[ "${HYPRWALK_WEBRTC:-1}" == 1 ]] || { [[ "${HYPRWALK_WEBRTC:-1}" == 0 ]] && { echo 'WebRTC explicitly disabled'; exit 0; }; die 'HYPRWALK_WEBRTC must be 0 or 1'; }
command -v pkg-config >/dev/null || die 'pkg-config missing'
media_pkgs=(gstreamer-1.0 gstreamer-app-1.0 gstreamer-sdp-1.0 gstreamer-webrtc-1.0 gstreamer-video-1.0)
pkg-config --atleast-version=1.22 gstreamer-1.0 || die 'GStreamer >= 1.22 development SDK required'
pkg-config --exists "${media_pkgs[@]}" || die 'missing GStreamer development modules: gstreamer, app, sdp, webrtc, video'
command -v gst-inspect-1.0 >/dev/null || die 'gst-inspect-1.0 missing; install runtime GStreamer packages'
elements=(webrtcbin nicesrc nicesink dtlssrtpenc dtlssrtpdec appsrc appsink queue videoconvert vp8enc vp8dec rtpvp8pay rtpvp8depay)
missing=()
for e in "${elements[@]}"; do gst-inspect-1.0 "$e" >/dev/null 2>&1 || missing+=("$e"); done
((${#missing[@]} == 0)) || die "missing runtime factories: ${missing[*]}"
printf 'WebRTC preflight PASS: GStreamer %s; %d required video factories available\n' "$(pkg-config --modversion gstreamer-1.0)" "${#elements[@]}"
