#include <hyprwalk/mp/MapSync.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace hyprwalk::mp {
namespace {
bool nonzero(const Digest& d) { return std::any_of(d.begin(),d.end(),[](auto b){return b!=0;}); }
bool finite(float f,float limit=1000000.f) {return std::isfinite(f)&&std::abs(f)<=limit;}
bool finite(Vec3 v) {return finite(v.x)&&finite(v.y)&&finite(v.z);}
}
const char* worldStateName(WorldState state) noexcept {
    switch (state) {
        case WorldState::Disabled:return "disabled";case WorldState::WaitingOffer:return "waiting_offer";
        case WorldState::Applying:return "applying";case WorldState::WaitingPeer:return "waiting_peer";
        case WorldState::Ready:return "ready";case WorldState::Rejected:return "rejected";
    }
    return "invalid";
}
bool validMap(const MapDescriptor& m,std::string& error) {
    error.clear();
    if (m.geometryVersion!=1) error="unsupported map geometry version";
    else if (m.kind!=MapKind::Courtyard && m.kind!=MapKind::EmbeddedGlb) error="unsupported map kind";
    else if (!finite(m.aspect)||m.aspect<0.1f||m.aspect>10.f||!finite(m.height)||m.height<0.1f||m.height>100.f||
             !finite(m.centerY)||!finite(m.scale)||m.scale<0.000001f||!finite(m.spawn)||!finite(m.spawnYaw)||
             !finite(m.desktopCenter)||!finite(m.desktopNormal)||!finite(m.desktopHeight)||m.desktopHeight<0.1f||m.desktopHeight>100.f)
        error="invalid map geometry values";
    else {
        const auto n=m.desktopNormal.x*m.desktopNormal.x+m.desktopNormal.y*m.desktopNormal.y+m.desktopNormal.z*m.desktopNormal.z;
        if (n<0.99f||n>1.01f) error="desktop normal must be normalized";
        else if (!nonzero(m.collision)) error="map collision fingerprint missing";
        else if (m.kind==MapKind::EmbeddedGlb && !nonzero(m.content)) error="embedded GLB content fingerprint missing";
        else if (m.kind==MapKind::Courtyard && (m.scale!=1.f||nonzero(m.content))) error="invalid courtyard descriptor";
    }
    return error.empty();
}
bool compatibleMap(const MapDescriptor& local,const MapDescriptor& host,std::string& error) {
    if (!validMap(host,error)||!validMap(local,error)) return false;
    if (local.kind!=host.kind) error="different map kinds: manually select the host map before join";
    else if (local.geometryVersion!=host.geometryVersion) error="different geometry interpreters";
    else if (host.kind==MapKind::EmbeddedGlb) {
        if (local.content!=host.content) error="different GLB content: manually install/select the same embedded GLB";
        else if (local.scale!=host.scale) error="different map scale: set the host scale locally and reload before join";
        else if (local.collision!=host.collision) error="different collision geometry: map interpreters disagree";
    }
    return error.empty();
}
std::vector<std::uint8_t> encodeMap(const MapDescriptor& m) {
    std::string error;if(!validMap(m,error)) throw std::invalid_argument(error);
    std::vector<std::uint8_t> b;b.reserve(MapDescriptorSize);
    const auto u16=[&](auto x){b.push_back(static_cast<std::uint8_t>(x>>8));b.push_back(static_cast<std::uint8_t>(x));};
    const auto f=[&](float x){auto v=std::bit_cast<std::uint32_t>(x);for(int s=24;s>=0;s-=8)b.push_back(static_cast<std::uint8_t>(v>>s));};
    const auto v=[&](Vec3 x){f(x.x);f(x.y);f(x.z);};
    u16(static_cast<std::uint16_t>(m.kind));u16(m.geometryVersion);
    b.insert(b.end(),m.content.begin(),m.content.end());b.insert(b.end(),m.collision.begin(),m.collision.end());
    f(m.aspect);f(m.height);f(m.centerY);f(m.scale);v(m.spawn);f(m.spawnYaw);v(m.desktopCenter);v(m.desktopNormal);f(m.desktopHeight);
    return b;
}
bool decodeMap(std::span<const std::uint8_t> b,MapDescriptor& out,std::string& error) {
    error.clear();if(b.size()!=MapDescriptorSize){error="wrong map descriptor size";return false;}
    std::size_t i=0;
    const auto u16=[&](){const auto v=static_cast<std::uint16_t>((b[i]<<8)|b[i+1]);i+=2;return v;};
    const auto f=[&](){std::uint32_t v=0;for(unsigned j=0;j<4;++j)v=(v<<8)|b[i++];return std::bit_cast<float>(v);};
    const auto v=[&](){Vec3 x;x.x=f();x.y=f();x.z=f();return x;};
    MapDescriptor m;m.kind=static_cast<MapKind>(u16());m.geometryVersion=u16();
    std::copy_n(b.begin()+i,32,m.content.begin());i+=32;std::copy_n(b.begin()+i,32,m.collision.begin());i+=32;
    m.aspect=f();m.height=f();m.centerY=f();m.scale=f();m.spawn=v();m.spawnYaw=f();m.desktopCenter=v();m.desktopNormal=v();m.desktopHeight=f();
    if(!validMap(m,error))return false;
    out=m;return true;
}
} // namespace hyprwalk::mp
