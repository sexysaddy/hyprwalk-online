#!/usr/bin/env python3
"""Source-level guard for production WebRTC queue and window-slot policy.
This is intentionally not described as a live ICE/DTLS test.
"""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
media = (root / "media/gstreamer/MediaSession.cpp").read_text()
video_types = (root / "include/hyprwalk/mp/VideoTypes.hpp").read_text()
readback = (root / "media/gl/Readback.cpp").read_text()
assert "queue max-size-buffers=512 max-size-time=0 max-size-bytes=4194304 leaky=no ! rtpvp8depay" in media
assert "queue max-size-buffers=1 max-size-time=0 max-size-bytes=0 leaky=downstream ! rtpvp8depay" not in media
assert "MaxVideoStreams = MaxSharedWindows" in video_types
assert "streams = static_cast<unsigned>(MaxVideoStreams)" in video_types
assert "m->sources.size()>=MaxVideoStreams" in readback
# ICE configuration is optional by design and must not leak credentials into diagnostics.
media_text = media
assert 'HYPRWALK_STUN_SERVER' in media_text and 'HYPRWALK_TURN_SERVER' in media_text
assert 'local_candidates=' in media_text and 'remote_candidates=' in media_text
assert 'URI omitted for credential safety' in media_text
print("PASS: count/byte-bounded non-leaky RTP queue, decoded-frame-only drop, 16 stream/window bound and ICE diagnostics/config contract")
