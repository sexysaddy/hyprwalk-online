#!/usr/bin/env python3
"""No live Hyprland: explicit wrapper/test-double boundaries are in the generated source."""
from pathlib import Path
import re,sys,subprocess,shlex
import argparse
p=argparse.ArgumentParser(description="Exact main methods with real Core/Cairo/avatar, fake compositor/window wrappers")
p.add_argument('--webrtc',action='store_true');p.add_argument('--media-abi-dir',type=Path);p.add_argument('fork',type=Path);p.add_argument('output',type=Path);p.add_argument('--abi-dir',type=Path);options=p.parse_args()
f=options.fork.resolve();out=options.output.resolve();out.mkdir(parents=True,exist_ok=True)
m=Path(__file__).resolve().parents[1];s=(f/'src/main.cpp').read_text()
who='egor' if 'class EgorAdapter' in s else 'stepan'
t=(m/'tests/fixtures'/f'stage3b-main-{who}.cpp.in').read_text()
ui=out/'ui';cpu=out/'cpu'
cmd=[sys.executable,str(m/'tests/run_menu_probe.py'),str(f),str(m),str(ui)]
if options.abi_dir:cmd+=['--abi-dir',str(options.abi_dir.resolve())]
subprocess.run(cmd,check=True)
subprocess.run([sys.executable,str(m/'tests/run_fork_avatar_test.py'),str(f),str(cpu)],check=True)
def method(text,n):
 hit=re.search(r'^([^\n]* CDesktop3D::'+n+r'\([^\n]*\)[^\n]*\{)',text,re.M);assert hit,n
 return text[hit.start():text.index('\n}',hit.end())+2]
for n in ['multiplayerMapLocked','multiplayerApplyMap','multiplayerRestoreMap','shutdownMultiplayer','multiplayerUpdate','multiplayerEvents','multiplayerCommand','onlinePage','onlineAction','menuPick','clearMultiplayerAvatarGpu','shutdownMultiplayerAvatar','multiplayerAvatarUpdate','multiplayerAvatarFrame','multiplayerAvatarCommand']:
 t=t.replace(method(t,n),method(s,n))
if options.webrtc:
 t=t.replace('#include "remote_avatar.hpp"','#include "remote_avatar.hpp"\n#include "media/gstreamer/MediaSession.hpp"\n#include "media/gl/Readback.hpp"\n#include <deque>')
# Replace the complete real adapter declaration.
a=t.index('class '+who.title()+'Adapter');b=t.index('\n};',a)+3
c=s.index('class '+who.title()+'Adapter');d=s.index('\n};',c)+3
t=t[:a]+s[c:d]+t[b:]
t=t.replace('#include "remote_avatar.hpp"','#include "remote_avatar.hpp"\n#include "remote_windows.hpp"\n#include <hyprwalk/mp/WindowMenu.hpp>\n#include <map>')
fixture=r'''
// Only these window/compositor wrappers are TEST DOUBLES; methods under test are extracted verbatim.
struct PrivacyValue{bool value=false;bool valueOrDefault()const{return value;}};
struct Rules{PrivacyValue privacy;PrivacyValue& noScreenShare(){return privacy;}};
struct WindowDouble{bool mapped=true,hidden=false,parent=false,overrideRedirect=false;
 std::string m_initialClass="org.example.Player",m_class="org.example.Player",m_title="Permitted window";
 std::unique_ptr<Rules> m_ruleApplicator=std::make_unique<Rules>();
};
using PHLWINDOW=std::shared_ptr<WindowDouble>;using PHLWINDOWREF=std::weak_ptr<WindowDouble>;
bool toplevel(const PHLWINDOW& w){return w&&w->mapped&&!w->hidden&&!w->parent&&!w->overrideRedirect;}
constexpr int PANEL_LAYER=0,PANEL_WINDOW=1,PANEL_POPUP=2;
struct PanelDouble{int kind=PANEL_WINDOW;PHLWINDOWREF window;float alpha=1;bool front=false;
 uintptr_t key=1;struct Box{double w=800,h=400;}box;
 struct Pose{V3 origin{0,2,0},right{1,0,0},down{0,-1,0};float scale=.0025f;}pose;
};
struct LockManager{bool locked=false;bool isSessionLocked()const{return locked;}} lockManager;
auto* g_pSessionLockManager=&lockManager;
constexpr uint32_t BTN_LEFT_=272;
'''
helper=re.search(r'    V3 forwardFrom\(float yaw, float pitch\) \{.*?\n    \}',s,re.S).group(0)
fixture += '\n'+helper+'\n'
t=t.replace('constexpr uint32_t K_TAB',fixture+'\nconstexpr uint32_t K_TAB')
t=t.replace('SRemotePlayerFrame remotePlayer;};','SRemotePlayerFrame remotePlayer;const std::vector<SRemoteWindowFrame>* remoteWindows=nullptr;};')
t=t.replace('struct RendererDouble{int clears=0;', '''struct RendererDouble{int clears=0,windowsClears=0;
 bool hasRemoteWindows() const{return false;}void clearRemoteWindows(){++windowsClears;}
 void pruneRemoteWindows(const std::vector<mp::RemoteWindow>&){}''')
