#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace hyprwalk::mp {
using Id = std::array<std::uint8_t, 16>;
inline constexpr std::uint16_t ProtocolVersion = 1;
inline constexpr std::uint32_t ControlCapability = 1U;
inline constexpr std::uint32_t UdpCapability = 2U;
inline constexpr std::uint32_t WorldCapability = 4U;
inline constexpr std::uint32_t WindowsCapability = 8U;

enum class State { Off, Starting, Listening, Connecting, Handshaking, Connected, Stopping, Error };
enum class Role { None, Host, Client };
enum class Level { Info, Warning, Error };

struct LocalInfo {
    // Protocol v1 names are printable ASCII, 1..64 bytes. Not an authentication identity.
    std::string name;
};
struct Endpoint {
    std::string ip; // Numeric IPv4 or IPv6, without brackets; never resolved through DNS.
    std::uint16_t port = 0; // Host may use 0 to request an ephemeral port.
};
enum class UdpState { Disabled, WaitingOffer, Probing, Ready, TimedOut, Error };
struct UdpStatus {
    UdpState state = UdpState::Disabled;
    Endpoint local, peer;
    std::uint64_t sent = 0, received = 0, playerSent = 0, playerReceived = 0;
    std::uint64_t invalid = 0, wrongPeer = 0, duplicates = 0, reordered = 0, tooOld = 0, gapsObserved = 0;
    std::uint64_t sendDropped = 0, receiveDropped = 0;
    std::string error;
};
const char* udpStateName(UdpState state) noexcept;
enum class WorldState { Disabled, WaitingOffer, Applying, WaitingPeer, Ready, Rejected };
const char* worldStateName(WorldState state) noexcept;
struct Settings {
    bool enableVideo = false; // Only advertise after a successful runtime backend probe.

    bool enableWindows = false; // Optional metadata only; requires world admission.
    std::chrono::milliseconds windowLease{3000};
    bool enableWorld = false;
    std::chrono::milliseconds worldTimeout{60000};
    bool enableUdp = false; // Explicit opt-in; preserves the stage-1 default control-only behavior.
    std::chrono::milliseconds udpHandshakeTimeout{5000};
    std::chrono::milliseconds udpPeerTimeout{3000};
    std::chrono::milliseconds udpHeartbeatInterval{500};
    std::chrono::milliseconds connectTimeout{5000};
    std::chrono::milliseconds handshakeTimeout{5000};
    std::chrono::milliseconds frameTimeout{5000};
    std::chrono::milliseconds writeTimeout{5000};
    std::chrono::milliseconds heartbeatInterval{3000};
    std::chrono::milliseconds peerTimeout{15000};
    std::chrono::milliseconds leaveTimeout{100};
};
struct Status {
    std::uint64_t videoSignalsSent = 0, videoSignalsReceived = 0;
    std::string videoError; // Media signaling fails independently of TCP/UDP/world.

    std::uint64_t windowsSent = 0, windowsReceived = 0;
    std::size_t publishedWindows = 0, remoteWindows = 0;
    WorldState world = WorldState::Disabled;
    std::string worldError;
    Id worldTicket{};
    UdpStatus udp;
    State state = State::Off;
    Role role = Role::None;
    Endpoint endpoint;
    Id session{};
    Id localId{};
    Id peerId{};
    std::string localName;
    std::string peerName;
    std::uint16_t version = 0;
    std::uint32_t capabilities = 0;
    std::uint64_t testsSent = 0;
    std::uint64_t testsReceived = 0;
    std::uint64_t testsAcknowledged = 0;
    std::uint64_t droppedEvents = 0;
    std::string lastMessage;
    std::string lastError;
    bool workerRunning = false;
};
struct Diagnostic {
    Level level = Level::Info;
    std::string text;
};

const char* stateName(State state) noexcept;
const char* roleName(Role role) noexcept;
std::string idString(const Id& id);
std::string formatStatus(const Status& status); // JSON, including escaped diagnostic strings.
} // namespace hyprwalk::mp
