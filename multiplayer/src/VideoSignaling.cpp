#include <hyprwalk/mp/VideoSignaling.hpp>
#include <hyprwalk/mp/Protocol.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>

namespace hyprwalk::mp {
namespace {
struct Writer {
    std::vector<std::uint8_t> b;
    void u(std::uint64_t n,unsigned count){ for(unsigned i=count;i;--i)b.push_back(static_cast<std::uint8_t>(n>>((i-1)*8))); }
    void id(const Id& n){b.insert(b.end(),n.begin(),n.end());}
};
struct Reader {
    std::span<const std::uint8_t> b;std::size_t at=0;
    std::uint64_t u(unsigned n){if(n>b.size()-at)throw std::runtime_error("truncated video signal");std::uint64_t v=0;while(n--)v=(v<<8)|b[at++];return v;}
    Id id(){Id v{};for(auto& n:v)n=static_cast<std::uint8_t>(u(1));return v;}
};
bool keyValid(const VideoTrackKey& k) {
    return wire::nonzero(k.window.session)&&wire::nonzero(k.window.owner)&&wire::nonzero(k.window.generation)&&
           k.window.window&&k.publication&&k.stream;
}
std::size_t bodyLimit(VideoSignalKind k){switch(k){case VideoSignalKind::Offer:case VideoSignalKind::Answer:return MaxVideoSignal;case VideoSignalKind::Ice:return 2048;default:return 256;}}
bool kindValid(VideoSignalKind k){return k>=VideoSignalKind::Offer&&k<=VideoSignalKind::Failure;}
}
bool validVideoSettings(const VideoSettings& s) noexcept {
    return s.width>=64&&s.width<=8192&&s.height>=64&&s.height<=8192&&!(s.width&1)&&!(s.height&1)&&
           (s.fps==0||s.fps<=30)&&s.bitrate>=64000&&s.bitrate<=12000000&&s.streams>=1&&s.streams<=MaxVideoStreams;
}
std::pair<unsigned,unsigned> fitVideoSize(unsigned w,unsigned h,const VideoSettings& s) noexcept {
    if(!w||!h||!validVideoSettings(s))return {};
    const double scale=std::min({1.0,double(s.width)/w,double(s.height)/h});
    // Keep native dimensions when under the ceiling instead of quantizing to a preset.
    // The RGBA readback has tightly packed rows; GStreamer converts to I420 for VP8.
    return {std::max(2U,static_cast<unsigned>(std::floor(w*scale))),
            std::max(2U,static_cast<unsigned>(std::floor(h*scale)))};
}
bool validVideoFrame(const VideoFrame& f) noexcept {
    return keyValid(f.key)&&f.width>=2&&f.width<=8192&&f.height>=2&&f.height<=8192&&
           f.rgba.size()==static_cast<std::size_t>(f.width)*f.height*4;
}
bool validVideoSignal(const VideoSignal& s,std::string& error) {
    error.clear();
    if(!keyValid(s.key)||!wire::nonzero(s.sender)||!s.serial||!kindValid(s.kind)){error="invalid video identifier/kind/sequence";return false;}
    if(s.body.empty()||s.body.size()>bodyLimit(s.kind)){error="invalid video signal length";return false;}
    // SDP/ICE are ASCII. Only SDP permits line breaks; never terminal escapes or NUL.
    for(unsigned char c:s.body)if((c<32||c>126)&& !((s.kind==VideoSignalKind::Offer||s.kind==VideoSignalKind::Answer)&&(c=='\r'||c=='\n'||c=='\t'))){error="invalid video signaling text";return false;}
    const bool owner=s.sender==s.key.window.owner;
    if((s.kind==VideoSignalKind::Offer&&!owner)||((s.kind==VideoSignalKind::Answer||s.kind==VideoSignalKind::Decoded)&&owner)){
        error="video signal from wrong publisher/receiver";return false;
    }
    return true;
}
bool videoScope(const VideoSignal& v,const Status& s,bool incoming) noexcept {
    return s.state==State::Connected&&s.world==WorldState::Ready&&(s.capabilities&VideoCapability)&&
           v.key.window.session==s.session&&v.key.window.generation==s.worldTicket&&
           (v.key.window.owner==s.localId||v.key.window.owner==s.peerId)&&
           v.sender==(incoming?s.peerId:s.localId);
}
std::vector<std::vector<std::uint8_t>> fragmentVideoSignal(const VideoSignal& s){
    std::string error;if(!validVideoSignal(s,error))throw std::invalid_argument(error);
    const auto count=(s.body.size()+VideoChunkBytes-1)/VideoChunkBytes;
    std::vector<std::vector<std::uint8_t>> result;result.reserve(count);
    for(std::size_t i=0;i<count;++i){
        Writer p;p.u(VideoSignalingVersion,2);p.u(static_cast<unsigned>(s.kind),1);p.u(0,1);
        p.id(s.key.window.session);p.id(s.key.window.owner);p.id(s.key.window.generation);p.u(s.key.window.window,8);
        p.u(s.key.publication,8);p.u(s.key.stream,8);p.id(s.sender);p.u(s.serial,8);
        p.u(i,2);p.u(count,2);p.u(s.body.size(),4);
        const auto from=i*VideoChunkBytes,n=std::min(VideoChunkBytes,s.body.size()-from);
        p.b.insert(p.b.end(),s.body.begin()+from,s.body.begin()+from+n);result.push_back(std::move(p.b));
    }
    return result;
}
bool decodeVideoChunk(std::span<const std::uint8_t> bytes,VideoChunk& out,std::string& error){
    error.clear();try{
        if(bytes.size()>4096)throw std::runtime_error("video chunk exceeds TCP envelope");
        Reader r{bytes};if(r.u(2)!=VideoSignalingVersion)throw std::runtime_error("unsupported video signaling version");
        VideoChunk p;p.signal.kind=static_cast<VideoSignalKind>(r.u(1));if(r.u(1))throw std::runtime_error("video reserved flags");
        auto& s=p.signal;s.key.window.session=r.id();s.key.window.owner=r.id();s.key.window.generation=r.id();s.key.window.window=r.u(8);
        s.key.publication=r.u(8);s.key.stream=r.u(8);s.sender=r.id();s.serial=r.u(8);
        p.index=static_cast<std::uint16_t>(r.u(2));p.count=static_cast<std::uint16_t>(r.u(2));p.total=static_cast<std::uint32_t>(r.u(4));
        if(!kindValid(s.kind)||!p.total||p.total>bodyLimit(s.kind)||p.count!=(p.total+VideoChunkBytes-1)/VideoChunkBytes||p.index>=p.count)
            throw std::runtime_error("invalid video fragment count/size");
        const auto expected=std::min<std::size_t>(VideoChunkBytes,p.total-std::size_t(p.index)*VideoChunkBytes);
        if(bytes.size()-r.at!=expected)throw std::runtime_error("invalid video fragment bytes");
        s.body.assign(reinterpret_cast<const char*>(bytes.data()+r.at),expected);
        if(!validVideoSignal(s,error))return false;
        out=std::move(p);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void VideoAssembler::reset() noexcept {m_start={};m_body.clear();m_next=0;m_last=0;m_deadline={};}
bool VideoAssembler::expire(Clock::time_point now) noexcept {
    if(m_next&&now>=m_deadline){m_body.clear();m_next=0;m_deadline={};return true;}return false;
}
std::optional<VideoSignal> VideoAssembler::accept(std::span<const std::uint8_t> b,Clock::time_point now,std::string& error){
    error.clear();if(expire(now)){error="video reassembly timeout";return {};}
    VideoChunk p;if(!decodeVideoChunk(b,p,error)){m_body.clear();m_next=0;return {};}
    if(p.index==0){
        if(m_next||p.signal.serial<=m_last){error="interleaved/replayed video signal";m_body.clear();m_next=0;return {};}
        m_start=p;m_start.signal.body.clear();m_body.clear();m_deadline=now+std::chrono::seconds(5);
    }
    if(p.index!=m_next||p.signal.serial!=m_start.signal.serial||p.signal.key!=m_start.signal.key||p.signal.sender!=m_start.signal.sender||
       p.signal.kind!=m_start.signal.kind||p.count!=m_start.count||p.total!=m_start.total){
        error="unordered or mismatched video fragment";m_body.clear();m_next=0;return {};
    }
    m_body+=p.signal.body;++m_next;
    if(m_next!=m_start.count)return {};
    VideoSignal result=m_start.signal;result.body=std::move(m_body);m_body.clear();m_next=0;m_deadline={};
    if(result.body.size()!=m_start.total||!validVideoSignal(result,error))return {};
    m_last=result.serial;return result;
}
} // namespace hyprwalk::mp
