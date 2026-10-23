# Using hyprwalk

Back to the [README](../../../../README.md).

## Controls

Enter and leave 3D with the `hyprwalk:toggle` dispatcher, `hyprctl hyprwalk toggle` or `hl.plugin.hyprwalk.toggle()`.
Keys held with Super or Ctrl+Alt still go to Hyprland. They act on the window under the crosshair: Super+Q closes
the window you look at, or nothing when you look at no window.

| Input | In 3D |
|---|---|
| mouse, arrow keys | look around (arrow keys are rebindable in Settings) |
| W A S D, Left Shift | walk (`walk_speed`), run (`run_speed`); steps up to 0.5 m are climbed |
| Space, Left Ctrl or C | jump, crouch (flying: up, down) |
| F | fly on/off |
| R | back to the start |
| clicks | click whatever the crosshair points at; a left click on nothing makes the avatar punch |
| wheel | scroll the window; on nothing in third person, zoom the camera |
| E or Enter | type into the window under the crosshair (every key goes to it); Super+Esc stops |
| P, Shift+P | play the window under the crosshair where it is, or filling the view; Super+Esc stops |
| Super+wheel | while playing a window in the tiling ring: bigger or smaller |
| G or H (unless H has been rebound) | pick up the window under the crosshair. G/H again or a left click puts it down where you look; Esc or a right click puts it back. While carrying: wheel = scale, Ctrl+wheel = distance, Shift+wheel = the window's real size |
| Shift+H | pin the window (or the one you carry) to the top right of your view; again takes it back |
| X | send a window back to the desktop wall |
| T, Shift+T | tiling mode: every window in a ring around you; Shift+T recentres the ring where you look |
| Y | the tiling ring follows you, or stays where it is |
| Q, B | the Apps page (launch into the world), the Windows page |
| `~` / grave | open or close Hyprwalk Settings while in 3D |
| Tab | the Action Menu |
| V | first / third person (needs an avatar) |
| F1–F8 | hand gestures; hold Left or Right Shift for one hand |
| Super+Esc | with another monitor: hand the mouse and keyboard to it (again: back) |
| Esc | leave 3D |

**Hyprwalk Settings** opens with grave/tilde while in 3D. Its five root groups are Movement, Camera / Display, Graphics, Environment and General. Controls is under Movement (through More); Viewmodel is under Camera / Display; Performance is under Graphics. Existing bindings, graphics switches and saved values retain their keys. More and Back keep the existing navigation history.

**Camera / Display** provides World FOV, sensitivity, dynamic FOV, Aspect Ratio, X Scale, Y Scale and the nested Viewmodel page. Aspect Ratio cycles Custom X/Y, 16:9, 16:10, 21:9 and 9:16. The completed scene is stretched independently in X/Y to fill the entire output, with no letterbox or pillarbox. World FOV keeps its existing vertical-angle semantics and is neither reset nor automatically increased to compensate for the stretch. Custom X/Y uses native aspect multiplied by X/Y. Equal X/Y values mean native aspect, not zoom. Existing 50-200% values are preserved. A narrower source aspect stretches horizontally; a wider one stretches vertically relative to its undistorted source. The complete source is sampled once in [0,1], never mirrored or repeated. Editing either X/Y dial selects Custom. These are NOT render-resolution controls. The independent Render Scale is in Graphics / Performance.

**Viewmodel / Hands** has a **Source** selector for the existing video path and a CS 1.6 model path. Video can select a local clip with `zenity` or `kdialog`, play/pause it, loop it, adjust screen position and width/height, preserve or independently adjust the displayed proportions, and tune green-key threshold/edge softness. The model path selects a GoldSrc StudioMDL `.mdl` file, including the bundled files under `assets/viewmodels/cs16/`, and exposes the same position, size, and aspect controls. Models are fully skinned from the StudioMDL skeleton: the supplied viewmodels include the original hands and held item, idle/draw/reload/shoot sequences advance from the embedded animation tables, left/right click plays the first matching attack sequence, and `R` plays reload or pin-pull sequences. The model path is a CPU-skinned screen-space overlay and does not use the avatar renderer or network code. The video path still requires a rebuild with GStreamer development packages (`gstreamer`, `gst-plugins-base` development files; the Makefile enables it automatically when `gstreamer-1.0`, `gstreamer-app-1.0` and `gstreamer-video-1.0` are available through pkg-config). On Arch Linux, install `gstreamer gst-plugins-base gst-plugins-good` and the matching development files provided by your distribution before rebuilding; for example, on Arch the pkg-config modules are normally supplied by `gstreamer` and `gst-plugins-base`. The video is a 2D screen-space overlay, not a 3D model, and it does not collide with the map.

