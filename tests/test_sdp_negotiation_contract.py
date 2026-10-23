#!/usr/bin/env python3
"""Regression guard that per-window WebRTC SDP is video-only in this snapshot."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
for profile in ('egor', 'stepan'):
    source = (ROOT / 'profiles' / profile / 'multiplayer-overrides/media/gstreamer/MediaSession.cpp').read_text()
    parser = source[source.index('static GstWebRTCSessionDescription* parseDescription'):source.index('std::unique_ptr<Track> create')]
    assert 'mediaHasRtpCodec(video,"vp8")' in parser, f'{profile}: VP8 must be selected by rtpmap/format mapping'
    assert 'rtpopus' not in source and 'opusenc' not in source and 'opusdec' not in source
    assert 'audio' not in parser.lower(), f'{profile}: audio negotiation must not be part of video-only SDP parsing'
    assert '96 vp8/90000' not in parser.lower(), f'{profile}: hard-coded VP8 payload ID still rejects valid SDP'
    assert 'm-lines=' in parser and 'expected exactly one video m-line' in parser, f'{profile}: SDP diagnostics must describe the single video m-line'
    print(f'PASS: {profile} accepts video-only VP8 SDP without an Opus branch')
print('NOTE: source contract only; live SDP negotiation still needs the target GStreamer runtime.')
