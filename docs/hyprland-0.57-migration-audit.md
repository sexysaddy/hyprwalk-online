# Hyprland 0.57.1 migration audit

## Scope

The two supplied archives were compared before editing. The `hyprwalk` TAR worktree has the same Git base revision as the original `hyprwalk-online-main` archive (`5282edd09b0f3865457782488dbfd8809e3c3cf9`). The DeepSeek version therefore was not a separate upstream revision: its tracked source changes were concentrated in the Egor profile and shared plugin files, while the Stepán profile was not migrated. It also carried prebuilt offline `.so` files, which are not valid substitutes for rebuilding against the current compositor ABI and are excluded from the repaired source package.

## Confirmed incompatibilities and repairs

1. **Keyboard modifier type.** Hyprland 0.57 exposes `Input::ModifierMask`; passing `mods & HL_MODIFIER_*` directly to `onlineKey(..., bool, ...)` does not perform the required implicit conversion. Both profiles now explicitly convert the mask result to `bool`.

2. **Render-stage event contract.** The old callback accepted an `eRenderStage`. In 0.57 it receives `Event::SRenderStageEvent`, which carries the monitor and an optional borrowed render context. PRE and POST do not contain a live context; `RENDER_LAST_MOMENT` does. Both profiles now use the event monitor/context and no longer reach into removed global `m_renderData` or `m_renderPass` renderer state.

3. **Pass-element virtual API.** Render-pass element methods now take `Render::CRenderContext&`. The custom 3D pass in each profile was updated to override the new signatures.

4. **Function-hook ABI.** `CPointerManager::renderSoftwareCursorsFor` now receives `Render::CRenderContext&` before the monitor argument. Egor's `CSurfacePassElement::discard` also receives the render context. The old trampolines had stale signatures and forwarded arguments in the wrong positions; these hooks now accept and forward the context. This is a plausible cause of failures on the first render after plugin load, but a runtime backtrace is still required to attribute any particular crash conclusively.

5. **Direct scanout.** The old `m_directScanoutBlocked` renderer field is no longer available. More importantly, 0.57 attempts direct scanout before emitting `render.pre`, so blocking it in that callback is too late. Both profiles now hook `CMonitor::attemptDirectScanout()` and return `false` only for the monitor currently rendered in 3D. Entering 3D fails closed if that hook cannot be installed. Cursor visibility is no longer used as a fake direct-scanout switch.

6. **Window and workspace API.** Window class/title, X11 traits, PID/parent, mapped state, popup heads, pin/floating flags, alpha/rounding/visibility and floating offsets were moved to the 0.57 metadata/backend/presentation/state interfaces. Both profiles and shared panel collection were migrated. `WindowMetadata.hpp` is included explicitly because `Window.hpp` only forward-declares the metadata type. X11 transient ownership now follows the backend parent chain instead of searching for a child of the intended parent.

7. **hyprctl plugin command API.** The old `SHyprCtlCommand` registration was replaced with `IPC::Socket1::SCommand`, `COMMAND_MATCH_PREFIX`, and an `SRequest`/`SResponse` handler.

8. **Configuration format.** Hyprland 0.57 removed the legacy `hyprland.conf`/hyprlang loader. This is separate from a plugin ABI crash. The README now documents loading the plugin from `hyprland.lua` (or with `hyprctl plugin load`) and applying plugin configuration after loading so its keys are registered.

9. **Plugin API-version gate.** After the first target-machine load reported `API version mismatch`, the upstream 0.57.0 and 0.57.1 `PluginAPI.hpp` files were checked directly: both define `HYPRLAND_API_VERSION` as `0.2`. The loader compares `pluginAPIVersion()` against its own `HYPRLAND_API_VERSION` before it invokes `pluginInit()`. The source already returns the header macro, so hardcoding the function would only bypass a safety check. A new build preflight prints the selected API macro and rejects stale/mixed headers on 0.57.0/0.57.1 before compilation. The mismatch still needs the target machine's selected header value and loaded `.so` path to determine whether the cause is stale headers or an old artifact.

## Validation performed

- `./test.sh --skip-core --jobs 4`: passed shell/Python syntax, package hygiene, both profile assemblies and SHA-256 manifests, build-mode contracts, 0.57 migration contracts, video error isolation, SDP negotiation, WebRTC pad routing, screen-capture contracts, mocked WebRTC preflight, and Egor graphics/viewmodel tests.
- GoldSrc model-dependent tests were skipped because the fixture `v_knife.mdl` is not bundled.
- Standalone `core` compilation was attempted for Egor but did not finish within the execution environment's 300-second command limit; it was terminated without a reported compiler diagnostic. The full test run also reached the core stage for both profiles before the outer command timed out; no compiler diagnostic was captured.
- The actual Hyprland plugin build and load could not be run in this environment: `pkg-config` cannot find `hyprland.pc`, and the matching Hyprland 0.57.1 development headers are not installed here. Consequently, the repaired source still needs a build and runtime smoke test on the target Arch installation.

## Target-machine acceptance commands

Run with the exact headers and `hyprland.pc` for the compositor instance that will load the resulting plugin:

```bash
./test.sh --jobs 8
./build.sh --profile egor --mode core --jobs 8
./build.sh --profile stepan --mode core --jobs 8
./build.sh --profile egor --mode offline --jobs 8
./build.sh --profile egor --mode video --jobs 8
./build.sh --profile stepan --mode offline --jobs 8
./build.sh --profile stepan --mode video --jobs 8
```

Then load the resulting `.so` into Hyprland 0.57.1 and test: idle desktop, entering/leaving 3D, software/hardware cursor transitions, fullscreen/direct-scanout transitions, X11 transient menus, window open/close, video stream, and plugin unload. Keep the Hyprland log/backtrace if the compositor still exits during `hl.plugin.load()`; that is needed to isolate any remaining runtime-only fault.
