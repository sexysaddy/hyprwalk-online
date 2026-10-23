#!/usr/bin/env python3
"""Source-level regression guards; these do not replace plugin/runtime tests."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[1]
for profile in ('egor','stepan'):
    prof=ROOT/'profiles'/profile
    main=(prof/'plugin-overrides/src/main.cpp').read_text()
    assert 'VideoContext context{state.session,state.localId,state.peerId,state.worldTicket,ready,{},{}};' in main, f'{profile}: VideoContext must initialize only sending and receiving vectors'
    video_types=(prof/'multiplayer-overrides/include/hyprwalk/mp/VideoTypes.hpp').read_text()
    assert 'width = 8192, height = 8192, fps = 0, bitrate = 2500000' in video_types, profile
    menu=(prof/'multiplayer-overrides/src/WindowMenu.cpp').read_text()
    root=menu.split('if(page==Root)return {"Windows",{',1)[1].split('if(page==root+":mode")',1)[0]
    assert 'Stop Sharing' not in root and 'Window Video' not in root, profile
    assert 'link("Debug"' in root, profile
    debug=menu.split('if(page==root+":debug")',1)[1].split('const auto p=split(page);',1)[0]
    assert 'Errors' in debug and 'Privacy' in debug and 'Window Video' in debug, profile
    renderer=(prof/'plugin-overrides/src/renderer.cpp').read_text()
    assert 'if (publishing && videoFps && !videoDue)' in renderer, profile
    assert 'surface->m_events.commit.listen' in renderer, profile
    assert 'videoSourceHash == ' in renderer, profile
    assert 'if (publishing && videoFps)' in renderer and 'nextVideoCapture = now + videoInterval' in renderer, profile
    capture=renderer.split('void CRenderer::captureVideoPanels',1)[1].split('\n    }',1)[0]
    retain_key=capture.index('valid.push_back(request.key)')
    cache_check=capture.index('if(gl==m_panelGL.end()||!gl->second.target.fbo)continue;')
    assert retain_key < cache_check, f'{profile}: an authorized stream must remain retained while its cached source is between refreshes'
    assert 'lastSeen!=m_frame' not in capture, f'{profile}: a render-frame marker must not destroy in-flight asynchronous readbacks'
    assert 'if(g.lastUsed==m_frame)sources.push_back' in capture, f'{profile}: only freshly updated FBOs may issue readbacks'
    renderer_header=(prof/'plugin-overrides/src/renderer.hpp').read_text()
    assert 'lastSeen' in renderer_header, f'{profile}: cached-panel lifetime needs a separate lastSeen marker'
    due_at=renderer.index('if (publishing && videoFps && !videoDue)')
    assert 'revision->second.lastSeen = m_frame' in renderer[due_at:due_at+1200], f'{profile}: keep commit listeners alive between video refreshes'
    assert 'm_frame - kv.second.lastSeen > 30' in renderer, f'{profile}: cached FBO pruning must use visibility, not capture cadence'
    assert 'm_frame - entry.second.lastSeen > 120' in renderer, f'{profile}: surface commit listeners must survive low explicit FPS caps'
    if profile=='stepan':
        assert 'const int lod = publishing ? 0' in renderer, profile
        assert 'if (!resized && g.hasContent && g.contentHash == h)' in renderer, profile
    media=(prof/'multiplayer-overrides/media/gstreamer/MediaSession.cpp').read_text()
    assert 'frameFingerprint(frame)' not in media, f'{profile}: do not hash every full-resolution RGBA frame after commit-driven capture'
    assert 'sourceActive&&now-lastUseful>1800ms' in media, f'{profile}: idle windows must not trigger endless keyframe recovery requests'
    assert 'gst_video_event_new_upstream_force_key_unit' in media, profile
    assert 'gst_pad_send_event(t.encodedSrc,event)' in media, profile
    assert 'keyframe-max-dist=' in media and 'wait-for-keyframe=true' in media, profile
    assert 'rtp_packets=' in media and 'rtp_bytes=' in media and 'keyframe_requests=' in media, profile
    assert 'counter_point=' in media, f'{profile}: packet counter observation point must be explicit'
    assert 'targetVideoBitrate(settings,width,height)' in media, profile
    helper=media.split('unsigned keyframeDistanceForInputFps(',1)[1].split('\n}',1)[0]
    fallback_at=helper.index('if(!fallbackFps)')
    finite_at=helper.index('if(!std::isfinite(fps)')
    assert fallback_at < finite_at, f'{profile}: zero-FPS default must be normalized before keyframe fallback'
    assert '"framerate",GST_TYPE_FRACTION,static_cast<int>(settings.fps),1' in media, f'{profile}: appsrc caps must reflect the configured variable/fixed cadence'
    assert 'now-t.lastFrame>=1s' not in media and 'Clock::now()-it->second->time>=1s' not in media, f'{profile}: idle age must not evict the last decoded frame'
    assert 'Clock::now()-f->time<1s' not in media, f'{profile}: stale age must not prune displayed texture'
    assert 'do-nack' in media and 'on-new-transceiver' in media, f'{profile}: RTP recovery feedback must be enabled on sender and receiver transceivers'
    assert 'summarizeWebRtcStats(reply,t.sending)' in media and 'get-stats\",selector,p' in media and 'packets-lost' in media and 'nack-count' in media, f'{profile}: WebRTC stats must expose real RTP loss/recovery information'
    assert 'selected-candidate-pair-id' in media and 'available-outgoing-bitrate' in media, f'{profile}: WebRTC stats should expose selected route state without endpoint addresses'
    assert 'rtpvp8pay pt=96 picture-id-mode=15-bit mtu=1000' in media, f'{profile}: encapsulated media packets must fit conservative VPN path MTUs'
    assert 'rtcp-fb-nack=(boolean)true' in media and 'rtcp-fb-nack-pli=(boolean)true' in media, f'{profile}: VP8 caps must negotiate NACK/PLI feedback'
    assert 'keyframeBackoffStep' in media, f'{profile}: recovery requests must back off when decode never resumes'
    assert 'targetVideoBitrate(const VideoSettings& settings,unsigned width,unsigned height)' in video_types, profile
    test=(prof/'multiplayer-overrides/tests/video_test.cpp').read_text()
    assert 'w>8192' in media and 'h>8192' in media, profile
    assert 'fitVideoSize(1200,500,s)==std::pair{1200U,500U}' in test, profile
    assert 'fitVideoSize(1201,501,s)==std::pair{1201U,501U}' in test, profile
    assert 'fitVideoSize(3840,2160,s)==std::pair{3840U,2160U}' in test, profile
    assert 'fitVideoSize(16384,8192,s)==std::pair{8192U,4096U}' in test, profile
    assert 'f.width=1201;f.height=501' in test, profile
online=(ROOT/'multiplayer/src/OnlineMenu.cpp').read_text()
assert 'A requested connection is not yet a confirmed connection' not in online, 'online errors must report real state instead of generic TCP/UDP/Map advice'
assert 'No explicit error was reported.' in online and 'worldStateName(s.world)' in online, 'online fallback should show the actual state gates'
readback=(ROOT/'multiplayer/media/gl/Readback.cpp').read_text()
readback_header=(ROOT/'multiplayer/media/gl/Readback.hpp').read_text()
assert 'fenceTimeouts' in readback_header and '++m->metrics.fenceTimeouts' in readback
assert 'if(waited==GL_TIMEOUT_EXPIRED&&age<=std::chrono::milliseconds(500))continue;' in readback, 'recent fences must be polled without blocking'
assert 'if(waited==GL_WAIT_FAILED||age>std::chrono::milliseconds(500))' in readback, 'stale/failed fences must be dropped'
assert '++m->metrics.fenceTimeouts' in readback, 'stale timeout diagnostics must be counted'
assert 'for(const auto& slot:entry.second.slots)if(slot.fence)++m->metrics.dropped' in readback, 'revoking a source must not silently hide in-flight readbacks from the dropped counter'
assert 'for(const auto& slot:source.slots)if(slot.fence)++m->metrics.dropped' in readback, 'clear/context reset must count outstanding work cancellation'
for profile in ('egor','stepan'):
    main=(ROOT/'profiles'/profile/'plugin-overrides/src/main.cpp').read_text()
    assert 'pending_estimate=' in main, f'{profile}: readback status should expose outstanding work estimate'
print('PASS: variable source-cadence capture, persistent last-frame display, asynchronous readback and profile contracts')
