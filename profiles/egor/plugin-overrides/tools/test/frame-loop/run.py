#!/usr/bin/env python3
"""Execute extracted production render-stage/timer methods in an explicit compositor model.
This checks event order, NOT real Hyprland presentation, hardware stutter or FPS.
The only rewritten expression in current production methods is the clock source.
"""
from pathlib import Path
import os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
source=(ROOT/'src/main.cpp').read_text()
def body(text,start):
    a=text.index(start);a=text.index('{',a);p=a+1;depth=1
    while depth:
        depth+=(text[p]=='{')-(text[p]=='}');p+=1
    return text[a:p]
def method(signature):
    return signature+body(source,signature)
methods='\n'.join(method(s) for s in (
'int CDesktop3D::fpsLimit() const ', 'bool CDesktop3D::needsContinuousFrame() const ',
'void CDesktop3D::cancelFrameTimer() ', 'void CDesktop3D::scheduleFrameTimer(int delayMs) ',
'void CDesktop3D::requestFrame() '))
methods+='\nvoid CDesktop3D::onRenderStage(eRenderStage stage) '+body(source,'[this](eRenderStage stage)')
timer=source[source.index('    m_frameTimer = wl_event_loop_add_timer('):]
methods+='\nint CDesktop3D::timerCallback(void* data) '+body(timer,'[](void* data)')
methods+='\nvoid CDesktop3D::legacyStage(eRenderStage stage) {\n'+(HERE/'previous-post.inc').read_text()+'\n}'
methods=methods.replace('std::chrono::steady_clock::now()', 'testNow()')
fixture=(HERE/'fixture.cpp').read_text().replace('// INSERT METHODS',methods)
with tempfile.TemporaryDirectory(prefix='hyprwalk-loop-') as td:
    td=Path(td); unit=td/'unit.cpp';unit.write_text(fixture);binary=td/'unit'
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror',
                    '-Wno-missing-field-initializers','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                    '-fno-pie','-no-pie','-I'+str(ROOT/'src'),str(unit),'-o',str(binary)],check=True)
    env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=1'
    subprocess.run([str(binary)],check=True,env=env)
