#!/usr/bin/env python3
"""Source-level integration checks for FPS pacing and the Graphics settings wiring."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
main = (ROOT / "src/main.cpp").read_text()
renderer_h = (ROOT / "src/renderer.hpp").read_text()
renderer_cpp = (ROOT / "src/renderer.cpp").read_text()
shaders = (ROOT / "src/shaders.hpp").read_text()
third_party = (ROOT / "src/third_party.cpp").read_text()
avatar = (ROOT / "src/avatar.cpp").read_text()
checks = []


def check(label: str, condition: bool) -> None:
    checks.append((label, condition))


# Compile failures reported for the prior version.
check("CDesktop3D declares fpsLimit", "int                              fpsLimit() const;" in main)
check("CDesktop3D declares needsContinuousFrame", "bool                             needsContinuousFrame() const;" in main)
check("CDesktop3D declares scheduleFrameTimer", "void                             scheduleFrameTimer(int delayMs);" in main)
check("CDesktop3D declares requestFrame", "void                             requestFrame();" in main)
drag_struct_start = main.find("} m_drag;")
drag_struct = main[max(0, main.rfind("    struct {", 0, drag_struct_start)):drag_struct_start + len("} m_drag;")] if drag_struct_start >= 0 else ""
frame_fn_start = main.find("bool CDesktop3D::needsContinuousFrame() const {")
frame_fn_end = main.find("\nvoid CDesktop3D::scheduleFrameTimer", frame_fn_start)
frame_fn = main[frame_fn_start:frame_fn_end] if frame_fn_start >= 0 and frame_fn_end >= 0 else ""
check("drag struct declares panel and surface, not key", "panel = 0" in drag_struct and "surface" in drag_struct and "m_drag.key" not in main)
check("continuous rendering checks actual drag surface state", "m_drag.surface" in frame_fn and "m_drag.key" not in frame_fn)
check("main.cpp includes stb_image declarations", '#include "third_party/stb_image.h"' in main)
check("stb_image implementation is defined exactly once", third_party.count("#define STB_IMAGE_IMPLEMENTATION") == 1)
check("unused avatar foot alias is removed", "const auto& fs = g.foot[s];" not in avatar)
check("avatar tick comparison uses matching unsigned types", "static_cast<unsigned>(k % GAIT_NEED_EVERY) == refreshTick" in avatar)

# Settings are discoverable, default to the old visual quality, and round-trip through settings.ini.
check("Graphics page is reachable from Settings", '"settings/graphics"' in main and '"Graphics"' in main)
default_on = 'if (key == "graphics_shadows" || key == "graphics_dynamic_lighting" || key == "graphics_antialiasing") return true;'
for key in ("graphics_shadows", "graphics_dynamic_lighting", "graphics_antialiasing"):
    check(f"{key} defaults on", default_on in main and key in default_on)
    check(f"{key} is writable and loadable", main.count(key) >= 4)
    check(f"{key} is passed to the renderer", f'settingBool("{key}")' in main)

# These toggles must reach actual renderer branches, not only menu labels.
check("frame params carry real graphics flags", all(x in renderer_h for x in ("shadowsEnabled", "dynamicLightingEnabled", "antialiasingEnabled")))
check("shadow update is conditional", "if (f.shadowsEnabled) {" in renderer_cpp and "updateShadow(f);" in renderer_cpp)
check("baked shadows are disabled with the shadow toggle", '(f.shadowsEnabled && gl.shadows) ? 1 : 0' in renderer_cpp)
check("dynamic light pass is skipped when disabled", "f.dynamicLightingEnabled ? updateLights(f, lights) : 0" in renderer_cpp)
check("MSAA target tracks sample count", "m_msaaSamples == samples" in renderer_cpp and "m_msaaSamples = samples;" in renderer_cpp)
check("MSAA-off path allocates single-sample targets", "else\n            glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);" in renderer_cpp)
check("shader avoids shadow-map taps when disabled", "if (uShadowsEnabled == 0)\n        return 1.0;" in shaders)
check("no fake volumetric-lighting toggle exists", "volumetric_lighting" not in main and "volumetric_lighting" not in renderer_cpp)

failed = [label for label, ok in checks if not ok]
for label, ok in checks:
    print(("ok   " if ok else "FAIL ") + label)
print(f"{len(checks) - len(failed)} passed, {len(failed)} failed")
sys.exit(bool(failed))
