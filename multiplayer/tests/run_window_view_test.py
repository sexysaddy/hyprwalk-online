#!/usr/bin/env python3
"""Run actual receiving placeholder raster code, no compositor/GPU."""
from pathlib import Path
import argparse,subprocess,shlex
p=argparse.ArgumentParser();p.add_argument('fork',type=Path);p.add_argument('output',type=Path);p.add_argument('--abi-dir',type=Path);a=p.parse_args()
f=a.fork.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);m=Path(__file__).resolve().parents[1]
flags=['-std=c++23','-O1','-g1','-pthread','-DHYPRWALK_MULTIPLAYER','-I'+str(m/'include'),'-I'+str(f/'src')]
if a.abi_dir:flags+=['-I'+str(a.abi_dir.resolve())]
else:flags+=shlex.split(subprocess.check_output(['pkg-config','--cflags','cairo','pangocairo'],text=True))
core=['Multiplayer','Protocol','UdpProtocol','UdpTransport','PlayerSync','MapSync','ContentHash','OnlineMenu','WindowSync','WindowMenu']
cmd=['g++',*flags,str(m/'tests/window_view_test.cpp'),str(f/'src/remote_windows.cpp'),*[str(m/'build'/(x+'.o')) for x in core],'-Wl,-l:libcairo.so.2','-Wl,-l:libpangocairo-1.0.so.0','-Wl,-l:libpango-1.0.so.0','-Wl,-l:libgobject-2.0.so.0','-o',str(out/'window_view_test')]
print(shlex.join(cmd),flush=True);subprocess.run(cmd,check=True);subprocess.run([str(out/'window_view_test'),str(out/'images')],check=True)
