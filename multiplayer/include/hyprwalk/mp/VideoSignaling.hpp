#pragma once
#include "VideoTypes.hpp"
#include <optional>
#include <span>

namespace hyprwalk::mp {
inline constexpr std::uint16_t VideoSignalingVersion = 1;
inline constexpr std::size_t MaxVideoSignal = 49152, VideoChunkBytes = 3072;
inline constexpr std::size_t MaxVideoSignalQueue = 64, MaxVideoSignalQueueBytes = 262144;
enum class VideoSignalKind : std::uint8_t { Offer = 1, Answer, Ice, Stop, Decoded, Failure };
struct VideoSignal {
    VideoTrackKey key;
    Id sender{};
    std::uint64_t serial = 0; // assigned by the control channel, strictly increases per direction
    VideoSignalKind kind = VideoSignalKind::Offer;
    std::string body;
};
struct VideoChunk {
    VideoSignal signal;
    std::uint16_t index = 0, count = 0;
    std::uint32_t total = 0;
};
bool validVideoSignal(const VideoSignal&, std::string& error);
bool videoScope(const VideoSignal&, const Status&, bool incoming) noexcept;
std::vector<std::vector<std::uint8_t>> fragmentVideoSignal(const VideoSignal&);
bool decodeVideoChunk(std::span<const std::uint8_t>, VideoChunk&, std::string& error);
// TCP preserves order: at most one incomplete logical message, no parallel allocation
// per advertised ID. Metadata/TCP heartbeat can be interleaved, signal chunks cannot.
class VideoAssembler {
  public:
    using Clock = std::chrono::steady_clock;
    std::optional<VideoSignal> accept(std::span<const std::uint8_t>, Clock::time_point, std::string& error);
    bool expire(Clock::time_point) noexcept;
    void reset() noexcept;
    std::size_t buffered() const noexcept { return m_body.size(); }
  private:
    VideoChunk m_start;
    std::string m_body;
    std::uint16_t m_next = 0;
    std::uint64_t m_last = 0;
    Clock::time_point m_deadline{};
};
} // namespace hyprwalk::mp
