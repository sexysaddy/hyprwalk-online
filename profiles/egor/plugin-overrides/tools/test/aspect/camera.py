#!/usr/bin/env python3
"""Execute the real aspect/FOV/visible-height helpers without Hyprland headers.

Only the monitor/config containers are stubs; method bodies are extracted from
main.cpp. This checks camera parameters and logical output geometry, not input
devices or live compositor integration.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
text = (ROOT / 'src/main.cpp').read_text()


def function(signature: str) -> str:
    a = text.index(signature)
    p = text.index('{', a) + 1
    depth = 1
    while depth:
        depth += (text[p] == '{') - (text[p] == '}')
        p += 1
    return text[a:p]


source = r'''
#include "viewport.hpp"
#include "math3d.hpp"
#include <cassert>
#include <cstdio>
#include <memory>
#include <numbers>
#include <string>
#include <unordered_map>
using namespace hyprwalk;
constexpr float F_PI = std::numbers::pi_v<float>;
struct Size { double x = 1920, y = 1080; };
struct Monitor { Size m_transformedSize; };
struct CDesktop3D {
    std::weak_ptr<Monitor> m_monitor;
    struct Screen { Size logicalSize; } m_screen;
    float m_fovCurrent = 90.f;
    std::unordered_map<std::string,float> settings;
    float settingFloat(const std::string& key) const { return settings.at(key); }
    float viewAspectRatio() const;
    float viewHeightLogical() const;
    float fovY() const;
};
'''
source += '\n'.join(function(signature) for signature in (
    'float CDesktop3D::viewAspectRatio()', 'float CDesktop3D::viewHeightLogical()',
    'float CDesktop3D::fovY()',
))
source += r'''
int main() {
    CDesktop3D desktop;
    int cases = 0;
    for (auto [w,h] : {std::pair{1920,1080}, {1920,1200}, {2520,1080}, {1080,1920}, {317,197}})
    for (double monitorScale : {1.,1.25,2.}) for (bool connected : {false,true}) {
        const auto mon = std::make_shared<Monitor>();
        mon->m_transformedSize = {(double)w,(double)h};
        desktop.m_monitor = connected ? mon : std::shared_ptr<Monitor>{};
        desktop.m_screen.logicalSize = {w/monitorScale,h/monitorScale};
        const int actualW = connected ? w : (int)std::lround(w/monitorScale);
        const int actualH = connected ? h : (int)std::lround(h/monitorScale);
        for (float fov : {60.f,90.f,120.f}) for (float preset : {0.f,1.f,2.f,3.f,4.f})
        for (float x : {50.f,100.f,200.f}) for (float y : {50.f,100.f,200.f}) {
            desktop.settings = {{"aspect_ratio",preset},{"scale_x",x},{"scale_y",y}};
            desktop.m_fovCurrent = fov;
            const auto c = fitContent(actualW,actualH,contentAspect((float)actualW/actualH,preset,x,y));
            const float expectedAspect = (float)c.width/c.height;
            for (int scale : RENDER_SCALES) {
                desktop.settings["render_scale"] = (float)scale;
                assert(desktop.fovY() == fov*F_PI/180.f);
                assert(desktop.viewAspectRatio() == expectedAspect);
                assert(desktop.viewHeightLogical() == (float)(h/monitorScale));
                const auto projection = M4::perspective(desktop.fovY(),desktop.viewAspectRatio(),.05f,200.f);
                assert(std::abs(projection.m[5] - 1.f/std::tan(fov*F_PI/360.f)) < 1.e-6f);
                assert(desktop.m_fovCurrent == fov);
                ++cases;
            }
        }
    }
    std::printf("PASS: %d actual camera-helper combinations; user vertical FOV unchanged, source aspect independent of Render Scale, full logical output height, monitor/fallback/HiDPI sizes\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix='hyprwalk-camera-') as tmp:
    unit = Path(tmp) / 'camera.cpp'
    binary = Path(tmp) / 'camera'
    unit.write_text(source)
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', '-O1',
                    '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
                    '-I', str(ROOT/'src'), str(unit), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
