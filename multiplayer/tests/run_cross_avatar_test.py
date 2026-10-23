#!/usr/bin/env python3
"""Two real processes, each linked to its fork's twelve Makefile objects and avatar implementation.
Requires run_fork_avatar_test.py output directories (normal GCC), and per-fork core objects.
No compositor, GL, media or model transfer. Generated CLI extends the unchanged player_cli fixture.
"""
import argparse,os,pathlib,subprocess,shlex
p=argparse.ArgumentParser();p.add_argument('egor',type=pathlib.Path);p.add_argument('stepan',type=pathlib.Path);p.add_argument('egor_objects',type=pathlib.Path);p.add_argument('stepan_objects',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);a=p.parse_args()
m=pathlib.Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
s=(m/'tests/player_cli.cpp').read_text().replace('#include <charconv>','#include <charconv>\n#include <cstdlib>\n#include "remote_avatar.hpp"')
s=s.replace('    Adapter adapter(argv[4]);','''    hyprwalk::CRemoteAvatar avatar;
    const char* modelPath=std::getenv(std::string(argv[4])=="egor"?"HYPRWALK_TEST_AVATAR_EGOR":"HYPRWALK_TEST_AVATAR_STEPAN");
    if(!modelPath) return 2;
    hyprwalk::SAvatarRequest request;request.path=modelPath;avatar.select(request,1);unsigned modelSamples=0;
    Adapter adapter(argv[4]);''')
s=s.replace('        core.dispatch();adapter.apply(core);','        core.dispatch();adapter.apply(core);avatar.take();if(!avatar.error().empty()){std::cerr<<avatar.error();failed=true;break;}')
s=s.replace('++adapter.sampled;}', '''++adapter.sampled;
            if(avatar.model()){
                avatar.pose(&sample->player,now,false);
                if(avatar.animator().joints().empty()||std::abs(avatar.transform().m[12]-sample->player.feet.x)>0.0001f){failed=true;break;}
                for(float x:avatar.animator().joints())if(!std::isfinite(x)){failed=true;break;}
                ++modelSamples;
            }
        }''')
s=s.replace('adapter.sampled>=20','adapter.sampled>=20&&modelSamples>=20')
s=s.replace('success=true;std::cout<<"WORLD_UDP_OK "','success=true;std::cout<<"REMOTE_MODEL_OK "<<modelPath<<" posed="<<modelSamples<<" animation="<<avatar.animator().playing()<<std::endl;std::cout<<"WORLD_UDP_OK "')
(out/'cross_avatar_cli.cpp').write_text(s)
for who,fork,objects in [('egor',a.egor,a.egor_objects),('stepan',a.stepan,a.stepan_objects)]:
    fork=fork.resolve();objects=objects.resolve()
    core=[fork/'build/multiplayer'/(n+'.o') for n in ['VideoSignaling','Multiplayer','Protocol','UdpProtocol','UdpTransport','PlayerSync','MapSync','ContentHash','OnlineMenu','WindowSync','WindowMenu']]
    avatar=[objects/(n+'.o') for n in ['avatar','gltf','third_party','sound','remote_avatar']]
    cmd=['g++','-std=c++23','-O0','-g1','-pthread','-DHYPRWALK_MULTIPLAYER','-DHYPRWALK_FORK_MAP','-I'+str(m/'include'),'-I'+str(fork/'src'),str(out/'cross_avatar_cli.cpp'),str(fork/'src/world.cpp'),str(fork/'src/collision.cpp'),*[str(x) for x in core+avatar],'-Wl,--gc-sections','-o',str(out/(who+'-cli'))]
    print(shlex.join(cmd),flush=True);subprocess.run(cmd,check=True)
env=os.environ.copy();env['HYPRWALK_TEST_AVATAR_EGOR']=str(a.egor_objects.resolve()/'fixtures/humanoid.glb');env['HYPRWALK_TEST_AVATAR_STEPAN']=str(a.stepan_objects.resolve()/'fixtures/clips.glb')
subprocess.run(['python3',str(m/'tests/player_process_test.py'),str(out/'egor-cli'),str(out/'stepan-cli'),str(out/'logs')],check=True,env=env)
for p in sorted((out/'logs').glob('*.log')):
    if '-host' in p.name or '-client' in p.name:
        text=p.read_text();assert 'REMOTE_MODEL_OK ' in text,p
        for line in text.splitlines():
            if line.startswith('REMOTE_MODEL_OK '): print(p.name,line,flush=True)
print('Both actual fork CPU avatar implementations pose received UDP snapshots in separate processes, both host roles. NOT real Hyprland/GL.')
