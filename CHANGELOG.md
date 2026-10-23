# Changelog

## Unreleased — public video-only snapshot

- Window streaming uses VP8/WebRTC video only; unfinished window-audio capture, Opus transport and remote-window audio controls are removed.
- Window capture follows source-surface commits by default with no fixed 30-FPS limit; an optional explicit 1–30 FPS cap remains available.
- Receivers retain the last decoded frame while a shared window is idle instead of replacing it with a waiting placeholder.
- Unified the supplied Egor and Stepan trees behind explicit build profiles and retained profile-specific gameplay/rendering features.
