// Headless REAL GLES3/Mesa test of the production Readback, not Hyprland capture.
#include "../media/gl/Readback.hpp"
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <stdexcept>
using namespace hyprwalk::mp;using namespace std::chrono_literals;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #x);}while(0)
int main(){
 EGLDisplay display=eglGetPlatformDisplay(0x31DD,nullptr,nullptr);EGLContext ctx=EGL_NO_CONTEXT;EGLSurface surface=EGL_NO_SURFACE;
 try{
  EGLint major=0,minor=0;if(!eglInitialize(display,&major,&minor)){std::cout<<"SKIP no headless EGL display\n";return 77;}
  CHECK(eglBindAPI(EGL_OPENGL_ES_API));EGLConfig config=nullptr;EGLint count=0;
  const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,0x40,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
  CHECK(eglChooseConfig(display,attrs,&config,1,&count)&&count);
  const EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE},sa[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
  ctx=eglCreateContext(display,config,EGL_NO_CONTEXT,ca);surface=eglCreatePbufferSurface(display,config,sa);CHECK(ctx&&surface&&eglMakeCurrent(display,surface,surface,ctx));
  std::cout<<"REAL "<<glGetString(GL_VERSION)<<" / "<<glGetString(GL_RENDERER)<<'\n';
  GLuint tex=0,fbo=0,sentinel=0;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  std::vector<unsigned char> padding(256*128*4,231);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,256,128,0,GL_RGBA,GL_UNSIGNED_BYTE,padding.data());
  glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
  std::vector<unsigned char> source(128*64*4,255);
  for(unsigned y=0;y<64;++y)for(unsigned x=0;x<128;++x){auto at=(y*128+x)*4;source[at]=x;source[at+1]=y;source[at+2]=7;}
  glTexSubImage2D(GL_TEXTURE_2D,0,0,0,128,64,GL_RGBA,GL_UNSIGNED_BYTE,source.data());
  glGenBuffers(1,&sentinel);glBindBuffer(GL_PIXEL_PACK_BUFFER,sentinel);glBufferData(GL_PIXEL_PACK_BUFFER,65536,nullptr,GL_STREAM_READ);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,sentinel);
  glPixelStorei(GL_PACK_ROW_LENGTH,17);glPixelStorei(GL_PACK_SKIP_ROWS,3);glPixelStorei(GL_PACK_SKIP_PIXELS,2);glPixelStorei(GL_PACK_ALIGNMENT,8);glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);
  media::Readback r;VideoTrackKey key;key.window.session[0]=1;key.window.owner[0]=2;key.window.generation[0]=3;key.window.window=1;key.publication=1;key.stream=1;VideoSettings settings;
  r.issue(key,fbo,128,64,settings);CHECK(r.metrics().error.empty());
  // A retained publication must survive between source refresh ticks while its
  // GPU fence is pending; capture cadence is independent from render cadence.
  r.retain({key});
  GLint value=0;glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&value);CHECK(value==static_cast<int>(sentinel));glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&value);CHECK(value==static_cast<int>(sentinel));glGetIntegerv(GL_PACK_ROW_LENGTH,&value);CHECK(value==17);CHECK(glIsEnabled(GL_SCISSOR_TEST));
  std::vector<std::shared_ptr<const VideoFrame>> frames;const auto end=std::chrono::steady_clock::now()+2s;while(frames.empty()&&std::chrono::steady_clock::now()<end){frames=r.take();std::this_thread::sleep_for(1ms);}
  CHECK(frames.size()==1&&frames[0]->width==128&&frames[0]->height==64&&frames[0]->rgba==source);
  std::cout<<"PASS exact populated rectangle; no padding leak; row 0 orientation and RGBA; foreign PBO/pack/scissor restored\n";
  r.clear();glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glDisable(GL_SCISSOR_TEST);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);glPixelStorei(GL_PACK_ALIGNMENT,1);
  for(unsigned cycle=0;cycle<100;++cycle){
   ++key.publication;const auto issuedBefore=r.metrics().issued;const auto droppedBefore=r.metrics().dropped;
   r.issue(key,fbo,128,64,settings);CHECK(r.metrics().issued==issuedBefore+1);
   r.retain({});CHECK(r.metrics().dropped==droppedBefore+1);
   CHECK(r.take().empty());CHECK(glGetError()==GL_NO_ERROR);
  }
  ++key.publication;settings.width=64;settings.height=64;r.issue(key,fbo,128,64,settings);frames.clear();const auto limit=std::chrono::steady_clock::now()+2s;while(frames.empty()&&std::chrono::steady_clock::now()<limit){frames=r.take();std::this_thread::sleep_for(1ms);}CHECK(frames.size()==1&&frames[0]->width==64&&frames[0]->height==32);r.clear();CHECK(glIsBuffer(sentinel)&&glIsTexture(tex));
  std::cout<<"PASS 100 revoke/regrant cleanup cycles, scaling 128x64 -> 64x32, independent local resources\n";
  std::cout<<"Readback measured here ONLY: issued="<<r.metrics().issued<<" completed="<<r.metrics().completed<<" max_issue_us="<<r.metrics().maxIssueUs<<" max_copy_us="<<r.metrics().maxCopyUs<<'\n';
  // Representative warm software-GL readback measurement, NOT a compositor/GPU FPS claim.
  glBindTexture(GL_TEXTURE_2D,tex);std::vector<unsigned char> large(1024*576*4,89);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1024,576,0,GL_RGBA,GL_UNSIGNED_BYTE,large.data());
  settings.width=960;settings.height=540;settings.fps=15;media::Readback warm;std::vector<double> calls;
  const auto benchStart=std::chrono::steady_clock::now();
  for(unsigned i=0;i<60;++i){
   const auto before=std::chrono::steady_clock::now();warm.issue(key,fbo,960,540,settings);
   calls.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-before).count());
   frames.clear();const auto stop=std::chrono::steady_clock::now()+2s;
   while(frames.empty()&&std::chrono::steady_clock::now()<stop){frames=warm.take();std::this_thread::sleep_for(1ms);}
   CHECK(frames.size()==1&&frames[0]->width==960&&frames[0]->height==540&&warm.metrics().error.empty());
   std::this_thread::sleep_until(benchStart+std::chrono::milliseconds((i+1)*70));
  }
  std::sort(calls.begin(),calls.end());
  std::cout<<"WARM software GL only 960x540, 60 samples: issue_median_us="<<calls[30]<<" issue_p95_us="<<calls[57]<<" issue_max_us="<<calls.back()<<" internal_copy_max_us="<<warm.metrics().maxCopyUs<<" completed="<<warm.metrics().completed<<'\n';
  warm.clear();
  glDeleteBuffers(1,&sentinel);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&tex);CHECK(glGetError()==GL_NO_ERROR);
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,ctx);eglTerminate(display);return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<" EGL="<<std::hex<<eglGetError()<<'\n';if(surface)eglDestroySurface(display,surface);if(ctx)eglDestroyContext(display,ctx);if(display)eglTerminate(display);return 1;}
}
