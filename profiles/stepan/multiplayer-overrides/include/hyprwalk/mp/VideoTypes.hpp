#pragma once
#include "WindowTypes.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace hyprwalk::mp {
inline constexpr std::uint32_t VideoCapability = 0x10;
inline constexpr std::size_t MaxVideoStreams = MaxSharedWindows; // one stream per shared-window slot
struct VideoTrackKey {
    WindowKey window;
    std::uint64_t publication = 0; // consent incarnation, not a metadata snapshot revision
    std::uint64_t stream = 0; // new for every encoder/ICE restart within that incarnation
    auto operator<=>(const VideoTrackKey&) const = default;
};
struct VideoSettings {
    // fps=0 follows the published surface's real commit cadence (variable framerate).
    // 1..30 is an optional explicit cap. Bitrate is the per-source VP8 ceiling at 1920x1080
    // and above; dimensions stay native unless they exceed the 8192x8192 allocation ceiling.
    unsigned width = 8192, height = 8192, fps = 0, bitrate = 2500000, streams = static_cast<unsigned>(MaxVideoStreams);
    bool operator==(const VideoSettings&) const = default;
};
// Treat bitrate as the per-stream ceiling at 1920x1080 and above. Smaller windows
// receive a proportional target (square-root pixel-area scaling) so a 1200x500 UI
// does not reserve the same network budget as a full-HD window. This never rescales
// the source image; it changes only VP8's rate-control target.
inline unsigned targetVideoBitrate(const VideoSettings& settings,unsigned width,unsigned height) noexcept {
    if(!width||!height||!settings.bitrate)return settings.bitrate;
    constexpr double referencePixels=1920.0*1080.0;
    const double scale=std::sqrt((static_cast<double>(width)*static_cast<double>(height))/referencePixels);
    const auto scaled=static_cast<unsigned long long>(static_cast<double>(settings.bitrate)*std::min(1.0,scale));
    const auto floor=std::min(settings.bitrate,300000U);
    return static_cast<unsigned>(std::clamp(scaled,static_cast<unsigned long long>(floor),static_cast<unsigned long long>(settings.bitrate)));
}
bool validVideoSettings(const VideoSettings&) noexcept;
std::pair<unsigned,unsigned> fitVideoSize(unsigned width, unsigned height, const VideoSettings&) noexcept;
// Tightly packed RGBA, row zero is the TOP of the application image. Alpha is opaque
// on receive (VP8 is not an alpha codec); premultiplied input composites over black.
struct VideoFrame {
    VideoTrackKey key;
    unsigned width = 0, height = 0;
    std::uint64_t serial = 0;
    std::chrono::steady_clock::time_point time{};
    std::vector<std::uint8_t> rgba;
};
bool validVideoFrame(const VideoFrame&) noexcept;
struct VideoPublication {
    WindowKey window;
    std::uint64_t publication = 0;
    unsigned width = 0, height = 0;
    bool operator==(const VideoPublication&) const = default;
};
struct VideoContext {
    Id session{}, local{}, peer{}, world{};
    bool enabled = false;
    std::vector<VideoPublication> sending;
    std::vector<WindowKey> receiving;
    bool operator==(const VideoContext&) const = default;
};
struct VideoStreamStatus {
    VideoTrackKey key;
    bool sending = false, connected = false, decoded = false;
    std::uint64_t frames = 0, dropped = 0;
    unsigned width = 0, height = 0;
    double fps = 0;
    std::string state = "Connecting", detail, ice;
};
} // namespace hyprwalk::mp
