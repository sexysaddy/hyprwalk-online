#!/usr/bin/env python3
"""Source-contract guard against renderer failures killing the whole media session."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
for profile in ('egor','stepan'):
    src=(ROOT/'profiles'/profile/'plugin-overrides/src/renderer.cpp').read_text()
    assert 'f.videoSession->fail(' not in src, f'{profile}: renderer still faults whole media session on a local video-path error'
    assert 'm_videoReadback.metrics().error' in src, f'{profile}: readback diagnostics disappeared'
    assert 'Decoded video texture upload failed for window ' in src, f'{profile}: upload error no longer identifies its source window'
    assert 'Decoded video texture allocation failed for window ' in src, f'{profile}: allocation error no longer identifies its source window'
    assert 'if(video&&video->key.window==w.key&&mp::validVideoFrame(*video)&&' in src, f'{profile}: one retained diagnostic error can suppress every receiving stream'
    print(f'PASS: {profile} local video-path failures no longer call global MediaSession::fail()')
print('NOTE: source-contract only; live GL/WebRTC fault injection remains required')
