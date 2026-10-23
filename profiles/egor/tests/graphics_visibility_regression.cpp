#include "graphics_visibility.hpp"
#include <cmath>
#include <iostream>
#include <limits>

using namespace hyprwalk;
using namespace hyprwalk::graphics_visibility;
static void require(bool condition, const char* msg) {
    if (!condition) { std::cerr << "FAIL: " << msg << '\n'; std::exit(1); }
}
int main() {
    const auto f = SFrustum::fromMatrix(M4::identity());
    require(f.valid, "identity clip frustum should be valid");
    require(remoteWindowVisible(f, {-0.4f,-0.4f,0}, {1,0,0}, {0,1,0}, .5f, .5f), "visible remote window rejected");
    require(!remoteWindowVisible(f, {3,0,0}, {1,0,0}, {0,1,0}, .5f, .5f), "remote window behind side frustum submitted");
    require(!remoteWindowVisible(f, {0,0,3}, {1,0,0}, {0,1,0}, .5f, .5f), "remote window behind far plane submitted");
    require(!remoteWindowVisible(f, {0,1.05f,0}, {1,0,0}, {0,1,0}, .4f, .4f), "invisible panel should cull");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    require(remoteWindowVisible(f, {nan,0,0}, {1,0,0}, {0,1,0}, .5f, .5f), "invalid geometry must fail open");
    require(remoteWindowVisible(SFrustum{}, {3,0,0}, {1,0,0}, {0,1,0}, .5f, .5f), "invalid frustum must fail open");
    require(shouldCapturePanel(true, false, false, true, 2), "visible window must capture");
    require(shouldCapturePanel(false, true, false, true, 2), "outgoing WebRTC window must capture even when offscreen");
    require(!shouldCapturePanel(false, false, false, true, 2), "invisible unshared window must skip capture");
    require(shouldCapturePanel(false, false, true, false, 2), "first shadow/light capture required");
    require(!shouldCapturePanel(false, false, true, true, 2), "cached offscreen caster should throttle");
    require(shouldCapturePanel(false, false, true, true, 30), "cached offscreen caster should refresh eventually");
    require(shouldCapturePanel(true, false, false, true, 31), "turning back must immediately refresh window");

    // One-sided windows are rejected by viewer position, NOT by the direction
    // the player looks. This test uses only eye/plane data, so turning around
    // while remaining behind a window cannot cause it to reappear.
    require(frontSideVisible({0,0,1}, {0,0,0}, {0,0,1}), "window front must render");
    require(!frontSideVisible({0,0,-1}, {0,0,0}, {0,0,1}), "window back must disappear");
    require(frontSideVisible({0,0,0}, {0,0,0}, {0,0,1}), "window plane boundary must remain visible");
    require(frontSideVisible({1,0,0}, {0,0,0}, {1,0,0}), "rotated window front must render");
    require(!frontSideVisible({-1,0,0}, {0,0,0}, {1,0,0}), "rotated window back must disappear");
    require(frontSideVisible({0,0,-1}, {0,0,0}, {0,0,0}), "degenerate normals fail open");
    require(frontSideVisible({nan,0,0}, {0,0,0}, {0,0,1}), "invalid camera fails open");
    require(frontSideVisible({0,0,-1}, {0,0,0}, {nan,0,1}), "invalid pose fails open");

    // Cross(down, right) must agree with the renderer's uFront sign.
    // Right +X and down -Y imply the front side is +Z.
    require(remoteWindowFrontVisible({0,0,2}, {0,0,0}, {1,0,0}, {0,-1,0}), "remote window front must render");
    require(!remoteWindowFrontVisible({0,0,-2}, {0,0,0}, {1,0,0}, {0,-1,0}), "remote window back must disappear");
    require(remoteWindowFrontVisible({0,0,-2}, {0,0,0}, {0,0,0}, {0,-1,0}), "degenerate remote window fails open");

    // Outbound capture must still run behind the plane; visual culling is not
    // an excuse to freeze the image being transmitted to another computer.
    require(shouldCapturePanel(frontSideVisible({0,0,-1},{0,0,0},{0,0,1}), true, false, true, 33),
            "backside window must keep its active WebRTC capture");
    require(!shouldCapturePanel(frontSideVisible({0,0,-1},{0,0,0},{0,0,1}), false, false, true, 33),
            "backside unshared window must skip GPU capture");
    require(shouldCapturePanel(frontSideVisible({0,0,1},{0,0,0},{0,0,1}), false, false, true, 34),
            "returning to front must refresh window immediately");
    std::cout << "PASS: 26 graphics visibility/capture cases (front-only panels and WebRTC continuity included)\n";
}
