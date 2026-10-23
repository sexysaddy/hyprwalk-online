#pragma once
#include "UdpProtocol.hpp"
#include <memory>
#include <optional>

namespace hyprwalk::mp {
// Owned and called ONLY by the existing network worker. No thread of its own,
// no TCP implementation, no callbacks into a plugin. All syscalls are nonblocking.
class UdpTransport {
  public:
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;
    explicit UdpTransport(Settings settings = {});
    ~UdpTransport();
    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    bool start(const Endpoint& bind, const Id& session, const Id& local, const Id& peer,
               const Id& receiveToken, Time now, std::string& error);
    // IP MUST come from getpeername(TCP), only the port/token come from the peer offer.
    bool setPeer(const Endpoint& endpoint, const Id& sendToken, Time now, std::string& error);
    void tick(Time now, const std::optional<PlayerSnapshot>& local);
    void receive(Time now, std::vector<ReceivedPlayer>& received); // At most 64 datagrams/call.
    std::optional<Time> deadline() const noexcept;
    void stop() noexcept;
    int fd() const noexcept;
    const UdpStatus& status() const noexcept;
  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace hyprwalk::mp
