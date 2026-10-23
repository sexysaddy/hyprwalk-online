# Testing

Run the unified source checks and both standalone multiplayer core suites:

```bash
./test.sh --jobs 4
```

For the lightweight source-contract checks without standalone core or graphics builds:

```bash
./test.sh --skip-core --skip-graphics --jobs 4
```

The suite checks shell/Python syntax, package hygiene, profile assembly/manifests, WebRTC video-only build contracts, video error isolation and selected graphics/viewmodel regressions. Core mode does not compile or load the Hyprland plugin. A full plugin build and runtime acceptance test must use a matching Hyprland development SDK.

For a real two-peer acceptance test, confirm plugin load/unload, window selection/privacy, dynamic surface updates, frozen last frames during idle periods, WebRTC ICE/DTLS and decoded/rendered video. Try both LAN and private VPN. Check performance with a game under load; source contracts do not prove real-time performance.

The optional GoldSrc MDL tests need a lawful local model copy:

```bash
HYPRWALK_TEST_GOLDSRC_MDL=/path/to/v_knife.mdl ./test.sh --jobs 4
```

Tests requiring that asset should skip when the variable is absent.
