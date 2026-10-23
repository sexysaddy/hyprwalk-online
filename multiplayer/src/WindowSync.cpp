#include <hyprwalk/mp/WindowSync.hpp>
#include <hyprwalk/mp/ContentHash.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hyprwalk::mp {
namespace {
bool nonzero(const Id& v) { return std::any_of(v.begin(),v.end(),[](auto b){return b != 0;}); }
// Strict UTF-8; control codes and bidi/zero-width formatting are not UI text.
std::size_t rune(std::string_view s, std::uint32_t& c) noexcept {
    if (s.empty()) return 0;
    const auto a = static_cast<unsigned char>(s[0]);
    std::size_t n = 0;
    if (a < 0x80) { c=a; n=1; }
    else if (a >= 0xc2 && a <= 0xdf) { c=a&31; n=2; }
    else if (a >= 0xe0 && a <= 0xef) { c=a&15; n=3; }
    else if (a >= 0xf0 && a <= 0xf4) { c=a&7; n=4; }
    if (!n || s.size()<n) return 0;
    for (std::size_t i=1;i<n;++i) {
        const auto b=static_cast<unsigned char>(s[i]);
        if ((b&0xc0)!=0x80) return 0;
        c=(c<<6)|(b&63);
    }
    if ((n==2 && c<0x80) || (n==3 && c<0x800) || (n==4 && c<0x10000) ||
        c>0x10ffff || (c>=0xd800 && c<=0xdfff)) return 0;
    if (c<32 || (c>=0x7f && c<=0x9f) || (c>=0x200b && c<=0x200f) ||
        (c>=0x2028 && c<=0x202e) || (c>=0x2060 && c<=0x206f) || c==0xfeff) return 0;
    return n;
}
float dot3(const std::array<float,3>& a,const std::array<float,3>& b) {
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}
struct Writer {
    std::vector<std::uint8_t> b;
    void u(std::uint64_t v,unsigned n) { for (unsigned i=n;i;--i) b.push_back(static_cast<std::uint8_t>(v>>((i-1)*8))); }
    void id(const Id& v) { b.insert(b.end(),v.begin(),v.end()); }
    void f(float v) { static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559); u(std::bit_cast<std::uint32_t>(v),4); }
    void text(const std::string& v) {u(v.size(),2); b.insert(b.end(),v.begin(),v.end());}
};
struct Reader {
    std::span<const std::uint8_t> b; std::size_t at=0;
    std::uint64_t u(unsigned n) { if(n>b.size()-at) throw std::runtime_error("truncated window field"); std::uint64_t v=0; while(n--)v=(v<<8)|b[at++]; return v; }
    Id id() {Id v{};for(auto& x:v)x=static_cast<std::uint8_t>(u(1));return v;}
    float f(){return std::bit_cast<float>(static_cast<std::uint32_t>(u(4)));}
    std::string text(std::size_t limit) {
        const auto n=static_cast<std::size_t>(u(2));
        if(n>limit || n>b.size()-at)throw std::runtime_error("window text length exceeded");
        std::string v(b.begin()+at,b.begin()+at+n);at+=n;return v;
    }
};
}
bool validWindowText(std::string_view t,std::size_t maxBytes) noexcept {
    if(t.size()>maxBytes)return false;
    while(!t.empty()){std::uint32_t c=0;const auto n=rune(t,c);if(!n)return false;t.remove_prefix(n);}return true;
}
std::string cleanWindowText(std::string_view t,std::size_t maxBytes) {
    std::string out;
    // Bound inspected bytes as well as output; hostile local titles cannot cause unbounded work.
    t=t.substr(0,std::min(t.size(),maxBytes*4+4));
    while(!t.empty() && out.size()<maxBytes){std::uint32_t c=0;auto n=rune(t,c);if(!n){out+='?';t.remove_prefix(1);}else{if(out.size()+n>maxBytes)break;out.append(t.substr(0,n));t.remove_prefix(n);}}
    return out;
}
std::string applicationKey(std::string_view app) {
    if(app.empty())return {};
    if(validWindowText(app,MaxApplicationId))return std::string(app);
    // Never truncate a long application ID into a different application's profile.
    ContentHash hash;
    hash.update(std::span(reinterpret_cast<const std::uint8_t*>(app.data()),app.size()));
    constexpr char hex[]="0123456789abcdef";
    std::string out;for(auto b:hash.finish()){out+=hex[b>>4];out+=hex[b&15];}return out;
}
bool validWindow(const WindowMetadata& w,std::string& error) {
    error.clear();
    if(!w.id)error="zero window ID";
    else if(!validWindowText(w.title,MaxWindowTitle)||!validWindowText(w.application,MaxApplicationId))error="invalid UTF-8 window text";
    const auto& g=w.geometry;
    for(const auto* a:{&g.origin,&g.right,&g.down})for(float v:*a)if(!std::isfinite(v)||std::abs(v)>100000.f)error="non-finite/out-of-range window coordinates";
    if(!std::isfinite(g.width)||!std::isfinite(g.height)||g.width<0.001f||g.height<0.001f||g.width>1000.f||g.height>1000.f)error="invalid window dimensions";
    if(!g.pixelWidth||!g.pixelHeight||g.pixelWidth>16384||g.pixelHeight>16384)error="invalid source pixel dimensions";
    if(std::abs(dot3(g.right,g.right)-1.f)>0.002f || std::abs(dot3(g.down,g.down)-1.f)>0.002f || std::abs(dot3(g.right,g.down))>0.002f)error="window axes must be orthonormal";
    return error.empty();
}
bool validWindowSnapshot(const WindowSnapshot& s,std::string& error) {
    error.clear();
    if(!nonzero(s.session)||!nonzero(s.owner)||!nonzero(s.generation)||!s.revision){error="invalid window session/generation/revision";return false;}
    if(s.windows.size()>MaxSharedWindows){error="window count exceeds 16";return false;}
    std::uint64_t previous=0;
    for(const auto& w:s.windows){if(w.id<=previous){error="window IDs must be sorted and unique";return false;}previous=w.id;if(!validWindow(w,error))return false;}
    return true;
}
std::vector<std::uint8_t> encodeWindows(const WindowSnapshot& s) {
    std::string why;if(!validWindowSnapshot(s,why))throw std::invalid_argument(why);
    Writer p;p.u(WindowProtocolVersion,2);p.u(0,2);p.id(s.session);p.id(s.owner);p.id(s.generation);p.u(s.revision,8);p.u(s.windows.size(),1);
    for(const auto& w:s.windows){
        p.u(w.id,8);for(const auto* a:{&w.geometry.origin,&w.geometry.right,&w.geometry.down})for(float f:*a)p.f(f);
        p.f(w.geometry.width);p.f(w.geometry.height);p.u(w.geometry.pixelWidth,2);p.u(w.geometry.pixelHeight,2);p.text(w.application);p.text(w.title);
    }
    if(p.b.size()>4096)throw std::length_error("window payload exceeds envelope");
    return std::move(p.b);
}
bool decodeWindows(std::span<const std::uint8_t> bytes,WindowSnapshot& out,std::string& error) {
    error.clear();try{
        if(bytes.size()>4096)throw std::runtime_error("window payload too large");
        Reader r{bytes};if(r.u(2)!=WindowProtocolVersion)throw std::runtime_error("unsupported windows version");if(r.u(2))throw std::runtime_error("nonzero windows flags");
        WindowSnapshot s;s.session=r.id();s.owner=r.id();s.generation=r.id();s.revision=r.u(8);
        const auto n=r.u(1);if(n>MaxSharedWindows)throw std::runtime_error("window count exceeds 16");
        for(std::size_t i=0;i<n;++i){WindowMetadata w;w.id=r.u(8);for(auto* a:{&w.geometry.origin,&w.geometry.right,&w.geometry.down})for(auto& f:*a)f=r.f();
            w.geometry.width=r.f();w.geometry.height=r.f();w.geometry.pixelWidth=static_cast<std::uint16_t>(r.u(2));w.geometry.pixelHeight=static_cast<std::uint16_t>(r.u(2));
            w.application=r.text(MaxApplicationId);w.title=r.text(MaxWindowTitle);s.windows.push_back(std::move(w));}
        if(r.at!=bytes.size())throw std::runtime_error("trailing window bytes");
        if(!validWindowSnapshot(s,error))return false;
        out=std::move(s);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
std::optional<WindowHit> hitRemoteWindow(const RemoteWindow& w,const std::array<float,3>& eye,const std::array<float,3>& d) {
    if(w.receive.hidden)return {};
    const auto& g=w.metadata.geometry;
    const std::array<float,3> n{g.down[1]*g.right[2]-g.down[2]*g.right[1],g.down[2]*g.right[0]-g.down[0]*g.right[2],g.down[0]*g.right[1]-g.down[1]*g.right[0]};
    const auto denom=dot3(d,n);if(!std::isfinite(denom)||denom>=-1e-5f)return {};
    const std::array<float,3> a{g.origin[0]-eye[0],g.origin[1]-eye[1],g.origin[2]-eye[2]};
    const float t=dot3(a,n)/denom;if(t<=0||!std::isfinite(t))return {};
    const std::array<float,3> p{eye[0]+d[0]*t-g.origin[0],eye[1]+d[1]*t-g.origin[1],eye[2]+d[2]*t-g.origin[2]};
    const float x=dot3(p,g.right),y=dot3(p,g.down);
    if(x<0||x>g.width)return {};
    if(y>=0&&y<=g.height)return WindowHit{w.key,t};
    return {};
}
const char* sharingModeName(SharingMode m) noexcept {switch(m){case SharingMode::Off:return "Off";case SharingMode::Selected:return "Selected Windows";case SharingMode::All:return "All Windows";}return "Invalid";}
bool WindowSync::session(const Status& s) {
    const bool ready=s.state==State::Connected && s.world==WorldState::Ready && (s.capabilities&WindowsCapability);
    const bool changed=ready!=m_ready || s.session!=m_session || s.localId!=m_localId || s.peerId!=m_peer || s.worldTicket!=m_generation;
    if(!changed)return false;
    m_ready=ready;m_session=s.session;m_localId=s.localId;m_peer=s.peerId;m_generation=s.worldTicket;
    m_token=ready?idString(s.worldTicket):std::string{};
    m_mode=SharingMode::Off;m_publicationEpoch.clear();m_localVideo.clear();m_selected.clear();m_local.clear();m_remote.clear();m_receivedRevision=0;m_chosen=0;m_error.clear();++m_revision;
    return true;
}
void WindowSync::sources(std::vector<WindowMetadata> sources) {
    std::string why;
    if(sources.size()>MaxLocalWindows){sources.clear();revoke();m_error="Too many local windows; sharing stopped.";}
    std::sort(sources.begin(),sources.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    std::uint64_t previous=0;
    for(const auto& s:sources)if(!validWindow(s,why)||s.id<=previous){sources.clear();revoke();m_error="Invalid local source metadata; sharing stopped.";break;}else previous=s.id;
    if(m_local==sources)return;
    m_local=std::move(sources);++m_revision;
    std::erase_if(m_selected,[&](auto id){return std::none_of(m_local.begin(),m_local.end(),[&](const auto& s){return s.id==id;});});
    if(m_mode==SharingMode::All && m_local.size()>MaxSharedWindows){revoke();m_error="All Windows exceeds the 16-window limit. Sharing stopped; use Selected.";}
    refreshPublicationEpochs();
}
void WindowSync::revoke() noexcept {m_mode=SharingMode::Off;m_selected.clear();m_publicationEpoch.clear();m_localVideo.clear();++m_revision;}
bool WindowSync::setMode(SharingMode mode,bool confirm) {
    if(mode==SharingMode::Off){revoke();m_error.clear();return true;}
    if(!m_ready){m_error="Connect to a peer supporting window metadata and wait for Map ready.";return false;}
    if(mode==SharingMode::All&&!confirm){m_error="All Windows requires explicit confirmation (includes future eligible windows).";return false;}
    if(mode==SharingMode::All&&m_local.size()>MaxSharedWindows){m_error="More than 16 eligible windows; use Selected instead.";return false;}
    m_mode=mode;m_error.clear();refreshPublicationEpochs();++m_revision;return true;
}
bool WindowSync::select(std::uint64_t id,bool yes) {
    if(!m_ready||m_mode!=SharingMode::Selected){m_error="Choose Selected Windows first.";return false;}
    if(std::none_of(m_local.begin(),m_local.end(),[&](const auto& s){return s.id==id;})){m_error="This local window no longer exists.";return false;}
    if(yes&&!m_selected.contains(id)&&m_selected.size()>=MaxSharedWindows){m_error="At most 16 windows may be shared.";return false;}
    if(yes)m_selected.insert(id);else m_selected.erase(id);m_error.clear();refreshPublicationEpochs();++m_revision;return true;
}
void WindowSync::refreshPublicationEpochs() {
    const auto p=publication();
    std::erase_if(m_publicationEpoch,[&](const auto& v){return std::none_of(p.windows.begin(),p.windows.end(),[&](const auto& w){return w.id==v.first;});});
    std::erase_if(m_localVideo,[&](const auto& v){return !m_publicationEpoch.contains(v.first);});
    for(const auto& w:p.windows)if(!m_publicationEpoch.contains(w.id)){
        if(m_nextPublication==UINT64_MAX){revoke();m_error="Publication generation exhausted; restart the plugin.";return;}
        m_publicationEpoch.emplace(w.id,++m_nextPublication);
    }
}
std::uint64_t WindowSync::publicationEpoch(std::uint64_t id) const noexcept {
    const auto it=m_publicationEpoch.find(id);return it==m_publicationEpoch.end()?0:it->second;
}
void WindowSync::videoEnable(bool on) {
    if(m_videoEnabled==on)return;
    m_videoEnabled=on;m_localVideo.clear();m_publicationEpoch.clear();refreshPublicationEpochs();++m_revision;
}
void WindowSync::videoBackend(bool available,std::string detail) {
    if(m_videoAvailable==available&&m_videoSummary==detail)return;
    m_videoAvailable=available;m_videoSummary=std::move(detail);++m_revision;
}
std::string WindowSync::localVideo(std::uint64_t id) const {
    if(!m_videoEnabled)return "Video disabled";
    if(!m_videoAvailable)return m_videoSummary;
    const auto it=m_localVideo.find(id);return it==m_localVideo.end()?"Waiting for frames":it->second;
}
void WindowSync::videoStatus(std::uint64_t id,bool sending,std::string detail) {
    detail=cleanWindowText(detail,240);
    if(sending){if(!publicationEpoch(id))return;auto& old=m_localVideo[id];if(old!=detail){old=std::move(detail);++m_revision;}}
    else if(auto* w=mutableRemote(id);w&&w->video!=detail){w->video=std::move(detail);++m_revision;}
}
WindowSnapshot WindowSync::publication() const {
    WindowSnapshot s{m_session,m_localId,m_generation,1,{}};
    if(m_ready&&m_mode!=SharingMode::Off)for(const auto& w:m_local)if(m_mode==SharingMode::All||m_selected.contains(w.id))s.windows.push_back(w);
    return s;
}
bool WindowSync::receive(const ReceivedWindows& event) {
    const auto& s=event.snapshot;
    if(!m_ready||s.session!=m_session||s.owner!=m_peer||s.generation!=m_generation)return false;
    if(event.expired){if(s.revision<m_receivedRevision)return false;m_receivedRevision=s.revision;m_remote.clear();m_chosen=0;++m_revision;return true;}
    std::string why;if(s.revision<=m_receivedRevision||!validWindowSnapshot(s,why))return false;
    std::vector<RemoteWindow> next;
    for(const auto& metadata:s.windows){
        RemoteWindow w{{s.session,s.owner,s.generation,metadata.id},metadata,{}};
        if(const auto old=findRemote(metadata.id);old&&old->metadata.application==metadata.application){w.receive=old->receive;w.video=old->video;}
        next.push_back(std::move(w));
    }
    m_remote=std::move(next);m_receivedRevision=s.revision;
    if(!findRemote(m_chosen))m_chosen=0;
    ++m_revision;return true;
}
const RemoteWindow* WindowSync::findRemote(std::uint64_t id) const {const auto it=std::find_if(m_remote.begin(),m_remote.end(),[&](const auto& w){return w.key.window==id;});return it==m_remote.end()?nullptr:&*it;}
RemoteWindow* WindowSync::mutableRemote(std::uint64_t id){return const_cast<RemoteWindow*>(std::as_const(*this).findRemote(id));}
bool WindowSync::hide(std::uint64_t id,bool value){if(auto* w=mutableRemote(id)){w->receive.hidden=value;if(value&&m_chosen==id)m_chosen=0;++m_revision;return true;}return false;}
bool WindowSync::choose(std::uint64_t id){if(id&&!findRemote(id))return false;m_chosen=id;++m_revision;return true;}
} // namespace hyprwalk::mp