If the build was made without GStreamer development packages, the rest of Hyprwalk still builds, but the video-hands toggle is disabled and says it requires a GStreamer-enabled rebuild.

**Performance** (`Settings / Graphics / Performance`) keeps Maximum FPS: 30, 45, 60, 90, 120 or Unlimited, default 60. Scheduling stays in the original LAST_MOMENT phase, using one-shot Wayland timers and phase-preserving deadlines; it never sleeps in the compositor thread. Unlimited means no plugin cap, not presentation faster than the compositor can provide. Completed output is reused between scene updates. The original fixed-step movement and continuous-animation/idle criteria remain. A low cap limits expensive scene updates, not all Hyprland work: another source of output damage can still composite the cached full-screen texture. Non-divisible output/cap rates cannot have identical refresh-quantised intervals without a different presentation mechanism. Live frame-time stability is NOT certified by the standalone simulations.

**Render Scale** (same Performance page) cycles 100%, 90%, 80%, 75%, 67%, 50%; default 100%. It changes internal scene color/depth/stencil targets, with matching MSAA dimensions, and resamples once across the full output using the same Aspect Ratio stretch. 1920x1080 native content becomes 960x540 at 50%. At 100% the source-aspect rectangle is rendered at full source resolution before stretching; lowering Render Scale changes sampling density, not the stretch. The desktop output mode and buffer are unchanged. Video hands and crosshair use native source coordinates and receive the same final X/Y stretch, independent of Render Scale. The radial menu/badge retain native output coordinates and unchanged input layout. 3D avatar rendering is part of the scaled scene. Shadow maps, captured window textures, geometry and CPU simulation do not scale with this control; no proportional FPS gain is promised. A rejected render target returns to the desktop rather than publishing a partial texture; this failure latch requires reloading the plugin after its cause is resolved.

**Graphics** (`Settings → Graphics`) provides three renderer-backed switches. `Shadows` skips the dynamic shadow update pass and shadow-map sampling (including baked shadow masks); `Dynamic lighting` skips calculating and uploading lights emitted by desktop panels/windows; `Anti-aliasing (MSAA)` switches the scene target between the device's supported multisample count (up to 4×) and a single-sample target. All default to on to preserve the existing appearance, apply live and are saved with the other settings. Volumetric lighting is not exposed because the current renderer does not implement a separate volumetric-lighting pass.

**Environment / Skybox** lets you choose a local PNG, JPEG, BMP or TGA image, enable or disable it, restore the standard sky, and tune brightness, zoom, rotation and vertical offset. The image is mapped as an equirectangular panorama; a 2:1 panorama is recommended, while ordinary rectangular images are accepted but may look stretched around the environment. Images above 32 megapixels or 16384×8192 are rejected to limit memory use. The decoded image is uploaded once and reused as a GPU texture. Custom skybox settings and its file path are saved in `~/.config/hyprwalk/settings.ini` (or `$XDG_CONFIG_HOME/hyprwalk/settings.ini`). The standard sky remains available if a selected image cannot be loaded.