if options.webrtc:
 t=t.replace('struct SFrameParams {','struct SFrameParams {')
 # Exact public media/frame shapes; compositor/renderer calls below remain explicit doubles.
 t=t.replace('struct SFrameParams', 'struct SVideoCaptureRequest{uintptr_t panel=0;mp::VideoTrackKey key;};\nstruct SFrameParams',1)
 t=t.replace('const std::vector<SRemoteWindowFrame>* remoteWindows=nullptr;',
    'const std::vector<SRemoteWindowFrame>* remoteWindows=nullptr;mp::media::MediaSession* videoSession=nullptr;const std::vector<SVideoCaptureRequest>* videoCaptures=nullptr;')
 t=t.replace('struct RendererDouble{int clears=0,windowsClears=0;',
    'struct RendererDouble{int clears=0,windowsClears=0;mp::media::ReadbackMetrics metrics;\n'
    'void clearVideoResources(){} void pruneRemoteVideos(const std::vector<mp::VideoTrackKey>&){}\n'
    'const auto& videoReadbackMetrics()const{return metrics;}std::string videoError()const{return {};}')
a=s.index('    struct SLocalWindowSource');b=s.index('    std::unique_ptr<CRemoteAvatar>',a)
fields=s[a:b]
# The previous fixture has an intentionally minimal camera/input state.
t=t.replace('class CDesktop3D{public:', 'class CDesktop3D{public:\n'+fields+r'''
 std::vector<PanelDouble> m_panels;
 struct{V3 eye{1,2.1f,3};float yaw=0,pitch=0;}m_camera;
 struct{bool input=false;}m_shell;
 struct{bool on=false;}m_play,m_held;
 struct{uintptr_t key=0;}m_hold;
 bool m_typing=false;int m_aimed=-1;std::weak_ptr<int> m_aimSurface;
''')
t=t.replace('CActionMenu m_menu{[this](const std::string& id){return id=="main"?actionPage(id,{}):onlinePage(id);}};', '''CActionMenu m_menu{[this](const std::string& id){return id=="main"?actionPage(id,{}):onlinePage(id);},
 [this](const SMenuItem& it,float value,float){}};''')
names=['multiplayerWindowPreferenceChanged','clearMultiplayerWindows','multiplayerWindowsUpdate','multiplayerWindowsFrame','multiplayerWindowsAim','multiplayerWindowButton','multiplayerWindowWheel',]
if options.webrtc:names+=['startMultiplayerVideo','shutdownMultiplayerVideo','multiplayerVideoUpdate','multiplayerVideoFrame','multiplayerVideoCommand']
a=t.index('\nint main(');t=t[:a]+'\n'+'\n'.join(method(s,n) for n in names)+t[a:]
# Run profile workers only in this dedicated test directory; no developer/user settings touched.
t=t.replace('require(argc==2);','require(argc==2);setenv("XDG_CONFIG_HOME",(std::string(argv[1])+"/test-config-"+std::to_string(getpid())).c_str(),1);')
t=t.replace(' shutdownMultiplayer();\n#endif\n }',' shutdownMultiplayer();\n#endif\n }')
t=t.replace(' h.shutdownMultiplayer();c.shutdownMultiplayer();pump();',' h.shutdownMultiplayer();c.shutdownMultiplayer();h.c.pump();')
extra=(m/'tests/main_windows_test.inc').read_text()
marker=' // Missing model and explicit none fall back without disconnecting the world.'
assert marker in t;t=t.replace(marker,extra+marker)
(out/'main_probe.cpp').write_text(t)
flags=['-std=c++23','-O0','-g1','-pthread','-ffunction-sections','-fdata-sections','-DHYPRWALK_MULTIPLAYER','-I'+str(ui),'-I'+str(f/'src'),'-I'+str(m/'include')]
if options.webrtc:flags+=['-DHYPRWALK_WEBRTC','-I'+str(m)]
if options.abi_dir:flags+=['-I'+str(options.abi_dir.resolve())]
else:flags+=shlex.split(subprocess.check_output(['pkg-config','--cflags','cairo','pangocairo'],text=True))
objects=[ui/f'{x}.o' for x in ['menu','control']]
objects += [cpu/f'{x}.o' for x in ['avatar','gltf','third_party','sound','remote_avatar']]
objects += [m/'build'/f'{x}.o' for x in ['VideoSignaling','Multiplayer','Protocol','UdpProtocol','UdpTransport','PlayerSync','MapSync','ContentHash','OnlineMenu','WindowSync','WindowMenu']]
for name in ['world','collision','walker','remote_windows']:
 obj=out/(name+'.o');cmd=['g++',*flags,'-c',str(f/'src'/(name+'.cpp')),'-o',str(obj)];print(shlex.join(cmd),flush=True);subprocess.run(cmd,check=True);objects.append(obj)
media_libs=[]
if options.webrtc:
 media_flags=['-I'+str(options.media_abi_dir.resolve())] if options.media_abi_dir else shlex.split(subprocess.check_output(['pkg-config','--cflags','gstreamer-app-1.0','gstreamer-sdp-1.0','gstreamer-webrtc-1.0'],text=True))
 media_libs=['-Wl,-l:'+x for x in ['libgstwebrtc-1.0.so.0','libgstsdp-1.0.so.0','libgstapp-1.0.so.0','libgstreamer-1.0.so.0','libgobject-2.0.so.0','libglib-2.0.so.0']]
 obj=out/'MediaSession.o';cmd=['g++',*flags,*media_flags,'-c',str(m/'media/gstreamer/MediaSession.cpp'),'-o',str(obj)]
 print(shlex.join(cmd),flush=True);subprocess.run(cmd,check=True);objects.append(obj)
cmd=['g++',*flags,str(out/'main_probe.cpp'),*map(str,objects),*media_libs,'-Wl,--gc-sections','-Wl,-l:libcairo.so.2','-Wl,-l:libpangocairo-1.0.so.0','-Wl,-l:libpango-1.0.so.0','-Wl,-l:libgobject-2.0.so.0','-o',str(out/'main_probe')]
print(shlex.join(cmd),flush=True);subprocess.run(cmd,check=True)
subprocess.run([str(out/'main_probe'),str(cpu/'fixtures')],check=True,timeout=90)
