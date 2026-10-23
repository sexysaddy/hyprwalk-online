// Test fixture only; run.py inserts current, unmodified production method bodies.
#include "gles.hpp"
#include "math3d.hpp"
#include "viewport.hpp"
#include "shaders.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <numbers>
#include <set>
#include <fstream>
#include <unistd.h>
// Track GL object ownership in the real driver, including rejected replacement candidates.
static std::set<GLuint> liveTextures, liveFbos, liveRbs;
static size_t generated = 0;
static int failChecks = 0;
static bool failSkyUpload=false;
static GLenum syntheticError=GL_NO_ERROR;
static decltype(glTexImage2D) realTexImage;
static decltype(glGetError) realGetError;
void rss(const char* phase) {
    unsigned long pages=0,resident=0;std::ifstream in("/proc/self/statm");in>>pages>>resident;
    std::printf("TEST_PROCESS_RSS,%s,%.3f MiB (Mesa/fixture, not Hyprland)\n",phase,resident*(double)sysconf(_SC_PAGESIZE)/(1024*1024));
}

static decltype(glGenTextures) realGenTextures;
static decltype(glDeleteTextures) realDeleteTextures;
static decltype(glGenFramebuffers) realGenFbos;
static decltype(glDeleteFramebuffers) realDeleteFbos;
static decltype(glGenRenderbuffers) realGenRbs;
static decltype(glDeleteRenderbuffers) realDeleteRbs;
static decltype(glCheckFramebufferStatus) realCheck;
void trackObjects() {
    realGenTextures=glGenTextures; realDeleteTextures=glDeleteTextures;
    realGenFbos=glGenFramebuffers; realDeleteFbos=glDeleteFramebuffers;
    realGenRbs=glGenRenderbuffers; realDeleteRbs=glDeleteRenderbuffers;
    realCheck=glCheckFramebufferStatus;
    realTexImage=glTexImage2D;realGetError=glGetError;
    glTexImage2D=[](GLenum t,GLint level,GLint internal,GLsizei w,GLsizei h,GLint border,GLenum format,GLenum type,const void* pixels) {
        realTexImage(t,level,internal,w,h,border,format,type,pixels);
        if(internal==GL_SRGB8_ALPHA8 && failSkyUpload) {failSkyUpload=false;syntheticError=0x0505;}
    };
    glGetError=[]()->GLenum {if(syntheticError!=GL_NO_ERROR){const auto e=syntheticError;syntheticError=GL_NO_ERROR;return e;}return realGetError();};
    glGenTextures=[](GLsizei n,GLuint* ids){realGenTextures(n,ids); for(int i=0;i<n;++i) if(ids[i]) {assert(liveTextures.insert(ids[i]).second); ++generated;}};
    glDeleteTextures=[](GLsizei n,const GLuint* ids){for(int i=0;i<n;++i) if(ids[i]) assert(liveTextures.erase(ids[i])==1); realDeleteTextures(n,ids);};
    glGenFramebuffers=[](GLsizei n,GLuint* ids){realGenFbos(n,ids); for(int i=0;i<n;++i) if(ids[i]) {assert(liveFbos.insert(ids[i]).second); ++generated;}};
    glDeleteFramebuffers=[](GLsizei n,const GLuint* ids){for(int i=0;i<n;++i) if(ids[i]) assert(liveFbos.erase(ids[i])==1); realDeleteFbos(n,ids);};
    glGenRenderbuffers=[](GLsizei n,GLuint* ids){realGenRbs(n,ids); for(int i=0;i<n;++i) if(ids[i]) {assert(liveRbs.insert(ids[i]).second); ++generated;}};
    glDeleteRenderbuffers=[](GLsizei n,const GLuint* ids){for(int i=0;i<n;++i) if(ids[i]) assert(liveRbs.erase(ids[i])==1); realDeleteRbs(n,ids);};
    glCheckFramebufferStatus=[](GLenum target)->GLenum {if(failChecks>0) {--failChecks; return 0x8CD6;} return realCheck(target);};
}
namespace hyprwalk {
template<class... Args> void logf(const char* message, Args&&...) { std::fprintf(stderr, "%s\n", message); }
struct SPanel {};
}
// INSERT PRODUCTION TYPES
namespace hyprwalk {
constexpr int UNIT_SKY=16;
void uMat(GLuint,const char*,const M4&);
void uVec3(GLuint,const char*,const V3&);
std::string withCommon(const char*,bool,bool,bool);
GLint U(GLuint program, const char* name) { return glGetUniformLocation(program, name); }
M4 flipY() { M4 m=M4::identity(); m.m[5]=-1; return m; }
struct Bounds { V3 min, max; };
enum { MAP_PASS_SKY, MAP_PASS_OPAQUE, MAP_PASS_BLEND };
class CRenderer {
public:
    bool m_ready=true;
    uint64_t m_frame=0;
    int m_samples=4;
    GLuint m_msaaFBO=0, m_msaaColor=0, m_msaaDepth=0;
    int m_msaaW=0, m_msaaH=0, m_msaaSamples=0;
    GLuint m_scaleFBO=0, m_scaleTex=0;
    int m_scaleW=0, m_scaleH=0;
    GLuint m_resolveFBO=0, m_quadVAO=0, m_quadVBO=0;
    GLuint m_progScale=0, m_progCross=0, m_progHud=0, m_progVideo=0;
    GLuint m_progGradient=0, m_progSquare=0;
    GLuint m_videoTex=0;
    int m_videoW=0, m_videoH=0;
    uint64_t m_videoSerial=0;
    struct SHudGL { GLuint tex=0; int w=0,h=0; uint64_t serial=0; } m_hudGL[2];
    bool m_sunFollow=false;
    GLuint m_shadowStaticFBO=0, m_customSkyTex=0;
    uint64_t m_customSkySerial=0,m_customSkyFailedSerial=0;
    V3 m_sunFocus;
    Bounds m_sunCasters;
    bool gradient=true, square=true, actualSky=false;
    int m_customSkyW=0,m_customSkyH=0;
    V3 m_sunDir{0,1,0};
    GLuint m_progSky=0;
    void drawProductionSky(const SFrameParams&,const M4&);
    void drawLegacySky(const SFrameParams&,const M4&);
    void releaseScaleTarget();
    void releaseSceneTargets();
    bool ensureScaleTarget(int,int);
    bool ensureMSAA(int,int,int);
    void drawVideo(const SFrameParams&);
    void drawCrosshair(const SFrameParams&);
    void drawHud(const SFrameParams&);
    void drawHudImage(const SFrameParams&, const SHudImage&, SHudGL&);
    bool render(const SFrameParams&, GLuint);
    bool renderLetterboxReference(const SFrameParams&, GLuint);
    void capturePanels(const SFrameParams&) { glActiveTexture(GL_TEXTURE0); }
    void updateAvatar(const SFrameParams&) {}
    void setupSun(const Bounds&, const Bounds&) {}
    void bakeShadow() {}
    void updateShadow(const SFrameParams&) {}
    int updateLights(const SFrameParams&, float*) { return 0; }
    void drawMap(const SFrameParams&, const M4&, int, const float*, int) {}
    void drawBackdrop(const SFrameParams&, int, const float*) {}
    void drawAvatar(const SFrameParams&, const M4&, int, const float*, bool) {}
    void drawPanels(const SFrameParams&, const M4&, bool) {}
    void drawSky(const SFrameParams& f, const M4& vp) {
        if(actualSky) {drawProductionSky(f,vp);return;}
        if (!gradient) return;
        glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
        glUseProgram(m_progGradient); glBindVertexArray(m_quadVAO);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    }
    void drawWorld(const SFrameParams&, const M4& vp, int, const float*) {
        if (!square) return;
        glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
        glUseProgram(m_progSquare); glBindVertexArray(m_quadVAO);
        glUniformMatrix4fv(U(m_progSquare,"uMatrix"),1,GL_FALSE,vp.m);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    }
};
// INSERT PRODUCTION METHODS
}
using namespace hyprwalk;
void noError(const char* where) {
    auto e=glGetError();
    if (e) { std::fprintf(stderr,"GL error 0x%x at %s\n",e,where); std::exit(1); }
}
GLuint texture(int w, int h) {
    GLuint t=0; glGenTextures(1,&t); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr); return t;
}
std::vector<unsigned char> readImage(GLuint fbo,int w,int h) {
    glBindFramebuffer(GL_FRAMEBUFFER,fbo); glPixelStorei(GL_PACK_ALIGNMENT,1);
    std::vector<unsigned char> p(w*h*4); glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,p.data());
    noError("readback"); return p;
}
void dumpImage(const std::string& name, int w, int h, const std::vector<uint8_t>& p) {
    const char* directory=std::getenv("HW_STRETCH_DUMPS");
    if(!directory) return;
    std::ofstream out(std::string(directory)+"/"+name+".ppm",std::ios::binary);
    assert(out); out<<"P6\n"<<w<<" "<<h<<"\n255\n";
    for(size_t i=0;i<p.size();i+=4) out.write(reinterpret_cast<const char*>(p.data()+i),3);
}
void checkState() {
    GLint v[4]; glGetIntegerv(GL_VIEWPORT,v); assert(v[0]==3 && v[1]==5 && v[2]==17 && v[3]==19);
    glGetIntegerv(GL_SCISSOR_BOX,v); assert(v[0]==7 && v[1]==9 && v[2]==11 && v[3]==13);
    assert(glIsEnabled(GL_SCISSOR_TEST)); assert(glIsEnabled(GL_STENCIL_TEST));
    GLboolean mask[4]; glGetBooleanv(GL_COLOR_WRITEMASK,mask);
    assert(!mask[0] && mask[1] && !mask[2] && mask[3]);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH,v); assert(v[0]==37);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS,v); assert(v[0]==2);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS,v); assert(v[0]==3);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,v); assert(v[0]==0);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,v); assert(v[0]==0);
    noError("restored state");
}
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    int major=0,minor=0; void* display=eglGetDisplay(nullptr);
    if (!eglInitialize(display,&major,&minor) || !eglBindAPI(0x30A0)) {
        std::fprintf(stderr,"FAIL: surfaceless EGL/GLES unavailable\n"); return 1;
    }
    const int attribs[]={0x3098,3,0x3038};
    void* context=eglCreateContext(display,nullptr,nullptr,attribs);
    if (!context || !eglMakeCurrent(display,nullptr,nullptr,context)) return 1;
    loadGL();
    trackObjects();
    std::printf("EGL %d.%d | %s | %s\n",major,minor,glGetString(GL_VERSION),glGetString(GL_RENDERER));
    CRenderer r;
    GLint samples=0; glGetIntegerv(GL_MAX_SAMPLES,&samples); assert(samples>=2); r.m_samples=std::min(4,samples);
    r.m_progScale=gl::makeProgram("scale",shaders::SCALE_VS,shaders::SCALE_FS);
    r.m_progVideo=gl::makeProgram("video",shaders::HUD_VS,shaders::VIDEO_FS);
    r.m_progHud=gl::makeProgram("hud",shaders::HUD_VS,shaders::HUD_FS);
    r.m_progCross=gl::makeProgram("crosshair",shaders::CROSS_VS,shaders::CROSS_FS);
    r.m_progSky=gl::makeProgram("sky",shaders::SKY_VS,withCommon(shaders::SKY_FS_BODY,true,false,false));
    assert(r.m_progSky);
    r.m_progGradient=gl::makeProgram("gradient",shaders::SCALE_VS,R"(#version 300 es
precision highp float; in vec2 vUV; out vec4 color;
void main(){color=vec4(vUV,0.25,1.0);})");
    r.m_progSquare=gl::makeProgram("square",R"(#version 300 es
layout(location=0) in vec2 aPos; uniform mat4 uMatrix;
void main(){gl_Position=uMatrix*vec4((aPos-.5)*.7,-2.,1.);})",R"(#version 300 es
precision highp float; out vec4 color; void main(){color=vec4(0.,0.,1.,1.);})");
    assert(r.m_progScale && r.m_progVideo && r.m_progHud && r.m_progCross && r.m_progGradient && r.m_progSquare);
    glGenFramebuffers(1,&r.m_resolveFBO);
    glGenVertexArrays(1,&r.m_quadVAO); glBindVertexArray(r.m_quadVAO);
    glGenBuffers(1,&r.m_quadVBO); glBindBuffer(GL_ARRAY_BUFFER,r.m_quadVBO);
    const float quad[]={0,0,1,0,0,1,1,1}; glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,nullptr); glEnableVertexAttribArray(0);
    std::vector<SPanel> panels;
    std::vector<uint32_t> hud(16,0xFFFF00FF); // 4x4 magenta, top row first
    std::vector<uint8_t> video(8*8*4);
    for (int y=0;y<8;++y) for(int x=0;x<8;++x) {
        const int i=(y*8+x)*4; video[i]=255; video[i+1]=y<4 ? 0:255; video[i+2]=0; video[i+3]=255;
    }

    const std::array<std::pair<int,int>,5> sizes={{{320,180},{320,200},{420,180},{180,320},{317,197}}};
    const std::array<float,5> aspects={0.f,16.f/9.f,16.f/10.f,21.f/9.f,9.f/16.f};
    const std::array<float,6> scales={1.f,.9f,.8f,.75f,.67f,.5f};
    int cases=0, restores=0, nativeMatches=0, oldBars=0;
    std::array<std::vector<uint8_t>,150> nativeImages;
    int configuration=0;
    for (float fov:{60.f,90.f,120.f}) for (const auto& [w,h]:sizes) for (float aspect:aspects) for (float scale:scales) for (bool aa:{false,true}) {
        if (std::getenv("HW_NATIVE_ONLY") && scale!=1.f) continue;
        SFrameParams f; f.width=w; f.height=h; f.renderScale=scale;
        auto content=fitContent(w,h,aspect);
        f.aspectRatio=(float)content.width/content.height;
        f.view=M4::identity(); f.proj=M4::perspective(fov*std::numbers::pi_v<float>/180.f,f.aspectRatio,.1,100);
        f.panels=&panels; f.shadowsEnabled=false; f.dynamicLightingEnabled=false; f.antialiasingEnabled=aa;
        f.crosshair=false;
        const int hudX=content.x+std::min(24,content.width/4), hudY=content.y+std::min(16,content.height/4);
        f.menu={.pixels=&hud,.w=4,.h=4,.serial=1,.x=(float)hudX,.y=(float)hudY};
        f.video={.rgba=&video,.width=8,.height=8,.serial=1,.xPercent=80,.yPercent=80,
                 .widthPercent=10,.heightPercent=10,.keepAspect=false};
        const SFrameParams original=f;
        const GLuint out=texture(w,h);
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glViewport(3,5,17,19); glScissor(7,9,11,13); glEnable(GL_SCISSOR_TEST); glEnable(GL_STENCIL_TEST);
        glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_TRUE);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,37); glPixelStorei(GL_UNPACK_SKIP_PIXELS,2); glPixelStorei(GL_UNPACK_SKIP_ROWS,3);
        assert(r.render(f,out)); checkState();
        const auto [rw,rh]=renderSize(content,scale);
        if(rw==w && rh==h) assert(!r.m_scaleTex && !r.m_scaleFBO);
        assert(r.m_msaaW==rw && r.m_msaaH==rh && r.m_msaaSamples==(aa?r.m_samples:1));
        GLint actual=0;
        glBindRenderbuffer(GL_RENDERBUFFER,r.m_msaaColor); glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_WIDTH,&actual); assert(actual==rw);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_HEIGHT,&actual); assert(actual==rh);
        glBindRenderbuffer(GL_RENDERBUFFER,r.m_msaaDepth); glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_WIDTH,&actual); assert(actual==rw);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_HEIGHT,&actual); assert(actual==rh);
        glBindRenderbuffer(GL_RENDERBUFFER,0);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,0); glPixelStorei(GL_UNPACK_SKIP_PIXELS,0); glPixelStorei(GL_UNPACK_SKIP_ROWS,0);
        auto p=readImage(r.m_resolveFBO,w,h);
        const int nativeIndex=(configuration/12)*2+(aa?1:0);
        if(scale==1.f) nativeImages[nativeIndex]=p;
        const auto pixel=[&](int x,int y,int c){return p[(y*w+x)*4+c];};
        int minx=w,miny=h,maxx=-1,maxy=-1,magenta=0;
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            if(pixel(x,y,2)>240 && pixel(x,y,0)<10 && pixel(x,y,1)<10) {
                minx=std::min(minx,x); maxx=std::max(maxx,x); miny=std::min(miny,y); maxy=std::max(maxy,y);
            }
            if(pixel(x,y,0)==255 && pixel(x,y,1)==0 && pixel(x,y,2)==255) ++magenta;
            assert(pixel(x,y,3)==255);
            // Gradient/video/geometry are all non-black: every output pixel must
            // belong to the image, including the OLD letterbox/pillarbox regions.
            assert(pixel(x,y,0)!=0 || pixel(x,y,1)!=0 || pixel(x,y,2)!=0);
        }
        // A square is square in SOURCE pixels, deliberately non-square on the output.
        const double sourceWidth=(maxx-minx+1)*(double)content.width/w;
        const double sourceHeight=(maxy-miny+1)*(double)content.height/h;
        const double tolerance=std::ceil(2/scale)+1;
        if (!(maxx>=minx && maxy>=miny && std::abs(sourceWidth-sourceHeight)<=tolerance))
            std::fprintf(stderr,"stretch failure: output=%dx%d aspect=%.6f fov=%.1f scale=%.2f aa=%d source=%dx%d box=%d,%d..%d,%d\n",w,h,aspect,fov,scale,aa,content.width,content.height,minx,miny,maxx,maxy);
        assert(maxx>=minx && maxy>=miny && std::abs(sourceWidth-sourceHeight)<=tolerance);
        const float expectedHeight=h*.175f/std::tan(fov*std::numbers::pi_v<float>/360.f);
        const float expectedWidth=w*.175f/(std::tan(fov*std::numbers::pi_v<float>/360.f)*f.aspectRatio);
        assert(std::abs((maxx-minx+1)-expectedWidth)<=tolerance*w/content.width);
        assert(std::abs((maxy-miny+1)-expectedHeight)<=tolerance*h/content.height);
        // One asymmetric source rasterisation pixel may span multiple output pixels.
        const double centreToleranceX=2.0*w/rw, centreToleranceY=2.0*h/rh;
        if(std::abs((minx+maxx)-(w-1))>centreToleranceX || std::abs((miny+maxy)-(h-1))>centreToleranceY)
            std::fprintf(stderr,"centre failure %dx%d aspect=%f fov=%f scale=%f aa=%d source=%dx%d internal=%dx%d bounds=%d,%d..%d,%d\n",w,h,aspect,fov,scale,aa,content.width,content.height,rw,rh,minx,miny,maxx,maxy);
        assert(std::abs((minx+maxx)-(w-1))<=centreToleranceX);
        assert(std::abs((miny+maxy)-(h-1))<=centreToleranceY);
        // Menu layout is intentionally unchanged (native output coordinates).
        assert(magenta==16 && pixel(hudX,hudY,0)==255 && pixel(hudX,hudY,2)==255);
        // Video is once-only and upright, stretched with the source rather than letterboxed.
        assert(pixel(int(w*.8),int(h*.77),0)>250);
        assert(pixel(int(w*.8),int(h*.77),1)<30);
        assert(pixel(int(w*.8),int(h*.83),1)>220);
        for(int y:{0,h-1}) {
            assert(pixel(0,y,0)<6 && pixel(w-1,y,0)>249);
            for(int x=1;x<w;++x) assert(pixel(x,y,0)>=pixel(x-1,y,0));
        }
        for(int x:{0,w-1}) {
            assert(pixel(x,0,1)<6 && pixel(x,h-1,1)>249);
            for(int y=1;y<h;++y) assert(pixel(x,y,1)>=pixel(x,y-1,1));
        }
        // No renderer-side camera/FOV adjustment; the caller's projection stays intact.
        for(int i=0;i<16;++i) assert(f.proj.m[i]==original.proj.m[i]);
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,37); glPixelStorei(GL_UNPACK_SKIP_PIXELS,2); glPixelStorei(GL_UNPACK_SKIP_ROWS,3);
        assert(r.renderLetterboxReference(f,out)); checkState();
        const auto reference=readImage(r.m_resolveFBO,w,h);
        if(w==320 && h==180 && fov==90.f && scale==1.f && aa) {
            const auto index=std::distance(aspects.begin(),std::find(aspects.begin(),aspects.end(),aspect));
            dumpImage("aspect-"+std::to_string(index)+"-before",w,h,reference);
            dumpImage("aspect-"+std::to_string(index)+"-after",w,h,p);
        }
        if(content.width==w && content.height==h) {
            assert(p==reference); ++nativeMatches;
        } else {
            assert(reference[0]==0 && reference[1]==0 && reference[2]==0);
            assert(p[2]>0); ++oldBars;
        }
        r.gradient=false; r.square=false; f.video={}; f.menu={};
        glBindFramebuffer(GL_FRAMEBUFFER,0); glViewport(3,5,17,19);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,37); glPixelStorei(GL_UNPACK_SKIP_PIXELS,2); glPixelStorei(GL_UNPACK_SKIP_ROWS,3);
        assert(r.render(f,out)); checkState();
        p=readImage(r.m_resolveFBO,w,h);
        for(size_t i=0;i<p.size();i+=4) assert(p[i]==0 && p[i+1]==0 && p[i+2]==0 && p[i+3]==255);
        f.crosshair=true; f.crosshairDot=true;
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        assert(r.render(f,out)); checkState();
        p=readImage(r.m_resolveFBO,w,h);
        assert(pixel(w/2,h/2,1)>200);
        assert(pixel(std::min(w-1,w/2+(int)std::ceil(10.0*w/content.width)),h/2,1)==0);
        r.gradient=true; r.square=true;
        if(scale!=1.f) {
            auto restored=original; restored.renderScale=1.f;
            glBindFramebuffer(GL_FRAMEBUFFER,0);
            assert(r.render(restored,out)); checkState();
            const auto native=readImage(r.m_resolveFBO,w,h);
            assert(!nativeImages[nativeIndex].empty() && native==nativeImages[nativeIndex]);
            ++restores;
        }
        glBindFramebuffer(GL_FRAMEBUFFER,0); glDeleteTextures(1,&out); ++cases; ++configuration;
    }
    std::printf("PASS: %d target configurations / %d GLES draws including reference; 3 FOV values, 5 outputs, 5 aspects, 6 scales, MSAA on/off; full-output stretch/no bars, native HUD, stretched video/crosshair, color/depth dimensions, GL state\n",cases,cases*4);
    std::printf("PASS: %d native-aspect frames byte-identical to supplied renderer; %d negative controls demonstrate old bars are gone\n",nativeMatches,oldBars);
    std::printf("PASS: %d returns from reduced scale to 100%% are byte-identical to their native reference frames\n",restores);

    // Video Keep Aspect is a SOURCE-layout option, not permission to cancel the final stretch.
    // Its final pixels and the reticle are independent of scene Render Scale.
    r.gradient=false; r.square=false;
    int overlayFrames=0;
    for(auto [w,h]:sizes) for(float aspect:aspects) for(bool keep:{false,true}) {
        auto c=fitContent(w,h,aspect);
        SFrameParams f;f.width=w;f.height=h;f.aspectRatio=(float)c.width/c.height;f.panels=&panels;
        f.shadowsEnabled=false;f.dynamicLightingEnabled=false;f.antialiasingEnabled=true;
        f.view=M4::identity();f.proj=M4::perspective(std::numbers::pi_v<float>/2,f.aspectRatio,.1,100);
        f.crosshair=true;f.crosshairDot=false;
        f.video={.rgba=&video,.width=8,.height=8,.serial=2,.xPercent=80,.yPercent=80,
                 .widthPercent=20,.heightPercent=20,.keepAspect=keep};
        const GLuint out=texture(w,h);
        std::vector<uint8_t> native;
        for(float scale:scales) {
            f.renderScale=scale;
            glBindFramebuffer(GL_FRAMEBUFFER,0);assert(r.render(f,out));
            const auto image=readImage(r.m_resolveFBO,w,h);
            if(scale==1.f) native=image; else assert(image==native);
            int left=w,right=-1,top=h,bottom=-1;
            for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
                const int i=(y*w+x)*4;
                if(image[i]>240 && image[i+2]<5) {
                    left=std::min(left,x);right=std::max(right,x);
                    top=std::min(top,y);bottom=std::max(bottom,y);
                }
            }
            assert(right>=left && bottom>=top);
            const double srcW=(right-left+1)*(double)c.width/w;
            const double srcH=(bottom-top+1)*(double)c.height/h;
            const double intendedW=keep ? .2*std::min(c.width,c.height) : .2*c.width;
            const double intendedH=keep ? .2*std::min(c.width,c.height) : .2*c.height;
            assert(std::abs(srcW-intendedW)<=2 && std::abs(srcH-intendedH)<=2);
            ++overlayFrames;
        }
        glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteTextures(1,&out);
    }
    std::printf("PASS: %d video/reticle frames; Keep Aspect on/off retains source layout and final stretch; overlay pixels byte-identical at all 6 Render Scales\n",overlayFrames);

    // Execute the real sky upload AND sky shader, not just the synthetic gradient pass.
    r.actualSky=true; r.square=false; r.gradient=false;
    SFrameParams sky;sky.width=96;sky.height=64;sky.aspectRatio=16.f/9.f;sky.renderScale=.5;
    sky.view=M4::identity();sky.proj=M4::perspective(std::numbers::pi_v<float>/2,sky.aspectRatio,.1,100);
    sky.panels=&panels;sky.crosshair=false;sky.shadowsEnabled=false;sky.dynamicLightingEnabled=false;
    sky.skyboxEnabled=true;sky.skyboxSerial=1;sky.skyboxWidth=8;sky.skyboxHeight=4;
    // Padding makes the legacy wrong-stride read valid host memory; incorrect pixels are the assertion target.
    std::vector<uint8_t> skyPixels(37*20*4,0);
    for(size_t i=0;i<skyPixels.size();i+=4){skyPixels[i+1]=255;skyPixels[i+3]=255;}
    for(int i=0;i<8*4;++i){skyPixels[i*4]=255;skyPixels[i*4+1]=0;}
    sky.skyboxPixels=&skyPixels;
    GLuint skyOut=texture(sky.width,sky.height),skyRead=0;
    glGenFramebuffers(1,&skyRead);glBindFramebuffer(GL_FRAMEBUFFER,r.m_resolveFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,skyOut,0);
    glViewport(0,0,96,64);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,37);glPixelStorei(GL_UNPACK_SKIP_PIXELS,2);glPixelStorei(GL_UNPACK_SKIP_ROWS,3);
    r.drawLegacySky(sky,flipY()*sky.proj*sky.view);
    glBindFramebuffer(GL_FRAMEBUFFER,skyRead);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,r.m_customSkyTex,0);
    auto legacySky=readImage(skyRead,8,4);
    assert(legacySky[0]==0 && legacySky[1]==255); // uploaded green padding, not the intended red image
    glDeleteTextures(1,&r.m_customSkyTex);r.m_customSkyTex=0;r.m_customSkySerial=0;
    std::puts("PASS: original sky upload wrong-stride pixel corruption reproduced safely with padded input");
    for(int cycle=0;cycle<240;++cycle) {
        const bool red=cycle%2==0;
        for(int i=0;i<8*4;++i){skyPixels[i*4]=red?255:0;skyPixels[i*4+1]=red?0:255;}
        sky.skyboxSerial=cycle+2;sky.renderScale=RENDER_SCALES[cycle%6]*.01f;
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(3,5,17,19);glScissor(7,9,11,13);
        glEnable(GL_SCISSOR_TEST);glEnable(GL_STENCIL_TEST);glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_TRUE);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,37);glPixelStorei(GL_UNPACK_SKIP_PIXELS,2);glPixelStorei(GL_UNPACK_SKIP_ROWS,3);
        assert(r.render(sky,skyOut));checkState();
        auto shown=readImage(r.m_resolveFBO,96,64);
        const int centre=(32*96+48)*4;
        assert(shown[centre+(red?0:1)]>200 && shown[centre+(red?1:0)]<5);
        glBindFramebuffer(GL_FRAMEBUFFER,skyRead);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,r.m_customSkyTex,0);
        auto uploaded=readImage(skyRead,8,4);
        for(int i=0;i<32;++i){assert(uploaded[i*4+(red?0:1)]==255);assert(uploaded[i*4+(red?1:0)]==0);}
        const auto allocated=generated;
        const GLuint current=r.m_customSkyTex;
        glBindFramebuffer(GL_FRAMEBUFFER,r.m_msaaFBO);
        r.drawProductionSky(sky,flipY()*sky.proj*sky.view);
        assert(r.m_customSkyTex==current && generated==allocated); // unchanged serial: no upload/reallocation
    }
    glBindFramebuffer(GL_FRAMEBUFFER,r.m_msaaFBO);
    const GLuint oldSky=r.m_customSkyTex;const uint64_t oldSerial=r.m_customSkySerial;
    const auto textureCount=liveTextures.size();
    ++sky.skyboxSerial;failSkyUpload=true;
    r.drawProductionSky(sky,flipY()*sky.proj*sky.view);
    assert(r.m_customSkyTex==oldSky && r.m_customSkySerial==oldSerial && r.m_customSkyFailedSerial==sky.skyboxSerial);
    assert(liveTextures.size()==textureCount);
    const auto failureAllocations=generated;
    for(int i=0;i<20;++i)r.drawProductionSky(sky,flipY()*sky.proj*sky.view);
    assert(generated==failureAllocations); // a failed serial never retries forever
    ++sky.skyboxSerial;r.drawProductionSky(sky,flipY()*sky.proj*sky.view);
    assert(r.m_customSkySerial==sky.skyboxSerial && !r.m_customSkyFailedSerial && liveTextures.size()==textureCount);
    noError("sky failure recovery");
    std::puts("PASS: injected sky upload error preserves old texture/serial; no false acknowledgement/retry loop; new serial recovers");
    glBindFramebuffer(GL_FRAMEBUFFER,r.m_msaaFBO);
    sky.skyboxEnabled=false;r.drawProductionSky(sky,flipY()*sky.proj*sky.view);
    assert(!r.m_customSkyTex && !r.m_customSkySerial);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteTextures(1,&skyOut);glDeleteFramebuffers(1,&skyRead);
    r.actualSky=false;
    std::puts("PASS: 240 real sky replacements across 6 scales; pixels/shader output/serial reuse/disable cleanup correct");

    // Moving synthetic geometry + the real textured-sky shader. A -> B -> A must
    // reproduce A exactly, despite intervening camera/aspect/scale/output changes.
    r.actualSky=true; r.square=true;
    std::vector<uint8_t> panorama(16*8*4);
    for(int y=0;y<8;++y)for(int x=0;x<16;++x) {
        const int i=(y*16+x)*4;
        panorama[i]=40+x*10;panorama[i+1]=40+y*20;panorama[i+2]=80+((x+y)%3)*40;panorama[i+3]=255;
    }
    int histories=0;
    for(auto [w,h]:sizes) for(float aspect:aspects) for(float scale:scales) {
        SFrameParams a;
        a.width=w;a.height=h;a.panels=&panels;a.renderScale=scale;
        const auto c=fitContent(w,h,aspect);a.aspectRatio=(float)c.width/c.height;
        a.eye={.12f,.08f,0};a.view=M4::lookAt(a.eye,a.eye+V3{.12f,-.06f,-1},V3{0,1,0});
        a.proj=M4::perspective(std::numbers::pi_v<float>/2,a.aspectRatio,.1,100);
        a.crosshair=false;a.shadowsEnabled=false;a.dynamicLightingEnabled=false;
        a.antialiasingEnabled=histories%2==0;
        a.skyboxEnabled=true;a.skyboxWidth=16;a.skyboxHeight=8;a.skyboxPixels=&panorama;a.skyboxSerial=10000;
        GLuint outA=texture(w,h);
        glBindFramebuffer(GL_FRAMEBUFFER,0);assert(r.render(a,outA));
        const auto first=readImage(r.m_resolveFBO,w,h);
        for(size_t i=0;i<first.size();i+=4) {
            assert(first[i+3]==255);
            assert(first[i]+first[i+1]+first[i+2]>0);
        }
        auto b=a;b.width=h;b.height=w;
        b.aspectRatio=contentAspect((float)h/w,(float)((histories+1)%5),75,100);
        b.renderScale=scale==1.f?.5f:1.f;b.antialiasingEnabled=!a.antialiasingEnabled;
        b.eye={-.25f,.2f,.1f};b.view=M4::lookAt(b.eye,b.eye+V3{-.4f,.2f,-1},V3{0,1,0});
        b.proj=M4::perspective(std::numbers::pi_v<float>/3,b.aspectRatio,.1,100);
        GLuint outB=texture(b.width,b.height);
        glBindFramebuffer(GL_FRAMEBUFFER,0);assert(r.render(b,outB));
        const auto moved=readImage(r.m_resolveFBO,b.width,b.height);assert(moved!=first);
        glBindFramebuffer(GL_FRAMEBUFFER,0);assert(r.render(a,outA));
        const auto last=readImage(r.m_resolveFBO,w,h);assert(first==last);
        glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteTextures(1,&outA);glDeleteTextures(1,&outB);
        ++histories;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,r.m_msaaFBO);
    SFrameParams noSky;noSky.proj=M4::identity();noSky.view=M4::identity();
    r.drawProductionSky(noSky,M4::identity());
    r.actualSky=false;r.square=false;
    std::printf("PASS: %d A-B-A histories / %d GLES frames with real panorama shader and synthetic moving geometry; camera/FOV/aspect/scale/output/AA changes leave no previous-frame residue\n",histories,histories*3);

    // Failure must not be advertised as a completed texture.
    SFrameParams bad; bad.width=320; bad.height=180; bad.panels=&panels;
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    assert(!r.render(bad,0)); noError("missing output rejected");
    // Repeated resize/AA changes, cache reuse and failure rollback in production allocation methods.
    const auto before=std::array<size_t,3>{liveFbos.size(),liveTextures.size(),liveRbs.size()};
    CRenderer q;
    rss("before-target-stress");
    for(int cycle=0;cycle<1200;++cycle) {
        if(cycle%300==0)rss(("target-cycle-"+std::to_string(cycle)).c_str());
        const int w=48+(cycle%6)*7, h=32+(cycle%5)*3, aa=cycle%2?2:1;
        assert(q.ensureMSAA(w,h,aa) && q.ensureScaleTarget(w,h));
        assert(liveFbos.size()==before[0]+2 && liveTextures.size()==before[1]+1 && liveRbs.size()==before[2]+2);
        const size_t allocations=generated;
        const GLuint fbo=q.m_msaaFBO, tex=q.m_scaleTex;
        assert(q.ensureMSAA(w,h,aa) && q.ensureScaleTarget(w,h));
        assert(generated==allocations && q.m_msaaFBO==fbo && q.m_scaleTex==tex);
        if(cycle%300==0) {
            failChecks=1; assert(!q.ensureMSAA(w+1,h+1,aa));
            assert(q.m_msaaFBO==fbo && q.m_msaaW==w && q.m_msaaH==h);
            failChecks=1; assert(!q.ensureScaleTarget(w+1,h+1));
            assert(q.m_scaleTex==tex && q.m_scaleW==w && q.m_scaleH==h);
            assert(liveFbos.size()==before[0]+2 && liveTextures.size()==before[1]+1 && liveRbs.size()==before[2]+2);
            assert(q.ensureMSAA(w,h,aa) && q.ensureScaleTarget(w,h));
        }
    }
    assert(!q.ensureMSAA(0,32,1) && !q.ensureScaleTarget(32,0));
    GLint maximum=0; glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum);
    assert(!q.ensureScaleTarget(maximum+1,32));
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE,&maximum);
    assert(!q.ensureMSAA(maximum+1,32,1));
    q.releaseSceneTargets(); q.releaseSceneTargets();
    assert(liveFbos.size()==before[0] && liveTextures.size()==before[1] && liveRbs.size()==before[2]);
    noError("resource stress");
    rss("after-target-release");
    std::puts("PASS: 1200 resize cycles; zero allocations on same-size reuse; 8 injected failures rolled back; invalid sizes rejected; tracked live targets bounded");
    // Release all fixture-owned objects before destroying the context.
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    r.releaseSceneTargets();
    for(GLuint f:{r.m_resolveFBO}) if(f) glDeleteFramebuffers(1,&f);
    for(GLuint b:{r.m_msaaColor,r.m_msaaDepth}) if(b) glDeleteRenderbuffers(1,&b);
    for(GLuint t:{r.m_scaleTex,r.m_videoTex,r.m_hudGL[0].tex,r.m_hudGL[1].tex}) if(t) glDeleteTextures(1,&t);
    for(GLuint p:{r.m_progSky,r.m_progScale,r.m_progVideo,r.m_progHud,r.m_progCross,r.m_progGradient,r.m_progSquare}) if(p) glDeleteProgram(p);
    glDeleteBuffers(1,&r.m_quadVBO); glDeleteVertexArrays(1,&r.m_quadVAO);
    noError("cleanup");
    assert(liveFbos.empty() && liveTextures.empty() && liveRbs.empty());
    std::puts("PASS: all tracked GL textures/framebuffers/renderbuffers released at fixture shutdown");
    eglMakeCurrent(display,nullptr,nullptr,nullptr); eglDestroyContext(display,context); eglTerminate(display);
}
