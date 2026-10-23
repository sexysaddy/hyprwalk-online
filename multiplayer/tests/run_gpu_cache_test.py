#!/usr/bin/env python3
"""Compile exact per-fork avatar resource and draw methods against an instrumented GL TEST DOUBLE.
No shader compilation, driver, compositor or real GPU is involved.
"""
from pathlib import Path
import re, subprocess, argparse, shlex
p=argparse.ArgumentParser();p.add_argument('fork',type=Path);p.add_argument('output',type=Path);p.add_argument('--compiler',default='g++');p.add_argument('--sanitize',choices=['address','thread']);a=p.parse_args()
f=a.fork.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
s=(f/'src/renderer.cpp').read_text();h=(f/'src/renderer.hpp').read_text()
def block(text,pattern):
 m=re.search(pattern,text,re.M);assert m,pattern
 i=text.index('{',m.start());depth=1;j=i+1
 while depth:
  if text[j]=='{':depth+=1
  elif text[j]=='}':depth-=1
  j+=1
 return text[m.start():j]
names=['uploadAvatar','destroyAvatar','updateAvatar','updateAvatarInstance','updateMorphs','avatarMaterials','drawAvatarDepth','drawAvatar','drawAvatarInstance','updateShadow','destroyRemotePlayer','drawRemotePlayer','clearRemoteAvatar','updateRemoteAvatar']
if 'CRenderer::prepareFrustumVisibility(' in s: names.append('prepareFrustumVisibility')
methods=[block(s,r'^    [^\n]* CRenderer::'+n+r'\(') for n in names]
gpu=block(h,r'        struct SAvatarGL')+';'
frame=block(h,r'    struct SAvatarFrame')+';'
code='\n'.join(methods)
constants=set(re.findall(r'\bGL_[A-Z0-9_]+\b',code));constants.update(['GL_NO_ERROR','GL_OUT_OF_MEMORY','GL_INVALID_OPERATION','GL_ARRAY_BUFFER','GL_ELEMENT_ARRAY_BUFFER','GL_DYNAMIC_DRAW','GL_TEXTURE_2D','GL_TEXTURE0','GL_TEXTURE5','GL_FLOAT','GL_RGBA','GL_TRUE','GL_FALSE'])
values={'GL_NO_ERROR':0,'GL_FALSE':0,'GL_TRUE':1,'GL_TEXTURE0':1000,'GL_TEXTURE5':1005}
constant_src='\n'.join(f'constexpr GLenum {name}={values.get(name,20+i)};' for i,name in enumerate(sorted(constants)))
# Functions that do not carry ownership or memory effects are explicit no-ops in this fixture.
tracked={'glGenVertexArrays','glDeleteVertexArrays','glGenBuffers','glDeleteBuffers','glGenTextures','glDeleteTextures','glBindVertexArray','glBindBuffer','glBindTexture','glActiveTexture','glBufferData','glBufferSubData','glTexStorage2D','glTexSubImage2D','glGetError','glDrawElements','glDrawArrays'}
allcalls=set(re.findall(r'\b(gl[A-Z]\w*)\(',code))
noops='\n'.join(f'template<class... T> void {name}(T...) {{}}' for name in sorted(allcalls-tracked))
# Render-local helpers (material/lighting shader uploads) are not what this resource test verifies.
helper=r'''
namespace hyprwalk {
const SMapLightSet::SVolume* chooseLightProbe(const SMapLightSet&,const V3&){return nullptr;}
struct CUnpackGuard {};
template<class...T> void logf(T&&...) {} void log(const char*) {}
void trimHeap() {} void staticBuffer(GLenum t,size_t n,const void* p){glBufferData(t,n,p,GL_DYNAMIC_DRAW);}
namespace gl { GLuint createTexture(GLenum t){GLuint id;glGenTextures(1,&id);glBindTexture(t,id);return id;} }
GLuint whiteTexture(){return gl::createTexture(GL_TEXTURE_2D);}
size_t uploadImages(const std::vector<SMapImage>& images,std::vector<GLuint>& textures){
 size_t bytes=0;for(const auto& i:images){textures.push_back(gl::createTexture(GL_TEXTURE_2D));bytes+=i.rgba.size();}return bytes;
}
GLint U(GLuint,const char*){return 0;}
void uMat(GLuint,const char*,const M4&){}
constexpr int TU_BASE=0,TU_SHADE=1,TU_NORMAL=2,TU_METAL=3,TU_EMISSIVE=4,TU_OCCLUSION=6,TU_MATCAP=7,TU_RIM=8,TU_UVMASK=9;
void setSamplerUnits(GLuint){} void setMaterial(GLuint,const SMapMaterial&,const std::vector<GLuint>&,const std::vector<SMapImage>&,GLuint){}
void uVec3(GLuint,const char*,const V3&){}
template<class T> GLenum stencilFunc(T){return 0;} template<class T> GLenum stencilOp(T){return 0;}
constexpr int UNIT_LAYER=10,UNIT_LAYER_MASK=11,UNIT_DETAIL_MASK=12,UNIT_OUTLINE_MASK=13,UNIT_DETAIL=14;
@FRAME@
struct SPanel {uintptr_t key=0;int layer=0;float alpha=1;struct Clip{double x=0,y=0,w=1,h=1;}clip,box;struct Pose{V3 origin,right,down,normal;float scale=1;}pose;};
std::vector<SPanel> emptyPanels;
struct SFrameParams{std::vector<SPanel>* panels=&emptyPanels;SAvatarFrame avatar,remoteAvatar;SRemotePlayerFrame remotePlayer;V3 eye;M4 view=M4::identity(),proj=M4::identity();int width=1280,height=720;float time=0;};
class CRenderer {public:
@GPU@
 SAvatarGL m_avatar,m_remoteAvatar;std::weak_ptr<SAvatarModel> m_avatarFailed,m_remoteAvatarFailed;
 GLuint m_remotePlayerVAO=0,m_remotePlayerVBO=0,m_progAvatar=1,m_progAvatarDepth=2,m_progWorld=3;
 GLuint m_shadowStaticFBO=1,m_shadowFBO=2,m_progPanelDepth=4,m_quadVAO=0;int m_shadowSize=128;
 std::vector<size_t> m_shadowPanelIndices;std::map<uintptr_t,int> m_panelGL;
 void setPanelUniforms(GLuint,const SPanel&,int){}
 SFrustum m_viewFrustum;std::vector<uint8_t> m_panelFrustumVisible;
 unsigned m_frustumPanelTests=0,m_frustumPanelsCulled=0,m_frustumAvatarTests=0,m_frustumAvatarsCulled=0;
 bool m_avatarInFrustum=true;uint64_t m_shadowCasterHash=1;M4 m_sunViewProj;
 struct Map {std::shared_ptr<SMapModel> model;struct LightSet{GLuint probes=0;};std::vector<LightSet> lightSets;} m_map;
 void setLighting(GLuint,const SFrameParams&,const M4&,int,const float*){}
 void setBakedLighting(GLuint,const SFrameParams&,int){}
 @DECLS@
};
@METHODS@
}
'''
# Generate method declarations from exact definitions.
decls=[]
for m in methods:
 sig=m[:m.index('{')].strip().replace('CRenderer::','')
 decls.append(sig+';')
