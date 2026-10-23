#pragma once
#include "UdpProtocol.hpp"
#include <array>
#include <optional>

namespace hyprwalk::mp {
struct InterpolationSettings {
    std::chrono::milliseconds delay{100};
    std::chrono::milliseconds extrapolationLimit{100};
    std::chrono::milliseconds staleAfter{500};
    float teleportDistance = 8.f;
};
struct SampledPlayer {
    PlayerSnapshot player;
    bool extrapolated = false, frozen = false;
};
// Main-thread-owned, fixed-capacity interpolation. No locks, allocation, networking or graphics.
// Reset whenever TCP session/peer changes or UDP leaves Ready. Do not reuse across sessions.
class PlayerSync {
  public:
    using Time = std::chrono::steady_clock::time_point;
    static constexpr std::size_t Capacity = 64;
    explicit PlayerSync(InterpolationSettings settings = {});
    bool push(const ReceivedPlayer& sample) noexcept;
    std::optional<SampledPlayer> sample(Time now) const noexcept;
    void reset() noexcept;
    std::size_t size() const noexcept { return m_size; }
    std::uint64_t revision() const noexcept { return m_revision; }
  private:
    InterpolationSettings m_settings;
    std::array<ReceivedPlayer, Capacity> m_samples{};
    std::array<double, Capacity> m_offsets{};
    std::size_t m_size = 0, m_offsetCount = 0, m_offsetNext = 0;
    udpwire::SequenceWindow m_sequences;
    double m_clockOffsetUs = 0;
    std::uint64_t m_revision = 0;
    Time m_lastArrival{};
};
} // namespace hyprwalk::mp
