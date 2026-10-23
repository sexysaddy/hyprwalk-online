#pragma once
#include "Types.hpp"
#include "MapSync.hpp"
#include "WindowTypes.hpp"
#include <deque>
#include <functional>
#include <span>
#include <sys/types.h>
#include <vector>

namespace hyprwalk::mp::wire {
inline constexpr std::size_t HeaderSize = 16;
inline constexpr std::size_t MaxPayload = 4096;
inline constexpr std::size_t MaxText = 1024;
inline constexpr std::size_t MaxQueuedBytes = 65536;
enum class Type : std::uint16_t {
    Hello = 1, Welcome, Ready, ReadyAck, Test, TestAck, Ping, Pong, Bye, Error, UdpOffer, MapOffer, MapReady, MapReject, Windows, Video
};
struct Frame {
    Type type{};
    std::vector<std::uint8_t> payload;
};
struct Message {
    Type type{};
    std::uint16_t minVersion = ProtocolVersion, maxVersion = ProtocolVersion;
    std::uint16_t selectedVersion = ProtocolVersion;
    std::uint32_t capabilities = ControlCapability;
    Id session{}, participant{}, echo{}, udpToken{}, worldTicket{};
    MapDescriptor map;
    WindowSnapshot windows;
    std::vector<std::uint8_t> videoChunk; // Checked by the media signaling codec, not metadata.
    std::uint16_t udpPort = 0, udpVersion = 1;
    std::string text;
    std::uint64_t sequence = 0;
};
bool printable(const std::string& text, std::size_t maxLength) noexcept;
bool nonzero(const Id& id) noexcept;
std::vector<std::uint8_t> encode(const Message& message); // Throws on invalid local input.
bool decode(const Frame& frame, Message& message, std::string& error);

// Incremental bounded framing. Rejects invalid headers BEFORE allocating the payload.
// The visitor may return false to stop processing (e.g. BYE). error is empty in that case.
class Decoder {
  public:
    using Visitor = std::function<bool(Frame&&)>;
    bool feed(std::span<const std::uint8_t> bytes, const Visitor& visitor, std::string& error);
    bool partial() const noexcept { return !m_buffer.empty(); }
    std::uint64_t completed() const noexcept { return m_completed; }
  private:
    std::vector<std::uint8_t> m_buffer;
    std::size_t m_expected = HeaderSize;
    Type m_type{};
    bool m_failed = false;
    std::uint64_t m_completed = 0;
};

// The same short-write logic is used by the TCP worker and deterministic fault tests.
// Send follows send(2): return byte count or -1 with errno; no callback is retained.
class WriteQueue {
  public:
    using Send = std::function<ssize_t(const std::uint8_t*, std::size_t)>;
    enum class Result { Drained, Pending, Failed };
    bool push(std::vector<std::uint8_t> frame);
    // Cannot retract a partially written frame, but never retain unsent revoked metadata.
    void discardUnsent(Type type);
    Result flush(const Send& send, std::string& error, std::size_t budget = MaxQueuedBytes);
    std::size_t bytes() const noexcept { return m_bytes; }
    bool empty() const noexcept { return m_frames.empty(); }
  private:
    std::deque<std::vector<std::uint8_t>> m_frames;
    std::size_t m_offset = 0, m_bytes = 0;
};
} // namespace hyprwalk::mp::wire
