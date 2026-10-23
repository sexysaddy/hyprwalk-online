#!/usr/bin/env python3
"""Compile exact fork menu/control sources with a HUD POD fixture; no Hyprland/GL.
Default: installed cairo/pangocairo dev headers. --abi-dir is an explicitly
limited test fallback containing declarations for actual runtime libraries.
"""
import argparse, pathlib, re, shlex, subprocess
p=argparse.ArgumentParser()
p.add_argument('fork',type=pathlib.Path);p.add_argument('module',type=pathlib.Path);p.add_argument('output',type=pathlib.Path)
p.add_argument('--compiler',default='g++');p.add_argument('--disabled',action='store_true');p.add_argument('--abi-dir',type=pathlib.Path)
p.add_argument('--sanitize',action='store_true'); a=p.parse_args()
f=a.fork.resolve(); m=a.module.resolve(); out=a.output.resolve(); out.mkdir(parents=True,exist_ok=True)
for name in ['menu.cpp','menu.hpp','control.cpp','control.hpp']:
    text=(f/'src'/name).read_text()
    if name=='menu.hpp':
        assert text.count('#include "renderer.hpp"')==1
        text=text.replace('#include "renderer.hpp"','#include "hud_fixture.hpp"')
    (out/name).write_text(text)
r=(f/'src/renderer.hpp').read_text(); match=re.search(r'    struct SHudImage \{.*?\n    \};',r,re.S); assert match
(out/'hud_fixture.hpp').write_text('#pragma once\n#include <vector>\n#include <cstdint>\nnamespace hyprwalk {\n'+match[0]+'\n}\n')
if a.abi_dir:
    cflags=['-I'+str(a.abi_dir.resolve())]
    libs=['-Wl,-l:libcairo.so.2','-Wl,-l:libpangocairo-1.0.so.0','-Wl,-l:libpango-1.0.so.0','-Wl,-l:libgobject-2.0.so.0']
else:
    cflags=shlex.split(subprocess.check_output(['pkg-config','--cflags','cairo','pangocairo'],text=True))
    libs=shlex.split(subprocess.check_output(['pkg-config','--libs','cairo','pangocairo'],text=True))
flags=['-std=c++23','-O1','-g','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-Wno-unused-parameter','-ffunction-sections','-fdata-sections','-pthread']
if a.sanitize: flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
if not a.disabled: flags+=['-DHYPRWALK_MULTIPLAYER']
flags+=cflags+['-I'+str(out),'-I'+str(m/'include'),'-I'+str(f/'src')]
objects=[]
for path in [out/'menu.cpp',out/'control.cpp']+([] if a.disabled else [m/'src/OnlineMenu.cpp']):
    obj=out/(path.stem+'.o'); cmd=[a.compiler,*flags,'-c',str(path),'-o',str(obj)]
    print(shlex.join(cmd),flush=True);subprocess.run(cmd,check=True);objects.append(str(obj))
# Link the real prebuilt common Core. No Core symbols are linked in disabled mode.
if not a.disabled:
    core=m/('build-asan' if a.sanitize else 'build')
    for name in ['VideoSignaling', 'Multiplayer', 'Protocol', 'UdpProtocol', 'UdpTransport', 'PlayerSync', 'MapSync', 'ContentHash', 'WindowSync', 'WindowMenu']:
        obj=core/(name+'.o')
        if not obj.is_file(): raise SystemExit('Build the common module first; missing '+str(obj))
        objects.append(str(obj))

cmd=[a.compiler,*flags,str(pathlib.Path(__file__).with_name('menu_probe.cpp')),*objects,'-Wl,--gc-sections',*libs,'-o',str(out/'menu_probe')]
print(shlex.join(cmd),flush=True);subprocess.run(cmd,check=True)
subprocess.run([str(out/'menu_probe'),str(out/'images')],check=True)
