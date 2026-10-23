#include "Readback.hpp"
#include <GLES3/gl3.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <map>

namespace hyprwalk::mp::media {
namespace {
using Clock=std::chrono::steady_clock;
struct State {
    GLint read=0,draw=0,pack=0,unpack=0,texture=0,align=0,row=0,rows=0,pixels=0;
    GLboolean scissor=GL_FALSE;
    State(){
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glGetIntegerv(GL_PACK_ALIGNMENT,&align);
        glGetIntegerv(GL_PACK_ROW_LENGTH,&row);glGetIntegerv(GL_PACK_SKIP_ROWS,&rows);glGetIntegerv(GL_PACK_SKIP_PIXELS,&pixels);
        scissor=glIsEnabled(GL_SCISSOR_TEST);glDisable(GL_SCISSOR_TEST);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,1);
        glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    }
    ~State(){
        glBindFramebuffer(GL_READ_FRAMEBUFFER,read);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);
        glBindBuffer(GL_PIXEL_PACK_BUFFER,pack);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack);glBindTexture(GL_TEXTURE_2D,texture);
        glPixelStorei(GL_PACK_ALIGNMENT,align);glPixelStorei(GL_PACK_ROW_LENGTH,row);glPixelStorei(GL_PACK_SKIP_ROWS,rows);glPixelStorei(GL_PACK_SKIP_PIXELS,pixels);
        if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
    }
};
}
struct Readback::Impl {
    struct Slot {GLuint pbo=0;GLsync fence=nullptr;unsigned width=0,height=0;std::size_t allocated=0;Clock::time_point at{};};
    struct Source {GLuint texture=0,fbo=0;unsigned width=0,height=0;std::array<Slot,2> slots;};
    std::map<VideoTrackKey,Source> sources;
    ReadbackMetrics metrics;
    std::uint64_t serial=0;
    static void destroy(Source& s) noexcept {
        for(auto& p:s.slots){if(p.fence)glDeleteSync(p.fence);if(p.pbo)glDeleteBuffers(1,&p.pbo);p={};}
        if(s.fbo)glDeleteFramebuffers(1,&s.fbo);if(s.texture)glDeleteTextures(1,&s.texture);s={};
    }
};
Readback::Readback():m(std::make_unique<Impl>()){}
Readback::~Readback()=default;
const ReadbackMetrics& Readback::metrics() const noexcept{return m->metrics;}
void Readback::clear() noexcept {
    for(auto& [key,source]:m->sources){
        (void)key;
        // clear() is used on stream teardown/context reset; account for work
        // cancelled while its fence is still outstanding instead of hiding it.
        for(const auto& slot:source.slots)if(slot.fence)++m->metrics.dropped;
        Impl::destroy(source);
    }
    m->sources.clear();m->metrics.error.clear();
}
void Readback::retain(const std::vector<VideoTrackKey>& permitted){
    std::erase_if(m->sources,[&](auto& entry){
        if(std::find(permitted.begin(),permitted.end(),entry.first)!=permitted.end())return false;
        // Removing a revoked source can cancel GPU reads that have not reached
        // take() yet. Count these as dropped work instead of silently inflating
        // issued while completed/dropped stay at zero.
        for(const auto& slot:entry.second.slots)if(slot.fence)++m->metrics.dropped;
        Impl::destroy(entry.second);
        return true;
    });
}
void Readback::issue(const VideoTrackKey& key,unsigned fbo,unsigned width,unsigned height,const VideoSettings& settings){
    const auto begin=Clock::now();
    if(!fbo||!width||!height||width>8192||height>8192||!validVideoSettings(settings))return;
    if(!m->sources.contains(key)&&m->sources.size()>=MaxVideoStreams){++m->metrics.dropped;return;}
    auto& source=m->sources[key];
    // capturePanels() issues readback only after a source surface commit (or an
    // explicit optional FPS cap). No second timer belongs in this GPU readback path.
    auto it=std::find_if(source.slots.begin(),source.slots.end(),[](const auto& slot){return !slot.fence;});
    if(it==source.slots.end()){++m->metrics.dropped;return;}
    const auto [w,h]=fitVideoSize(width,height,settings);if(!w||!h)return;
    auto& slot=*it;State state;
    if(glGetError()!=GL_NO_ERROR){m->metrics.error="GL error before video readback; capture stopped";++m->metrics.dropped;return;}
    if(!source.texture)glGenTextures(1,&source.texture);
    if(!source.fbo)glGenFramebuffers(1,&source.fbo);
    if(!slot.pbo)glGenBuffers(1,&slot.pbo);
    if(!source.texture||!source.fbo||!slot.pbo){m->metrics.error="Readback GL allocation failed";++m->metrics.dropped;return;}
    glBindTexture(GL_TEXTURE_2D,source.texture);
    if(source.width!=w||source.height!=h){
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);source.width=w;source.height=h;
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,source.fbo);glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,source.texture,0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo);
    if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE||glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){
        m->metrics.error="Readback framebuffer incomplete";++m->metrics.dropped;return;
    }
    // Existing Hyprwalk capture shader deliberately maps application top to GL row 0.
    // Copy exactly the populated rectangle, not the 128-rounded texture allocation.
    glBlitFramebuffer(0,0,width,height,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,source.fbo);glBindBuffer(GL_PIXEL_PACK_BUFFER,slot.pbo);
    const auto bytes=std::size_t(w)*h*4;
    if(slot.allocated!=bytes){glBufferData(GL_PIXEL_PACK_BUFFER,static_cast<GLsizeiptr>(bytes),nullptr,GL_STREAM_READ);slot.allocated=bytes;}
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    if(glGetError()!=GL_NO_ERROR){slot.allocated=0;m->metrics.error="GL readback/blit failed; no pixels admitted";++m->metrics.dropped;return;}
    slot.fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    if(!slot.fence){m->metrics.error="Readback GPU fence allocation failed";++m->metrics.dropped;return;}
    slot.width=w;slot.height=h;slot.at=begin;glFlush();++m->metrics.issued;
    m->metrics.maxIssueUs=std::max(m->metrics.maxIssueUs,std::chrono::duration<double,std::micro>(Clock::now()-begin).count());
}
std::vector<std::shared_ptr<const VideoFrame>> Readback::take(){
    std::vector<std::shared_ptr<const VideoFrame>> result;if(m->sources.empty())return result;
    State state;
    for(auto& [key,source]:m->sources)for(auto& slot:source.slots){
        if(!slot.fence)continue;
        const auto waited=glClientWaitSync(slot.fence,0,0); // NEVER glFinish or nonzero CPU wait
        const auto age=Clock::now()-slot.at;
        if(waited==GL_TIMEOUT_EXPIRED&&age<=std::chrono::milliseconds(500))continue;
        // Polling a fence must remain non-blocking, but a fence which never
        // signals must not occupy one of the two PBO slots forever. The old
        // ordering continued before checking age, making the 500 ms watchdog
        // unreachable for GL_TIMEOUT_EXPIRED. Also avoid delivering stale frames
        // if a fence finally signals only after the low-latency age budget.
        glDeleteSync(slot.fence);slot.fence=nullptr;
        if(waited==GL_WAIT_FAILED||age>std::chrono::milliseconds(500)){
            ++m->metrics.dropped;
            if(waited==GL_TIMEOUT_EXPIRED)++m->metrics.fenceTimeouts;
            continue;
        }
        const auto start=Clock::now();glBindBuffer(GL_PIXEL_PACK_BUFFER,slot.pbo);
        const auto bytes=std::size_t(slot.width)*slot.height*4;
        auto frame=std::make_shared<VideoFrame>();frame->key=key;frame->width=slot.width;frame->height=slot.height;frame->time=slot.at;frame->serial=++m->serial;
        frame->rgba.resize(bytes); // allocation before mapping; no throwing work while mapped
        const auto* pixels=glMapBufferRange(GL_PIXEL_PACK_BUFFER,0,static_cast<GLsizeiptr>(bytes),GL_MAP_READ_BIT);
        if(!pixels){m->metrics.error="Readback PBO map failed";++m->metrics.dropped;continue;}
        std::memcpy(frame->rgba.data(),pixels,bytes);
        const bool valid=glUnmapBuffer(GL_PIXEL_PACK_BUFFER)==GL_TRUE;
        if(valid){result.push_back(std::move(frame));++m->metrics.completed;m->metrics.bytes+=bytes;}else ++m->metrics.dropped;
        m->metrics.maxCopyUs=std::max(m->metrics.maxCopyUs,std::chrono::duration<double,std::micro>(Clock::now()-start).count());
    }
    return result;
}
} // namespace hyprwalk::mp::media
