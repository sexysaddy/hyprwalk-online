# Architecture

## Source layout

- `shared/plugin/` contains plugin files shared by both variants.
- `multiplayer/` contains the common standalone multiplayer module.
- `profiles/egor/` and `profiles/stepan/` contain files that are profile-specific or intentionally different.
- `tools/assemble_profile.py` creates a disposable compatibility tree for the selected profile; edit the canonical source tree, not generated files.

## Runtime outline

Hyprwalk is a Hyprland plugin with a 3D first-person desktop scene. Multiplayer control/signaling uses the project's TCP/UDP protocol. Window sharing publishes explicitly selected/consented window metadata and streams video using VP8 over GStreamer WebRTC; audio is not part of the current window-sharing implementation. Surface-change-driven capture avoids repeatedly reading an unchanged window, and receivers retain the last decoded frame during idle periods.

## Profiles

- **Egor:** Source/CS-inspired movement, bunnyhop, configurable camera FOV/aspect stretch, and optional video/GoldSrc model viewmodels.
- **Stepan:** alternative renderer/gameplay implementation with additional visibility and performance optimizations.

Keep common behavior in shared files and maintain distinct profile overrides only when needed. `SOURCE_MANIFESTS.json` is generated integrity metadata for the profile-assembly tests.