helper=helper.replace('@FRAME@',frame).replace('@GPU@',gpu).replace('@DECLS@','\n'.join(decls)).replace('@METHODS@',code)
template=(Path(__file__).parent/'gpu_cache_fixture.cpp.in').read_text()
culling='#include "avatar_culling.hpp"\n#include "frustum.hpp"' if 'avatar_culling::' in code else ''
if 'CRenderer::prepareFrustumVisibility(' not in s: helper=helper.replace('SFrustum m_viewFrustum;', '')
text=template.replace('@CONSTANTS@',constant_src).replace('@NOOPS@',noops).replace('@CULLING@',culling).replace('@IMPLEMENTATION@',helper)
(out/'gpu_cache_test.cpp').write_text(text)
flags=['-std=c++23','-O0','-g1','-DHYPRWALK_MULTIPLAYER','-ffunction-sections','-fdata-sections','-I'+str(f/'src'),'-I'+str(Path(__file__).resolve().parents[1]/'include')]
if a.sanitize:flags+=['-fsanitize='+('address,undefined' if a.sanitize=='address' else 'thread'),'-fno-omit-frame-pointer']
cmd=[a.compiler,*flags,str(out/'gpu_cache_test.cpp'),'-Wl,--gc-sections','-o',str(out/'gpu_cache_test')]
print(shlex.join(cmd),flush=True);subprocess.run(cmd,check=True);subprocess.run([str(out/'gpu_cache_test')],check=True)
