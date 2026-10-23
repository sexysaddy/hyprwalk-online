#!/usr/bin/env python3
"""Run Stage3D VIDEO-capable control peers against actual supplied Stage3B/3C source.
No GStreamer/video transport: this tests capability downgrade, real TCP/UDP/world/windows.
Old source trees are read only; all objects are built in the explicit output directory.
"""
import argparse, pathlib, subprocess, shlex
p=argparse.ArgumentParser();p.add_argument('stage3b',type=pathlib.Path);p.add_argument('stage3c',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);a=p.parse_args()
m=pathlib.Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
core=[m/'build'/(x+'.o') for x in ('VideoSignaling','Multiplayer','Protocol','UdpProtocol','UdpTransport','PlayerSync','MapSync','ContentHash','OnlineMenu','WindowSync','WindowMenu')]
for stage,old,which,driver in [('3b',a.stage3b,'player_cli','player_process_test.py'),('3c',a.stage3c,'window_cli','window_process_test.py')]:
    old=old.resolve();build=out/('old-'+stage);binary=build/which
    cmd=['make','-C',str(old),'-j2','BUILD_DIR='+str(build),str(binary)]
    print(shlex.join(cmd),flush=True);subprocess.run(cmd,check=True)
    source=(m/'tests'/(which+'.cpp')).read_text().replace('Adapter adapter(argv[4]);Settings settings;', 'Adapter adapter(argv[4]);Settings settings;settings.enableVideo=settings.enableWindows=true;')
    new=out/('video-capable-'+which+'.cpp');new.write_text(source);exe=out/('video-capable-'+which)
    cmd=['g++','-std=c++20','-O1','-g1','-pthread','-I'+str(m/'include'),str(new),*[str(x) for x in core],'-o',str(exe)]
    print(shlex.join(cmd),flush=True);subprocess.run(cmd,check=True)
    subprocess.run(['python3',str(m/'tests'/driver),str(exe),str(binary),str(out/('against-'+stage))],check=True)
    print('PASS Stage3D VIDEO offer downgrades with actual Stage'+stage+' implementation, BOTH host roles; NOT WebRTC',flush=True)
