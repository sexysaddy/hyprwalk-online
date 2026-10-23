#!/usr/bin/env python3
"""No Hyprland SDK needed: verify UI/settings/shader data flow in the bundled fork."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
main=(root/'fork/src/main.cpp').read_text()
menu=(root/'fork/src/menu.hpp').read_text()
renderer=(root/'fork/src/renderer.cpp').read_text()
model_h=(root/'fork/src/renderer.hpp').read_text()
shader=(root/'fork/src/shaders.hpp').read_text()
for suffix in ('x','y','z'):
    key=f'model_rot_{suffix}'
    assert main.count(f'"{key}"')>=5, key
    assert f'slider("Rotation {suffix.upper()}' in main, key
    assert f'f.model.rotation{suffix.upper()} = settingFloat("{key}")' in main, key
    assert f'm_settingFloats.erase("{key}")' in main, key
assert 'MA_VIEWMODEL_RESET_ROTATION' in menu
assert 'key == "model_rot_x" || key == "model_rot_y" || key == "model_rot_z") return -180.f;' in main
assert 'key == "model_rot_x" || key == "model_rot_y" || key == "model_rot_z") return 180.f;' in main
assert 'm_modelRotationValid' in renderer and 'model->framing(' in renderer
assert 'glUniformMatrix3fv(U(m_progModel, "uRotation")' in renderer
assert 'uniform mat3 uRotation;' in shader and 'uniform vec3 uPivot;' in shader
assert 'vec3 posed = uPivot + uRotation * (aPos - uPivot);' in shader
assert 'if (!modelMode)' in main
assert 'm_settingStrings["viewmodel_mode"] = model ? "model" : "video";' in main
print('PASS: model-only More dials, -180..180 degree limits, runtime wiring, reset and INI persistence')
