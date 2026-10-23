#!/usr/bin/env python3
"""Regression guards for deferred WebRTC RTP-pad routing and per-track stats."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
for profile in ('egor', 'stepan'):
    source = (ROOT / 'profiles' / profile / 'multiplayer-overrides/media/gstreamer/MediaSession.cpp').read_text()
    assert 'gst_pad_query_caps(pad,nullptr)' not in source, f'{profile}: do not route on template/query caps'
    assert 'padName=="src_0"' not in source and 'padName=="src_1"' not in source, f'{profile}: do not guess media from pad index'
    assert 'static GstPadProbeReturn incomingCapsProbe' in source, f'{profile}: missing deferred CAPS-event routing'
    assert 'GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM' in source and 'GST_EVENT_TYPE(event)!=GST_EVENT_CAPS' in source
    assert 'linkIncomingRtpPad(*state->track,pad,caps)' in source, f'{profile}: negotiated caps do not choose the receiver branch'
    assert 'codec!="vp8"||clockRate!=90000' in source, f'{profile}: incoming video must be verified as VP8/90kHz'
    assert 'queue name=rx_rtp_queue max-size-buffers=512 max-size-time=0 max-size-bytes=4194304 leaky=no' in source, f'{profile}: bounded RTP queue must not block on timestamps alone'
    assert 'rtpvp8depay name=vp8_depay' in source
    assert 'appsink name=decoded sync=false async=false' in source, f'{profile}: video appsink must not hold pipeline preroll'
    assert 'gst_element_get_state(t.pipeline,&currentState,&pendingState,0)' in source, f'{profile}: actual pipeline state is missing from diagnostics'
    assert 't->depayProbeId=gst_pad_add_probe' in source, f'{profile}: no counter at depayloader output'
    assert 'depayBuffers.load' in source, f'{profile}: depay output is not visible in diagnostics'
    assert 'decoderProbe' in source and 'decoderBuffers.load' in source, f'{profile}: VP8 decoder output is not counted separately'
    assert 'current-level-buffers' in source and 'current-level-time' in source, f'{profile}: receiver queue pressure is not visible'
    assert 'std::map<VideoTrackKey,std::string> stats;' in source, f'{profile}: stats are not keyed to the exact publication/stream'
    assert 'g_signal_emit_by_name(t.rtc,"get-stats",selector,p)' in source, f'{profile}: get-stats must select the track pad'
    assert 'summarizeWebRtcStats(reply,t.sending)' in source, f'{profile}: stats parser must respect track direction'
    assert 'g_ascii_strcasecmp(mime, "video/VP8")' in source, f'{profile}: stats must identify actual VP8 codec, not any video/RTX record'
    assert 'getNonnegativeUint(codec, "clock-rate", clockRate) && clockRate == 90000' in source, f'{profile}: VP8 stats must require the 90 kHz codec clock'
    assert 'Payload numbers are dynamic SDP identifiers' in source, f'{profile}: stats matching must not mistake dynamic payload IDs for codec identity'
    assert '!outbound.found' in source and '!inbound.found' in source, f'{profile}: duplicate records must not overwrite selected VP8 counters'
    # The encoder's GOP must follow the submitted cadence, not only an optional FPS cap.
    # When the cap is 0 (variable source cadence), a bounded fallback is still required.
    assert 'keyframeDistanceForInputFps(double measuredFps,unsigned fallbackFps)' in source, f'{profile}: missing measured-FPS GOP helper'
    assert 'keyframe-max-dist="<<std::max(1U,settings.fps?settings.fps:60U)' in source, f'{profile}: variable-rate default must still have a bounded initial keyframe interval'
    assert 'g_object_set(t.encoder,"keyframe-max-dist",wanted,nullptr)' in source, f'{profile}: runtime GOP is not adjusted for actual input cadence'
    assert 'keyframeRequestsAccepted' in source and 'keyframeRequestsUnhandled' in source, f'{profile}: keyframe feedback outcome is not reported'
    assert 'keyframe_max_dist=' in source, f'{profile}: active GOP distance is missing from diagnostics'
    assert 'stats.erase(t.key)' in source, f'{profile}: retired stream stats can linger'
    assert 'const auto state=m->states.find(x.first.window)' in source, f'{profile}: stats cleanup must look up state by WindowKey'
    assert 'state->second.key!=x.first' in source, f'{profile}: stale publication/stream stats must be pruned by full VideoTrackKey'
    print(f'PASS: {profile} waits for negotiated RTP caps and scopes diagnostics to the active video track')
print('NOTE: source-contract only; live GStreamer pad negotiation and decoding still require runtime testing.')
