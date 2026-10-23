#!/usr/bin/env python3
"""Exact native capture adapter with window/FBO/readback/session DOUBLES. Not Hyprland."""
from pathlib import Path
import argparse,re,subprocess
p=argparse.ArgumentParser();p.add_argument('fork',type=Path);p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
f=a.fork.resolve();m=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
s=(f/'src/renderer.cpp').read_text();start=s.index('    void CRenderer::captureVideoPanels(');end=s.index('\n    }',start)+6;method=s[start:end]
code=r'''
#include <hyprwalk/mp/VideoTypes.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <source_location>
#include <stdexcept>
using GLuint=unsigned;
void check(bool b,const std::source_location l=std::source_location::current()){if(!b)throw std::runtime_error("capture gate at "+std::to_string(l.line()));}
namespace hyprwalk {
struct Rule{bool denied=false;const auto& noScreenShare()const{return *this;}bool valueOrDefault()const{return denied;}};
struct Window{bool m_isMapped=true,hidden=false;std::unique_ptr<Rule> m_ruleApplicator=std::make_unique<Rule>();bool isHidden()const{return hidden;}};
constexpr int PANEL_WINDOW=1;
struct Panel{uintptr_t key=0;int kind=PANEL_WINDOW;std::weak_ptr<Window> window;std::vector<int> surfaces{1};float alpha=1;};
struct SVideoCaptureRequest{uintptr_t panel;mp::VideoTrackKey key;};
struct SessionDouble{
 std::vector<mp::VideoTrackKey> gates;std::vector<mp::WindowKey> revoked;std::vector<std::shared_ptr<const mp::VideoFrame>> sent;mp::VideoSettings limits;std::string error;
 auto captures()const{return gates;}const auto& settings()const{return limits;}
 void revoke(const mp::WindowKey& k){revoked.push_back(k);std::erase_if(gates,[&](const auto& g){return g.window==k;});}
 void submit(std::shared_ptr<const mp::VideoFrame> f){sent.push_back(std::move(f));}void fail(const std::string& e){error=e;}
};
struct ReadbackDouble{
 std::vector<mp::VideoTrackKey> kept;std::vector<std::shared_ptr<const mp::VideoFrame>> pending;struct {std::string error;} stats;
 unsigned issues=0,lastFbo=0,width=0,height=0;
 void clear(){kept.clear();pending.clear();}
 void retain(const std::vector<mp::VideoTrackKey>& keys){kept=keys;std::erase_if(pending,[&](const auto& f){return std::find(keys.begin(),keys.end(),f->key)==keys.end();});}
 auto take(){auto out=std::move(pending);pending.clear();return out;}
 void issue(const mp::VideoTrackKey&,unsigned f,unsigned w,unsigned h,const mp::VideoSettings&){++issues;lastFbo=f;width=w;height=h;}
 const auto& metrics()const{return stats;}
};
struct SFrameParams{SessionDouble* videoSession=nullptr;const std::vector<SVideoCaptureRequest>* videoCaptures=nullptr;const std::vector<Panel>* panels=nullptr;};
class CRenderer{public:
 struct Cached{unsigned lastUsed=0;struct{GLuint fbo=0;int w=1024,h=512;}target;float uvMax[2]{800.f/1024,400.f/512};};
 std::map<uintptr_t,Cached> m_panelGL;unsigned m_frame=9;ReadbackDouble m_videoReadback;std::string m_videoError;void captureVideoPanels(const SFrameParams&);
};
@METHOD@
}
using namespace hyprwalk;
int main(){try{
 CRenderer r;SessionDouble media;std::vector<Panel> panels(2);std::vector<SVideoCaptureRequest> requests;
 auto window=std::make_shared<Window>();panels[0].key=101;panels[0].window=window;panels[1].key=202;panels[1].window=std::make_shared<Window>();
 mp::VideoTrackKey key;key.window.session[0]=1;key.window.owner[0]=2;key.window.generation[0]=3;key.window.window=1;key.publication=key.stream=1;
 requests={{101,key}};media.gates={key};r.m_panelGL[101].lastUsed=9;r.m_panelGL[101].target.fbo=303;
 SFrameParams frame{&media,&requests,&panels};r.captureVideoPanels(frame);check(r.m_videoReadback.issues==1&&r.m_videoReadback.lastFbo==303&&r.m_videoReadback.width==800&&r.m_videoReadback.height==400);
 auto pending=std::make_shared<mp::VideoFrame>();pending->key=key;r.m_videoReadback.pending={pending};media.gates.clear();r.captureVideoPanels(frame);check(r.m_videoReadback.pending.empty()&&media.sent.empty()&&r.m_videoReadback.issues==1);
 for(int which=0;which<6;++which){
  window->m_isMapped=true;window->hidden=false;window->m_ruleApplicator=std::make_unique<Rule>();panels[0].surfaces={1};panels[0].alpha=1;
  if(which==0)window->m_isMapped=false;if(which==1)window->hidden=true;if(which==2)window->m_ruleApplicator->denied=true;
  if(which==3)window->m_ruleApplicator.reset();if(which==4)panels[0].surfaces.clear();if(which==5)panels[0].alpha=0;
  media.gates={key};r.m_videoReadback.pending={pending};r.captureVideoPanels(frame);check(media.gates.empty()&&media.sent.empty()&&r.m_videoReadback.issues==1&&r.m_videoReadback.pending.empty());
 }
 window->m_ruleApplicator=std::make_unique<Rule>();window->m_isMapped=true;window->hidden=false;panels[0].surfaces={1};panels[0].alpha=1;
 media.gates={key};panels[0].kind=2;r.captureVideoPanels(frame);check(media.gates.empty());panels[0].kind=PANEL_WINDOW;
 media.gates={key};r.m_panelGL[101].lastUsed=8;r.m_videoReadback.pending={pending};r.captureVideoPanels(frame);check(r.m_videoReadback.pending.empty()&&r.m_videoReadback.issues==1);r.m_panelGL[101].lastUsed=9;media.sent.clear();
 key.publication=2;key.stream=2;media.gates={key};requests[0].key=key;r.m_videoReadback.pending={pending};r.captureVideoPanels(frame);check(media.sent.empty()&&r.m_videoReadback.issues==2);
 pending=std::make_shared<mp::VideoFrame>();pending->key=key;r.m_videoReadback.pending={pending};r.captureVideoPanels(frame);check(media.sent.size()==1&&media.sent[0]->key==key);
 frame.videoSession=nullptr;r.m_videoReadback.pending={pending};r.captureVideoPanels(frame);check(r.m_videoReadback.pending.empty());frame.videoSession=&media;
 window.reset();media.gates={key};r.captureVideoPanels(frame);check(media.gates.empty());
 std::cout<<"PASS exact capture adapter: allowed native FBO only; populated UV rectangle; Off, unmap, hide, protection, missing rules, empty surfaces, transparent/layer rejection, stale FBO, new incarnation, pending-readback discard, destroyed weak window and disabled frame\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
'''.replace('@METHOD@',method)
(out/'capture_gate_test.cpp').write_text(code)
flags=['-std=c++20','-O0','-g1']
# VideoTypes.hpp is supplied by the selected profile override. Support both a
# checked-in plugin fork (profiles/<name>/plugin-overrides) and an assembled
# view (<tmp>/<name>/plugin) before falling back to common multiplayer headers.
for candidate in (f.parent.parent/'multiplayer-overrides/include', f.parent/'multiplayer/include'):
    if (candidate/'hyprwalk/mp/VideoTypes.hpp').is_file():
        flags.append('-I'+str(candidate))
flags.append('-I'+str(m/'include'))
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
cmd=['g++',*flags,str(out/'capture_gate_test.cpp'),'-o',str(out/'capture_gate_test')]
print(' '.join(cmd),flush=True);subprocess.run(cmd,check=True);subprocess.run([str(out/'capture_gate_test')],check=True)
