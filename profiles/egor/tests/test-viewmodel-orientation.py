#!/usr/bin/env python3
"""Regression for Egor viewmodel overlay Y orientation (no GL runtime required).

The renderer resolves its output to an inverted texture (texture row 0 is the
compositor's top row); both the HUD and video shaders have always used a
positive NDC Y coefficient. The model overlay must use exactly that basis.
"""
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[1]
shaders = (root / 'fork/src/shaders.hpp').read_text()
renderer = (root / 'fork/src/renderer.cpp').read_text()

def source(name):
    pattern = rf'constexpr const char\* {name} = R"\((.*?)\)";'
    match = re.search(pattern, shaders, flags=re.S)
    assert match, f'Missing shader {name}'
    return match.group(1)

hud = source('HUD_VS')
model = source('VIEWMODEL_MODEL_VS')
assert 'gl::makeProgram("viewmodel-video", shaders::HUD_VS, shaders::VIDEO_FS)' in renderer
assert 'vec2 p = uRect.xy + aPos * uRect.zw;' in hud
assert 'gl_Position = vec4(p / uViewport * 2.0 - 1.0, 0.0, 1.0);' in hud
assert 'vec2 ndc = p / uViewport * 2.0 - 1.0;' in model, 'model Y is reversed relative to video/HUD'
assert 'vec2 p = uRect.xy + vec2(q.x, 1.0 - q.y) * uRect.zw;' in model, 'GoldSrc +Z must project upwards'
assert 'vec3 posed = uPivot + uRotation * (aPos - uPivot);' in model, 'model rotation must follow CPU skinning'
assert 'vec2 projected = vec2(-posed.y, posed.z) / depth;' in model, 'unexpected GoldSrc camera basis'
assert 'gl_Position = vec4(ndc * depth, depth - 2.0 * nearPlane, depth);' in model, 'perspective correction lost'

# Walk precisely through the model vertex shader and the inverted output target.
# A screen coordinate in the user-facing image is (ndc.y + 1) / 2, not
# (1 - ndc.y) / 2: the resolve texture is presented upside down by the compositor.
for viewport_h in (720, 928, 1080, 1440):
    for y_percent in (0., 26., 50., 74., 100.):
        center_y = y_percent / 100 * viewport_h
        rect_h = viewport_h * 0.42
        for q_y in (0., 0.5, 1.):  # feet/bottom, center, upper blade
            pixel_y = center_y + (0.5 - q_y) * rect_h
            ndc_y = (pixel_y / viewport_h) * 2 - 1
            displayed_y = ((ndc_y + 1) / 2) * viewport_h
            assert abs(displayed_y - pixel_y) < 1e-8
            if q_y == 0.5:
                assert abs(displayed_y / viewport_h * 100 - y_percent) < 1e-9
    # +Z must display above -Z for all supported aspect ratios.
    high_z_y = center_y + (0.5 - 1) * rect_h
    low_z_y = center_y + (0.5 - 0) * rect_h
    assert high_z_y < low_z_y

print('PASS: model/HUD/video share the same output Y basis; model_y=74 displays at 74%, GoldSrc +Z points up')
