# Hyprwalk

An experimental 3D desktop for Hyprland: walk around your desktop in first person, explore maps, and share selected windows with another Hyprwalk user.

> **Status:** unfinished hobby project, not a stable release. Build and runtime compatibility depends on your exact Hyprland version. Multiplayer signaling is not authenticated or encrypted; use only with people you trust over a private LAN or VPN.

## Highlights

- **First-person 3D desktop:** walk, run and jump through a world containing desktop windows, maps and avatars.
- **Two builds in one source tree:** choose the `egor` or `stepan` profile at build time.
- **Egor profile:** Counter-Strike/Source-inspired movement with configurable CS-style bunnyhop, camera FOV and aspect ratio/stretch; optional video viewmodels from MP4/WebM clips, plus GoldSrc `.mdl` model viewmodels.
- **Stepan profile:** an alternative renderer/gameplay variant with extra visibility culling and rendering/performance optimizations.
- **Window sharing:** explicitly selected or consented windows can be streamed as VP8 over WebRTC. Capture follows actual window-surface updates by default (no fixed 30-FPS limit); an optional explicit 1–30 FPS cap remains available for constrained setups. The receiver keeps the last frame visible while the source is idle.
- **Local effects and lip-sync:** Hyprwalk's own sounds and local microphone-driven lip-sync are separate from window sharing.

**Window-stream audio is intentionally not included in this snapshot.** The unfinished capture/Opus-streaming implementation and its UI have been removed; window sharing is video-only for now.

## Build

On Arch Linux, install the development dependencies for the Hyprland instance you're running, plus a C++ compiler, `make`, `pkg-config`, Python 3 and the GStreamer WebRTC/VP8 development and runtime plugins. Hyprland plugin ABI compatibility matters: a successful compile against another version does not guarantee that it will load.

```bash
# Source checks, profile assembly, graphics/viewmodel tests and both standalone cores
./test.sh --jobs 4

# Standalone multiplayer core build/tests only
./build.sh --profile egor --mode core --jobs 4
./build.sh --profile stepan --mode core --jobs 4

# Build a Hyprland plugin with VP8/WebRTC window sharing
./build.sh --profile egor --mode video --jobs 4
./build.sh --profile stepan --mode video --jobs 4

# Build without the multiplayer module / Online menu
./build.sh --profile egor --mode offline --jobs 4
```

`core` builds and tests the standalone multiplayer module; it does **not** compile a Hyprland plugin. `video` requires GStreamer 1.22+ development modules and runtime factories for WebRTC, DTLS/SRTP and VP8. `offline` builds the plugin with multiplayer compiled out. The build script checks dependencies before assembling/compiling the profile and refuses to overwrite an existing output file.

## Repository layout

- `shared/plugin/` and `multiplayer/` — common source.
- `profiles/egor/` and `profiles/stepan/` — profile-specific overrides, settings and tests.
- `tools/assemble_profile.py` — assembles the selected profile into a temporary build tree.
- `tests/` — source contracts and regression checks.
- `docs/` — architecture, security and test notes.

Edit common files in the shared directories; use a profile override only when the implementations actually differ. `SOURCE_MANIFESTS.json` records hashes used by the profile-assembly checks.

## Egor viewmodel assets

The optional CS 1.6 `.mdl` model code is present, but `v_knife.mdl` is not bundled because redistribution permission has not been confirmed. Supply your own lawful copy for model-dependent tests or runtime use. MP4/WebM video viewmodels need the appropriate GStreamer video packages.

## Security and status

The TCP signaling layer does not authenticate peers. WebRTC's DTLS/SRTP media encryption does not fix that separate trust problem. Do not expose the signaling listener to arbitrary Internet peers. See [Security](docs/security.md), [Architecture](docs/architecture.md), and [Testing](docs/testing.md). Third-party notices are collected in [THIRD_PARTY.md](THIRD_PARTY.md); upstream code and asset redistribution rights should be reviewed before publishing a binary release.
