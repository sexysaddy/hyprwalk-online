#pragma once
#include "PlayerSnapshot.hpp"
#include <span>
#include <vector>

namespace hyprwalk::mp::udpwire {
inline constexpr std::uint16_t Version = 1;
inline constexpr std::size_t HeaderSize = 88, PlayerSize = 44, MaxDatagram = HeaderSize + PlayerSize;
enum class Type : std::uint16_t { Probe = 1, ProbeAck = 2, Heartbeat = 3, Player = 4 };
struct Packet {
    Type type = Type::Heartbeat;
    Id session{}, sender{}, target{}, token{};
    std::uint32_t sequence = 0, acknowledgement = 0;
    std::uint64_t senderTimeUs = 0;
    PlayerSnapshot player;
};
std::vector<std::uint8_t> encode(const Packet& packet);
bool decode(std::span<const std::uint8_t> bytes, Packet& packet, std::string& error);

// RFC 1982 serial arithmetic. The ambiguous exact half-range is never newer.
bool newer(std::uint32_t a, std::uint32_t b) noexcept;
class SequenceWindow {
  public:
    enum class Result { Newest, Reordered, Duplicate, TooOld };
    Result accept(std::uint32_t sequence) noexcept;
    void reset() noexcept { *this = {}; }
    std::uint64_t gapsObserved() const noexcept { return m_gaps; }
  private:
    bool m_initialized = false;
    std::uint32_t m_latest = 0;
    std::uint64_t m_seen = 0, m_gaps = 0;
};
} // namespace hyprwalk::mp::udpwire
