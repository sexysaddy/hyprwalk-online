#pragma once

// Pure camera/graphics helpers. No Hyprland, networking, or media dependencies.
// Invalid data fails open so a corrupt transform cannot permanently hide an object.

#include "frustum.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hyprwalk::graphics_visibility {

    inline bool finite(const V3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    // A panel is a one-sided plane: its outward normal points at the side
    // carrying the window's content. Test the VIEWER'S POSITION, not the camera
    // forward vector, so looking backwards while standing behind a window
    // does not make its rear surface reappear.
    //
    // Treat an eye exactly on the plane as on the front side. Invalid or
    // degenerate transforms fail open (matching the frustum helpers), since
    // it is safer to display a corrupt panel than make it impossible to find.
    inline bool frontSideVisible(const V3& eye, const V3& origin, const V3& normal) {
        if (!finite(eye) || !finite(origin) || !finite(normal) || dot(normal, normal) < 1e-10f)
            return true;
        return dot(eye - origin, normal) >= 0.f;
    }

    // Remote windows store a right/down basis instead of an explicit normal.
    // The renderer's uFront uses exactly cross(down, right) for this surface.
    inline bool remoteWindowFrontVisible(const V3& eye, const V3& origin, const V3& right, const V3& down) {
        if (!finite(right) || !finite(down))
            return true;
        return frontSideVisible(eye, origin, cross(down, right));
    }

    // A remote window is a rectangle in world space.
    inline bool remoteWindowVisible(const SFrustum& frustum, const V3& origin, const V3& right,
                                    const V3& down, float width, float height) {
        if (!frustum.valid || !finite(origin) || !finite(right) || !finite(down) ||
            !std::isfinite(width) || !std::isfinite(height) ||
            width <= 0.f || height <= 0.f || width > 10000.f || height > 10000.f)
            return true;
        const V3 w = right * width, h = down * height;
        SAABB bounds = SAABB::empty();
        for (const V3 point : {origin, origin + w, origin + h, origin + w + h})
            bounds.grow(point);
        return frustum.intersects(bounds);
    }

    // Compositor texture capture is a separate GPU pass from drawing a 3D panel.
    // Never stop refreshing a panel exported to WebRTC: the peer must keep
    // receiving new pixels even if the local camera is pointing elsewhere.
    // Offscreen shadow/light casters are refreshed infrequently so their effects
    // remain approximately current without requiring a full-resolution FBO pass
    // on every frame. Re-entry into the frustum always refreshes immediately.
    inline bool shouldCapturePanel(bool cameraVisible, bool publishing, bool affectsLightingOrShadow,
                                   bool hasPreviousCapture, std::uint64_t frame) {
        return cameraVisible || publishing ||
               (affectsLightingOrShadow && (!hasPreviousCapture || frame % 30u == 0u));
    }
}
