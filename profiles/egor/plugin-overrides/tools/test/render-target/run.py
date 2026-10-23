#!/usr/bin/env python3
"""Real GLES integration test of extracted production renderer target methods.

Runs the actual CRenderer::render, FBO allocation, resolve, HUD/video/crosshair
passes, shaders, GL state guard and projection. Scene/world services are replaced
by synthetic geometry. This is NOT a full plugin, Hyprland damage/presentation,
asset decoder, map, shadow or avatar animation test. Requires C++20, Python 3 and
runtime libEGL with GLES 3 (Mesa surfaceless EGL by default).
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
renderer = (ROOT / 'src/renderer.cpp').read_text()
header = (ROOT / 'src/renderer.hpp').read_text()


def between(text: str, start: str, end: str) -> str:
    a = text.index(start)
    return text[a:text.index(end, a)]


def function(text: str, signature: str) -> str:
    a = text.index(signature)
    b = text.index('{', a)
    depth = 1
    p = b + 1
    while depth:
        depth += (text[p] == '{') - (text[p] == '}')
        p += 1
    return text[a:p]


gl_header = (ROOT / 'src/gl.hpp').read_text()
gl_source = (ROOT / 'src/gl.cpp').read_text()
production = 'namespace hyprwalk { namespace gl {\n'
production += between(gl_header, '    class CStateGuard', '    // a single-sampled')
production += between(gl_source, '    static GLuint compile(', '    bool hasExtension(')
production += between(gl_source, '    CStateGuard::CStateGuard()', '    bool STarget::ensure(')
production += '}\n'
production += between(header, '    struct SHudImage', '    class CRenderer')
production = production.replace('SAvatarFrame         avatar;', 'int                  avatar = 0;')
production += between(renderer, '        class CUnpackGuard', '        GLuint whiteTexture()')
production += '}\n'
methods = '\n'.join(function(renderer, signature) for signature in (
    'void CRenderer::releaseScaleTarget(', 'void CRenderer::releaseSceneTargets(',
    'bool CRenderer::ensureScaleTarget(', 'bool CRenderer::ensureMSAA(',
    'void CRenderer::drawSky(',
    'void CRenderer::drawVideo(', 'void CRenderer::drawCrosshair(',
    'void CRenderer::drawHud(', 'void CRenderer::drawHudImage(',
    'bool CRenderer::render(',
))
methods=methods.replace('void CRenderer::drawSky(', 'void CRenderer::drawProductionSky(')
methods += '\n' + (HERE/'previous-sky.inc').read_text()
methods += '\n' + (HERE/'letterbox-reference.inc').read_text()
methods += '\n' + function(renderer, 'std::string withCommon(')
methods += '\n' + function(renderer, 'void uMat(')
methods += '\n' + function(renderer, 'void uVec3(')
source = (HERE / 'fixture.cpp').read_text()
source = source.replace('// INSERT PRODUCTION TYPES', production)
source = source.replace('// INSERT PRODUCTION METHODS', methods)
with tempfile.TemporaryDirectory(prefix='hyprwalk-gles-') as tmp:
    tmp = Path(tmp)
    unit, binary = tmp / 'unit.cpp', tmp / 'unit'
    unit.write_text(source)
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', '-O2',
                    '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers',
                    '-I', str(ROOT / 'src'), '-I', str(HERE), str(unit),
                    '-l:libEGL.so.1', '-o', str(binary)], check=True)
    env = os.environ.copy()
    env.setdefault('EGL_PLATFORM', 'surfaceless')
    env.setdefault('LIBGL_ALWAYS_SOFTWARE', '1')
    env.setdefault('LP_NUM_THREADS', '2')
    subprocess.run([str(binary)], check=True, env=env)
