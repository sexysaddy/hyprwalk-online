#!/usr/bin/env python3
"""Contracts for video-only WebRTC and offline build modes."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
build = (ROOT / 'build.sh').read_text()
preflight = (ROOT / 'tools/check-webrtc.sh').read_text()
assert '[--mode video|offline|core]' in build
assert 'video-audio' not in build
assert 'HYPRWALK_AUDIO' not in build
assert 'HYPRWALK_WEBRTC_AUDIO' not in build
assert 'offline) HYPRWALK_MULTIPLAYER=0; HYPRWALK_WEBRTC=0' in build
assert 'HYPRWALK_MULTIPLAYER="$HYPRWALK_MULTIPLAYER"' in build
assert build.index('if ((CHECK_ONLY)); then') < build.index('python3 "$ROOT/tools/assemble_profile.py"'), 'check-only preflight must not assemble the source overlay'
assert 'libpipewire-0.3.so' not in build
assert all(x in preflight for x in ('webrtcbin','vp8enc','vp8dec','rtpvp8pay','rtpvp8depay'))
assert 'opusenc' not in preflight and 'rtpopuspay' not in preflight and 'libpipewire-0.3' not in preflight
ice = (ROOT / 'multiplayer/media/gstreamer/IceSignaling.hpp').read_text()
assert 'only m-line 0 (video) is supported' in ice
for profile in ('egor', 'stepan'):
    make = (ROOT / 'profiles' / profile / 'plugin-overrides' / 'Makefile').read_text()
    media = (ROOT / 'profiles' / profile / 'multiplayer-overrides/media/gstreamer/MediaSession.cpp').read_text()
    menu = (ROOT / 'profiles' / profile / 'multiplayer-overrides/src/WindowMenu.cpp').read_text()
    ui = (ROOT / 'profiles' / profile / 'plugin-overrides/src/remote_windows.cpp').read_text()
    menu_push = 'p.items.push_back({.label = "Online"'
    pos = menu_push in (ROOT / 'profiles' / profile / 'plugin-overrides/src/menu.cpp').read_text()
    assert pos, f'{profile}: Online menu item missing'
    assert 'ifeq ($(HYPRWALK_MULTIPLAYER),1)' in make
    assert 'OBJ += build/multiplayer/VideoSignaling.o' in make
    assert 'HYPRWALK_WEBRTC_AUDIO' not in make and 'HYPRWALK_AUDIO' not in make
    assert 'opusenc' not in media and 'rtpopus' not in media and 'PcmMixer' not in media
    assert 'AudioCapability' not in (ROOT / 'profiles' / profile / 'multiplayer-overrides/src/Multiplayer.cpp').read_text()
    assert 'Window Video' in menu
    assert 'Audio unavailable' not in ui and 'Volume' not in ui
for p in ROOT.rglob('*'):
    if p == ROOT / 'tests/test_build_modes.py':
        continue
    if p.is_file() and p.suffix in {'.cpp','.hpp','.h','.sh','.py','Makefile'}:
        if 'shared/plugin/src/sound.cpp' in p.as_posix() or '/mic.cpp' in p.as_posix():
            continue
        text=p.read_text(errors='ignore')
        assert 'HYPRWALK_WEBRTC_AUDIO' not in text, f'stale streaming-audio macro in {p.relative_to(ROOT)}'
        assert 'AudioCapability' not in text, f'stale streaming-audio capability in {p.relative_to(ROOT)}'
print('PASS: video-only WebRTC wiring, offline mode, and removed window-stream audio contract')
