#pragma once
#ifdef HYPRWALK_MULTIPLAYER
#include <hyprwalk/mp/WindowSync.hpp>
#include <map>
#ifdef HYPRWALK_WEBRTC
#include <hyprwalk/mp/VideoTypes.hpp>
#endif
#include <memory>
namespace hyprwalk {
struct SRemoteWindowImage { int width=0,height=0; std::uint64_t serial=0; std::vector<std::uint8_t> rgba; };
struct SRemoteWindowFrame {
    mp::WindowKey key;
    mp::WindowGeometry geometry;
    std::shared_ptr<const SRemoteWindowImage> panel;
#ifdef HYPRWALK_WEBRTC
    std::shared_ptr<const mp::VideoFrame> video = nullptr;
#endif
};
class CRemoteWindowView {
  public:
    void update(const mp::WindowSync&, std::string_view owner, std::uint64_t hovered);
    void clear() { m_cache.clear(); m_frames.clear(); }
    std::vector<SRemoteWindowFrame>& frames() { return m_frames; }
    const std::vector<SRemoteWindowFrame>& frames() const { return m_frames; }
  private:
    struct Cache {
        std::string title, app, owner, video;
        std::shared_ptr<SRemoteWindowImage> panel;
    };
    std::uint64_t m_serial=0;
    std::map<mp::WindowKey,Cache> m_cache;
    std::vector<SRemoteWindowFrame> m_frames;
};
} // namespace hyprwalk
#endif
