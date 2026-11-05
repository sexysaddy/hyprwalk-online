#!/usr/bin/env python3
"""Static regression contracts for the Hyprland 0.57 plugin API migration."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
compat = (ROOT / "shared/plugin/src/compat.hpp").read_text(encoding="utf-8")
panels = (ROOT / "shared/plugin/src/panels.cpp").read_text(encoding="utf-8")

assert "Hyprland 0.57+ API compatibility helpers" in compat
assert "Hyprwalk requires Hyprland 0.57+ headers" in compat
assert "hyprland/src/render/Context.hpp" in compat
assert "hyprland/src/desktop/DesktopTypes.hpp" in compat
assert "hyprland/src/workspace/HLWorkspace.hpp" in compat

for profile in ("egor", "stepan"):
    src = (ROOT / f"profiles/{profile}/plugin-overrides/src/main.cpp").read_text(encoding="utf-8")
    renderer = (ROOT / f"profiles/{profile}/plugin-overrides/src/renderer.cpp").read_text(encoding="utf-8")

    assert "#include <hyprland/src/desktop/view/window/WindowMetadata.hpp>" in src, f"{profile}: metadata API must be complete at call sites"
    assert "const Event::SRenderStageEvent& sev" in src, f"{profile}: old render-stage callback signature"
    assert "const auto mon = sev.monitor" in src, f"{profile}: monitor must come from render event"
    assert "sev.context->get()" in src or "auto& ctx = sev.context->get()" in src, f"{profile}: no live render context"
    assert "CMonitor::attemptDirectScanout(" in src, f"{profile}: no pre-render direct-scanout hook"
    assert "if (!m_hookDirectScanout)" in src, f"{profile}: entering 3D must fail closed if scanout hook is missing"
    assert "bool hkDirectScanout(void* self)" in src
    assert "g_p3D->rendersOn(self)" in src
    assert "m_menu.onlineKey(k, bool(mods & Input::HL_MODIFIER_SHIFT)" in src, f"{profile}: ModifierMask must be converted explicitly"
    assert "w->metadata().initialAppID()" in src, f"{profile}: class fallback lost initialAppID"
    assert "auto parent = w->backend().parent()" in src, f"{profile}: window parent must use 0.57 backend API"
    assert "w->mapped()" in renderer, f"{profile}: capture code still uses removed mapped field"
    assert "using FnSoftCursor = void (*)(void*, Render::CRenderContext&, PHLMONITOR" in src, f"{profile}: software-cursor hook ABI missing render context"
    assert "void hkSoftCursors(void* self, Render::CRenderContext& ctx, PHLMONITOR" in src, f"{profile}: software-cursor trampoline ABI mismatch"
    assert "m_original)(self, ctx, mon, now, damage, at, flags...)" in src, f"{profile}: software-cursor original call drops render context"
    assert "using FnDiscard    = void (*)(CSurfacePassElement*, Render::CRenderContext&);" in src, f"{profile}: discard hook type doesn't match 0.57"
    if profile == "egor":
        assert "void hkDiscard(CSurfacePassElement* self, Render::CRenderContext& ctx)" in src
        assert "((FnDiscard)g_discard->m_original)(self, ctx);" in src

    for obsolete in ("m_directScanoutBlocked", "m_renderData", "m_renderPass", "m_prevDSBlocked"):
        assert obsolete not in src, f"{profile}: obsolete Hyprland renderer field remains: {obsolete}"
    # setCursorHidden belongs only to the cursor-mode hook; it must not stand in for DS blocking.
    assert src.count("setCursorHidden(") == 1, f"{profile}: cursor hiding is still used as a direct-scanout workaround"

assert "w->presentation().alphaValue" in panels
assert "w->presentation().floatingOffset()" in panels
assert "w->presentation().visibleOnMonitor(mon)" in panels
assert "ls->popupHead()" in panels
assert "parent->backend().parent()" in panels
print("PASS: both profiles use 0.57 render contexts, cursor-hook ABI, monitor scanout, metadata/backend/window APIs")
