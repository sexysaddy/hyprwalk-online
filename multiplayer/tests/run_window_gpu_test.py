#!/usr/bin/env python3
"""Exact fork remote-window texture methods against instrumented GL, NOT a driver test."""
from pathlib import Path
import argparse,re,subprocess,shlex
p=argparse.ArgumentParser();p.add_argument('fork',type=Path);p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true');p.add_argument('--webrtc',action='store_true');a=p.parse_args()
f=a.fork.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
s=(f/'src/renderer.cpp').read_text();h=(f/'src/renderer.hpp').read_text()
def block(t,pat):
 m=re.search(pat,t,re.M);assert m,pat;i=t.index('{',m.start());j=i+1;n=1
 while n:
  if t[j]=='{':n+=1
  elif t[j]=='}':n-=1
  j+=1
 return t[m.start():j]
methods=[block(s,r'^    void CRenderer::'+n+r'\(') for n in ['clearRemoteWindows','pruneRemoteWindows','drawRemoteWindows']]
if a.webrtc:methods+=[block(s,r'^    void CRenderer::'+n+r'\(') for n in ['clearVideoResources','pruneRemoteVideos']]
guard=block(s,r'        class CUnpackGuard')+';';gpu=block(h,r'        struct SRemoteWindowGL')+';'
code='\n'.join(methods);constants=set(re.findall(r'\bGL_[A-Z0-9_]+\b',code+guard))
const='\n'.join(f'constexpr GLenum {v}={i+1};' for i,v in enumerate(sorted(constants)))
tracked={'glGenTextures','glDeleteTextures','glBindTexture','glTexImage2D','glDrawArrays','glGetIntegerv','glBindBuffer','glPixelStorei','glTexSubImage2D','glGetError'}
noops='\n'.join('template<class... T>void '+n+'(T...){}' for n in sorted(set(re.findall(r'\b(gl[A-Z]\w*)\(',code+guard))-tracked))
impl='namespace hyprwalk {\n'+guard+'\n'+r'''
void log(const char*){} GLint U(GLuint,const char*){return 0;}
void uMat(GLuint,const char*,const M4&){}
void uVec3(GLuint,const char* key,const V3& v){fixture::vectors[key]=v;}
struct MediaDouble{std::string error;void fail(const std::string& e){error=e;}};
struct SFrameParams {MediaDouble* videoSession=nullptr;const std::vector<SRemoteWindowFrame>* remoteWindows=nullptr;V3 eye{0,1,3};};
class CRenderer {public:
@GPU@
 struct ReadbackDouble{void clear(){}} m_videoReadback;
 std::string m_videoError;
 GLuint m_progPanel=1,m_quadVAO=2;
 std::map<mp::WindowKey,SRemoteWindowGL> m_remoteWindows;
@DECLS@
};
@METHODS@
}
'''
impl=impl.replace('@GPU@',gpu).replace('@DECLS@','\n'.join(m[:m.index('{')].replace('CRenderer::','')+';' for m in methods)).replace('@METHODS@',code)
text=(Path(__file__).with_name('window_gpu_fixture.cpp.in')).read_text().replace('@CONSTANTS@',const).replace('@NOOPS@',noops).replace('@IMPL@',impl)
(out/'window_gpu_test.cpp').write_text(text)
flags=['-std=c++23','-O0','-g1','-Wall','-Wextra','-Werror','-DHYPRWALK_MULTIPLAYER','-I'+str(f/'src'),'-I'+str(Path(__file__).resolve().parents[1]/'include')]
if a.webrtc:flags+=['-DHYPRWALK_WEBRTC']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
cmd=['g++',*flags,str(out/'window_gpu_test.cpp'),*[str(Path(__file__).resolve().parents[1]/'build'/(n+'.o')) for n in ['VideoSignaling','Multiplayer','Protocol','UdpProtocol','UdpTransport','PlayerSync','MapSync','ContentHash','OnlineMenu','WindowSync','WindowMenu']],'-Wl,--gc-sections','-o',str(out/'window_gpu_test')];print(shlex.join(cmd),flush=True);subprocess.run(cmd,check=True);subprocess.run([str(out/'window_gpu_test')],check=True)