**The Action Menu** has pages for emotes, expressions, gestures, the outfit, apps, windows, options, maps and avatars.
The mouse points, a left click picks, a right click goes back, a middle click closes. The wheel, 1–9, Enter,
Backspace and Esc work too. Sliders (VRChat's radial puppets) open a dial, and two-axis puppets open a stick.

**Launchers and shell menus** (rofi, fuzzel, a power menu: any layer surface that takes the keyboard) show over the
3D view and work as on the desktop. An app you start from one opens in front of you.

### Play mode

P gives the window what a game expects: keyboard focus and every key (Super and Ctrl+Alt combinations still go to
Hyprland), the buttons and wheel, and the mouse as the app asks for it (relative motion under a pointer lock, a
confined pointer, or a plain pointer with the app's own cursor). P plays the window where it is. Shift+P turns the
camera to fill the view with it (`play_view = fill` swaps the two). In the tiling ring a played window takes
`play_size` of the view, and Super+wheel resizes it.

A window that goes fullscreen in 3D (a video, a game) is played automatically. Play mode ends with Super+Esc, when
you leave 3D, when the screen locks or the window closes, or when another window or a launcher takes the keyboard.
In that last case the game's keys are held back, so stray keys don't trigger hyprwalk actions: P plays the game again,
and Super+Esc walks. Controllers are read by the game itself (SDL), and only while it has the keyboard focus.

### Windows in the world

- Apps launched from 3D (Q, or `hyprctl hyprwalk launch`) open in front of you, as big as on your screen.
  `app_rules` sets where by window class:
  `CLASS: DISTANCE [HEIGHT|auto] [left|right|SIDE]`, e.g.
  `steam_app_.*: 2.4 1.6, discord: 1.2 auto left` (metres from your eye, the height or `auto`, and a side). Built-in
  rules put games and video players 2 m away, chat apps 1.3 m away and to the left, and anything else 1.5 m away.
- Where you put a window is remembered per class and map in `$XDG_STATE_HOME/hyprwalk/windows/`; X forgets it.
- Other windows that open on the 3D monitor (a terminal, the portal's picker) open in front of you too. Dialogs
  follow their window.
- The Windows page (B) lists every window: focus, bring here, to the wall, pin/unpin, bigger/smaller (its real
  size), play, close.
- Tiling mode (T) is hyprwalk's own layout: Hyprland's tiling doesn't change.
- X11 apps work through XWayland, with their menus and tooltips drawn as popups. Drag and drop works, and so do
  input method popups (fcitx5).

### Two monitors

3D goes on the focused monitor, or the one `monitor` names. The others stay your normal desktop. Super+Esc, a
keybind that moves the focus to another monitor, or moving the mouse across hands the mouse and keyboard over; the
3D view keeps running. Coming back works the same way. For your own keybinds there's `hyprctl hyprwalk away`, the
`hyprwalk:away` dispatcher and `hl.plugin.hyprwalk.away()`.

### Lip sync

Off until you turn it on: `lipsync = true`, `hyprctl hyprwalk avatar lipsync on`, or the Action Menu's options.
While you're in 3D with an avatar it listens to your default microphone (or `lipsync_source`) through PipeWire, and a
red badge shows it's listening. Nothing is recorded or sent. It shows five vowels (a, i, u, e, o) and the consonants
pp, ff, ss and ch where the avatar has those visemes. The gain is automatic (`lipsync_gain` sets a fixed one), and
the badge tells you when the microphone is muted, silent or missing. `hyprctl hyprwalk avatar lipsync` reports the
details.

## Config

All values are `plugin:hyprwalk:…`. Paths may start with `~/`. A config reload applies changes at once; a value
changed at run time (`hyprctl keyword`) takes effect within a second. The values exist only once the plugin is
loaded; Hyprland reloads the config after loading a plugin. Movement speeds and jump velocity are metres/second, `gravity` is metres/second², and `ground_accel` / `air_accel` are Source-style acceleration factors per second. `fov` is the **vertical** camera angle in degrees (converted to radians for the perspective matrix).

```ini
plugin {
    hyprwalk {
        avatar = ~/avatars/me.glb
        fov = 90                 # vertical degrees; default 70
        mouse_sensitivity = 0.0022
        walk_speed = 1.6         # m/s
        run_speed = 4.5          # m/s, hold Left Shift
        ground_accel = 14        # Source acceleration factor per second
        ground_friction = 6
        air_accel = 10
        jump_speed = 6.3         # m/s vertical take-off velocity
        gravity = 20              # m/s^2
        max_speed = 12            # m/s, includes bhop speed gain
        bhop = true               # hold Space to auto-jump on landing
        air_strafing = true
        dynamic_fov = false       # optional speed-based FOV widening
        dynamic_fov_boost = 6     # extra degrees at high speed
    }
}
```

or, with a Lua config:

```lua
hl.config({ plugin = { hyprwalk = {
    avatar = "~/avatars/me.glb",
} } })
```

| Value | Default | |
|---|---|---|
| `layer_spacing` | `0.02` | metres between stacked layers (bars, notifications) on the desktop wall, 0–0.5 |
| `wallpaper` | `false` | keep the wallpaper on the desktop wall |
| `map` | `""` | a glTF/GLB map instead of the courtyard |
| `map_scale` | `0` | metres per map unit; 0 guesses |
| `avatar` | `""` | a glTF, GLB or VRM avatar |
| `avatar_height` | `0` | scale the avatar to this height in metres; 0 keeps its own |
| `avatar_physics` | `true` | spring bones (hair, skirts) |
| `first_person_body` | `true` | first person from the avatar's eyes with its body and hands; `false`: from 1.65 m, no body |
| `avatar_emotes` | `""` | more emotes: `.vrma` or glTF clips, files or folders, comma-separated |
| `emote_volume` | `0.5` | volume of emotes' sounds, 0–1 |
| `lipsync` | `false` | lip sync from the microphone |
| `lipsync_gain` | `auto` | microphone gain in dB (−20 to 60), or `auto` |
| `lipsync_source` | `""` | the microphone, by name or description as `wpctl status` lists it; `""` = default |
| `apps` | `""` | the Apps page's favourites: desktop ids, app names or commands, comma-separated |
| `app_rules` | `""` | where launched apps open (see Windows in the world), comma-separated |
| `pin_size` | `0.3` | share of the view's height a pinned window takes, 0.05–1 |
| `monitor` | `""` | the monitor for 3D: its name (`DP-1`) or `desc:` and the start of its description; `""` = focused |
| `walk_speed` | `1.6` | walking speed in m/s, 0.3–10 |
| `run_speed` | `4.5` | running speed in m/s while holding Left Shift, 0.5–15 |
| `ground_accel` | `14` | Source-style acceleration factor per second; higher reaches requested speed sooner, 0–100 |
| `ground_friction` | `6` | ground friction; higher stops horizontal movement faster after releasing/reversing input, 0–30 |
| `air_accel` | `10` | Source-style acceleration factor per second; controls A/D + mouse air-strafing strength, 0–100 |
| `jump_speed` | `6.3` | initial vertical jump velocity in m/s, 1–15 |
| `gravity` | `20` | downward acceleration in m/s², 1–50 |
| `max_speed` | `12` | hard horizontal speed cap in m/s, including bhop, 1–30 |
| `bhop` | `true` | holding Space auto-jumps on landing; `false` requires a fresh press for every jump |
| `air_strafing` | `true` | allow Source-style directional acceleration in air while preserving momentum |
| `fov` | `70` | vertical field of view in degrees, 60–120; changes smoothly at runtime |
| `mouse_sensitivity` | `0.0022` | camera rotation in radians per mouse count; `hyprctl hyprwalk sens VALUE` temporarily overrides it; `sens config` follows config again |
| `dynamic_fov` | `false` | smoothly widen FOV with horizontal speed instead of keeping it constant |
| `dynamic_fov_boost` | `6` | extra vertical FOV in degrees when `dynamic_fov` is enabled, 0–20 |
| `tiling` | `false` | start in tiling mode |
| `tiling_follow` | `true` | the tiling ring follows you; `false`: it stays where you turned it on |
| `play_view` | `here` | what P does: `here` (play in place) or `fill` (fill the view); Shift+P does the other |
| `play_size` | `0.5` | share of the view a window played in the tiling ring takes, 0.25–0.94 |

## hyprctl

`hyprctl hyprwalk` with no arguments prints the state as JSON.

| Command | |
|---|---|
| `status`, `toggle`, `on [MONITOR]`, `off [now]` | the state; enter or leave 3D |
| `away [on\|off\|toggle]` | hand the mouse and keyboard to another monitor, or take them back |
| `type [on\|off]` | type into the window under the crosshair |
| `play [on\|off\|toggle] [here\|fill]` | play mode; without arguments, what's played |
| `launch WHAT`, `apps` | launch an app into the world; list the desktop entries |
| `window SEL focus\|bring\|wall\|pin\|unpin\|bigger\|smaller\|size W H\|play\|close` | act on a window by address, class or title |
| `tile [on\|off\|toggle\|here\|follow [on\|off\|toggle]]` | tiling mode (T, Shift+T, Y) |
| `grab`, `place`, `hold DIST [SCALE]`, `pin`, `reset-windows [forget]` | carry, place and pin windows; put them all back on the wall |
| `windows`, `panels`, `camera`, `log [LINES]` | windows in the world, everything drawn, the camera, the plugin's recent log |
| `look DX DY`, `turn YAW PITCH`, `tp X Y Z`, `aim [WINDOW]` | move the view |
| `walk SECS [forward\|back\|left\|right]`, `jump`, `fly`, `click [left\|right\|middle]`, `sens [VALUE|config]` | input from a script |
| `map [PATH\|none\|reload\|forget\|scale S]`, `spawn [here]`, `desktop [here [HEIGHT]]` | the map, its start and where the desktop hangs |
| `view [first\|third\|toggle] [DIST] [SIDE]`, `view body [on\|off\|toggle]` | the camera |
| `avatar [PATH\|none\|reload\|height M]` | load an avatar, or print its state |
| `avatar expression [NAME [WEIGHT]\|none]`, `avatar gesture [left\|right\|both GESTURE]` | faces and hand gestures |
| `avatar parts [reset]`, `avatar toggle NAME [on\|off\|reset]`, `avatar slider NAME [VALUE\|NN%\|reset]`, `avatar slider NAME X Y`, `avatar shape KEY [WEIGHT\|reset]` | the outfit |
| `avatar physics [on\|off\|toggle]` | spring bones |
| `avatar lipsync [on\|off\|toggle\|gain DB\|auto\|source NAME\|default]` | lip sync; without arguments, what it hears |
| `avatar emote [NAME\|NUMBER\|FILE\|FOLDER [once\|loop]\|stop]`, `avatar attack [left\|right]` | emotes and punches |
| `menu [open [PAGE]\|close\|toggle\|back\|pick [N]\|move DX DY\|scroll N]` | the Action Menu |

Dispatchers: `hyprwalk:toggle`, `hyprwalk:menu [PAGE]`, `hyprwalk:play [here|fill|on|off|toggle]`, `hyprwalk:away` and
`hyprwalk:tile [here|follow]`. Lua functions: `hl.plugin.hyprwalk.toggle()`, `enter()`, `exit()`, `type()`,
`play([VIEW])`, `away()`, `tile(["here"|"follow"])` and `menu([PAGE])`.

## Known limits

- Only keys held with Super or Ctrl+Alt reach Hyprland in 3D, so Alt+Tab doesn't, and a game never gets Super
  combinations.
- Direct scanout is off while 3D is up, on every monitor. A fullscreen game is composited into the 3D view, with up to
  a frame of latency; `hyprctl hyprwalk status` shows `fps`, `updateMs` and `renderMs`.
- With Hyprland 0.56, windows and layers that close vanish in 3D without fading out.
- A dragged item's icon isn't drawn in 3D (the cursor shows "grabbing" instead).
- Changing a window's real size makes a tiled window floating. Window placement is remembered per class, so an app
  with several windows of one class is put back only when it has one.
- The tiling ring is one row: when crowded, its windows shrink instead of wrapping.
- Lip sync knows five Japanese vowels and four consonants. Other voices and languages can pick the wrong shape, and
  the automatic gain also hears other voices near you (a fixed `lipsync_gain` is steadier).
- Emote sounds are Ogg Vorbis only, come only from the settings file, and aren't positioned in the world.
- First person's hands are posed the same way for every avatar. The punches were made on Hatsune Miku NT and land a
  little differently on other builds.
- The built-in emotes are procedural look-alikes, since VRChat's own are proprietary. Load real ones as `.vrma` or
  glTF clips.
- The converter handles the Modular Avatar and VRCFury features avatars use most, not all of them. PhysBone curves
  and Gravity Falloff aren't carried over, and toon shading uses only the first shade step. It was tested on
  synthetic packages and free Booth items, not on paid avatars.
- Hyprland 0.55.x crashes when it quits with windows open, with or without hyprwalk (fixed for dwindle in 0.56.0). A
  patch is in `extras/hyprland-exit-crash`. hyprwalk works around aquamarine's headless-output bug (fixed in 0.12.1).
