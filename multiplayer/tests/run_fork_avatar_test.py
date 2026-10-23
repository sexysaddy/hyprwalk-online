#!/usr/bin/env python3
"""Build actual fork avatar/loader code. No compositor or GL required.
#embed is expanded losslessly for pre-C++26 test compilers; production sources untouched.
"""
import argparse,pathlib,subprocess,re,shlex,os
p=argparse.ArgumentParser();p.add_argument('fork',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);p.add_argument('--compiler',default='g++');p.add_argument('--sanitize',choices=['address','thread']);a=p.parse_args()
f=a.fork.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);m=pathlib.Path(__file__).resolve().parents[1]
flags=['-std=c++23','-O0','-g1','-pthread','-ffunction-sections','-fdata-sections','-DHYPRWALK_MULTIPLAYER','-I'+str(f/'src'),'-I'+str(m/'include')]
if a.sanitize:flags+=['-fsanitize='+('address,undefined' if a.sanitize=='address' else 'thread'),'-fno-omit-frame-pointer']
objects=[]
for name in ['avatar','gltf','third_party','sound','remote_avatar']:
    source=f/'src'/(name+'.cpp');text=source.read_text()
    if '#embed ' in text:
        def expand(match):
            data=(source.parent/match[1]).read_bytes();return ','.join(map(str,data))
        text,n=re.subn(r'^#embed "([^"]+)"\s*$',expand,text,flags=re.M)
        expanded=out/(name+'-expanded.cpp');expanded.write_text(text);source=expanded
        print('Losslessly expanded',n,'#embed directives for',name,flush=True)
    obj=out/(name+'.o');cmd=[a.compiler,*flags,'-c',str(source),'-o',str(obj)];print(shlex.join(cmd),flush=True);subprocess.run(cmd,check=True);objects.append(str(obj))
for name in ['Protocol','UdpProtocol']:
    obj=out/(name+'.o');cmd=[a.compiler,*flags,'-c',str(m/'src'/(name+'.cpp')),'-o',str(obj)];subprocess.run(cmd,check=True);objects.append(str(obj))
exe=out/'fork_avatar_test';cmd=[a.compiler,*flags,str(m/'tests/fork_avatar_test.cpp'),*objects,'-Wl,--gc-sections','-o',str(exe)];print(shlex.join(cmd),flush=True);subprocess.run(cmd,check=True)
subprocess.run(['python3',str(m/'tests/make_avatar_fixture.py'),str(out/'fixtures')],check=True)
env=os.environ.copy()
if a.sanitize=='address':env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
if a.sanitize=='thread':env.update(TSAN_OPTIONS='halt_on_error=1')
subprocess.run([str(exe),str(out/'fixtures')],check=True,env=env)
