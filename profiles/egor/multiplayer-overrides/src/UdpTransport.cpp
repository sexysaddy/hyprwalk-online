#include <hyprwalk/mp/UdpTransport.hpp>
#include <hyprwalk/mp/Protocol.hpp>
#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace hyprwalk::mp {
namespace {
using Clock = UdpTransport::Clock;
using Time = UdpTransport::Time;
constexpr auto ProbeInterval = std::chrono::milliseconds(100);
struct Address { sockaddr_storage storage{}; socklen_t size = 0; int family = AF_UNSPEC; };
Address address(const Endpoint& ep) {
    if (ep.ip.empty() || ep.ip.size() >= INET6_ADDRSTRLEN || ep.ip.find('\0') != std::string::npos)
        throw std::invalid_argument("invalid UDP numeric address");
    Address a;
    auto* v4 = reinterpret_cast<sockaddr_in*>(&a.storage);
    if (::inet_pton(AF_INET, ep.ip.c_str(), &v4->sin_addr) == 1) {
        a.family = v4->sin_family = AF_INET; v4->sin_port = htons(ep.port); a.size = sizeof(*v4);
    } else {
        auto* v6 = reinterpret_cast<sockaddr_in6*>(&a.storage);
        if (::inet_pton(AF_INET6, ep.ip.c_str(), &v6->sin6_addr) != 1)
            throw std::invalid_argument("numeric IPv4/IPv6 required for UDP");
        a.family = v6->sin6_family = AF_INET6; v6->sin6_port = htons(ep.port); a.size = sizeof(*v6);
    }
    return a;
}
bool sameEndpoint(const Address& a, const Address& b) noexcept {
    if (a.storage.ss_family != b.storage.ss_family) return false;
    if (a.storage.ss_family == AF_INET) {
        const auto* x = reinterpret_cast<const sockaddr_in*>(&a.storage);
        const auto* y = reinterpret_cast<const sockaddr_in*>(&b.storage);
        return x->sin_addr.s_addr == y->sin_addr.s_addr && x->sin_port == y->sin_port;
    }
    const auto* x = reinterpret_cast<const sockaddr_in6*>(&a.storage);
    const auto* y = reinterpret_cast<const sockaddr_in6*>(&b.storage);
    return x->sin6_port == y->sin6_port && x->sin6_scope_id == y->sin6_scope_id &&
           std::memcmp(&x->sin6_addr, &y->sin6_addr, sizeof(in6_addr)) == 0;
}
std::string systemError(const char* operation) { return std::string(operation) + ": " + std::strerror(errno); }
bool transient(int error) {
    return error == EAGAIN || error == EWOULDBLOCK || error == ENOBUFS || error == EINTR ||
           error == ECONNREFUSED || error == EHOSTUNREACH || error == ENETUNREACH;
}
}
const char* udpStateName(UdpState state) noexcept {
    switch (state) {
        case UdpState::Disabled: return "disabled";
        case UdpState::WaitingOffer: return "waiting_offer";
        case UdpState::Probing: return "probing";
        case UdpState::Ready: return "ready";
        case UdpState::TimedOut: return "timed_out";
        case UdpState::Error: return "error";
    }
    return "invalid";
}
struct UdpTransport::Impl {
    Settings settings;
    UdpStatus s;
    int socket = -1;
    Address peerAddress;
    Id session{}, local{}, peer{}, receiveToken{}, sendToken{};
    Time started{}, phaseDeadline{}, lastRx{}, nextProbe{}, nextFrame{}, lastTx{};
    std::uint32_t nextSequence = 0;
    udpwire::SequenceWindow window;
    std::deque<std::uint32_t> probes;
    explicit Impl(Settings st) : settings(st) {}
    ~Impl() { close(); }
    void close() noexcept { if (socket >= 0) ::close(socket); socket = -1; }
    void fail(UdpState state, const std::string& reason) { s.state = state; s.error = reason; close(); }
    bool send(udpwire::Type type, Time now, const PlayerSnapshot* player = nullptr, std::uint32_t ack = 0) {
        udpwire::Packet p; p.type = type; p.session = session; p.sender = local; p.target = peer; p.token = sendToken;
        p.sequence = nextSequence++; p.acknowledgement = ack;
        p.senderTimeUs = static_cast<std::uint64_t>(std::max<std::int64_t>(0,
            std::chrono::duration_cast<std::chrono::microseconds>(now - started).count()));
        if (player) p.player = *player;
        const auto bytes = udpwire::encode(p);
        ssize_t n = -1;
        for (unsigned attempt = 0; attempt < 4; ++attempt) {
            n = ::sendto(socket, bytes.data(), bytes.size(), MSG_NOSIGNAL,
                         reinterpret_cast<const sockaddr*>(&peerAddress.storage), peerAddress.size);
            if (n >= 0 || errno != EINTR) break;
        }
        if (n < 0 && transient(errno)) { ++s.sendDropped; return false; }
        if (n < 0) { fail(UdpState::Error, systemError("UDP sendto")); return false; }
        if (static_cast<std::size_t>(n) != bytes.size()) { fail(UdpState::Error, "short UDP datagram write"); return false; }
        ++s.sent; lastTx = now;
        if (type == udpwire::Type::Player) ++s.playerSent;
        if (type == udpwire::Type::Probe) {
            if (probes.size() == 64) probes.pop_front();
            probes.push_back(p.sequence);
        }
        return true;
    }
};
UdpTransport::UdpTransport(Settings settings) : m_impl(std::make_unique<Impl>(settings)) {
    for (auto t : {settings.udpHandshakeTimeout, settings.udpPeerTimeout, settings.udpHeartbeatInterval,
                   settings.udpPlayerInterval})
        if (t.count() <= 0 || t > std::chrono::hours(1)) throw std::invalid_argument("invalid UDP timeout");
    if (settings.udpHeartbeatInterval >= settings.udpPeerTimeout)
        throw std::invalid_argument("UDP heartbeat must be shorter than peer timeout");
    if (settings.udpPlayerInterval >= settings.udpPeerTimeout)
        throw std::invalid_argument("UDP player interval must be shorter than peer timeout");
}
UdpTransport::~UdpTransport() = default;
void UdpTransport::stop() noexcept {
    auto& i = *m_impl;
    i.close(); i.s = {}; i.probes.clear(); i.window.reset();
    i.session = i.local = i.peer = i.receiveToken = i.sendToken = {};
    i.nextSequence = 0;
}
bool UdpTransport::start(const Endpoint& bind, const Id& session, const Id& local, const Id& peer,
                         const Id& receiveToken, Time now, std::string& error) {
    stop(); error.clear(); auto& i = *m_impl;
    try {
        if (!wire::nonzero(session) || !wire::nonzero(local) || !wire::nonzero(peer) || !wire::nonzero(receiveToken) || local == peer)
            throw std::invalid_argument("invalid UDP session identifiers");
        auto a = address(bind);
        i.socket = ::socket(a.family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (i.socket < 0) throw std::runtime_error(systemError("UDP socket"));
        const int one = 1, buffer = 65536;
        if (a.family == AF_INET6 && ::setsockopt(i.socket, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one)) != 0)
            throw std::runtime_error(systemError("UDP IPV6_V6ONLY"));
        if (::setsockopt(i.socket, SOL_SOCKET, SO_RCVBUF, &buffer, sizeof(buffer)) != 0 ||
            ::setsockopt(i.socket, SOL_SOCKET, SO_SNDBUF, &buffer, sizeof(buffer)) != 0)
            throw std::runtime_error(systemError("UDP socket buffers"));
        // Deliberately no SO_REUSEPORT / SO_REUSEADDR: another session must not steal packets.
        if (::bind(i.socket, reinterpret_cast<const sockaddr*>(&a.storage), a.size) != 0)
            throw std::runtime_error(systemError("UDP bind"));
        if (::getsockname(i.socket, reinterpret_cast<sockaddr*>(&a.storage), &a.size) != 0)
            throw std::runtime_error(systemError("UDP getsockname"));
        i.s.local = bind;
        i.s.local.port = a.family == AF_INET ? ntohs(reinterpret_cast<sockaddr_in*>(&a.storage)->sin_port)
                                            : ntohs(reinterpret_cast<sockaddr_in6*>(&a.storage)->sin6_port);
        i.session = session; i.local = local; i.peer = peer; i.receiveToken = receiveToken;
        i.started = i.lastRx = i.lastTx = now; i.phaseDeadline = now + i.settings.udpHandshakeTimeout;
        i.s.state = UdpState::WaitingOffer; return true;
    } catch (const std::exception& e) { error = e.what(); i.fail(UdpState::Error, error); return false; }
}
bool UdpTransport::setPeer(const Endpoint& endpoint, const Id& sendToken, Time now, std::string& error) {
    auto& i = *m_impl; error.clear();
    try {
        if (i.s.state != UdpState::WaitingOffer) throw std::invalid_argument("unexpected or duplicate UDP offer");
        if (!endpoint.port || !wire::nonzero(sendToken)) throw std::invalid_argument("invalid UDP offer port/token");
        auto a = address(endpoint);
        if (a.family != address(i.s.local).family) throw std::invalid_argument("UDP address family mismatch");
        i.peerAddress = a; i.s.peer = endpoint; i.sendToken = sendToken;
        i.nextProbe = now; i.s.state = UdpState::Probing;
        return true;
    } catch (const std::exception& e) { error = e.what(); i.fail(UdpState::Error, error); return false; }
}
void UdpTransport::tick(Time now, const std::optional<PlayerSnapshot>& local) {
    auto& i = *m_impl;
    if (i.socket < 0) return;
    if (i.s.state != UdpState::Ready && now >= i.phaseDeadline) {
        i.fail(UdpState::TimedOut, "UDP negotiation timeout; TCP remains connected; check UDP firewall and rejoin"); return;
    }
    if (i.s.state == UdpState::Probing && now >= i.nextProbe) {
        i.send(udpwire::Type::Probe, now); i.nextProbe = now + ProbeInterval;
    }
    if (i.s.state != UdpState::Ready) return;
    if (now - i.lastRx >= i.settings.udpPeerTimeout) {
        i.fail(UdpState::TimedOut, "UDP receive timeout; TCP remains connected; leave/join to recover"); return;
    }
    if (now < i.nextFrame) return;
    // Skip missed deadlines, never catch up in a burst. Tick cadence is independent of render FPS.
    i.nextFrame += i.settings.udpPlayerInterval * ((now - i.nextFrame) / i.settings.udpPlayerInterval + 1);
    if (local && validPlayerSnapshot(*local)) i.send(udpwire::Type::Player, now, &*local);
    else if (now - i.lastTx >= i.settings.udpHeartbeatInterval) i.send(udpwire::Type::Heartbeat, now);
}
void UdpTransport::receive(Time now, std::vector<ReceivedPlayer>& received) {
    auto& i = *m_impl;
    if (i.socket < 0) return;
    for (unsigned count = 0; count < 64 && i.socket >= 0; ++count) {
        std::array<std::uint8_t, udpwire::MaxDatagram> bytes{};
        Address from; from.size = sizeof(from.storage);
        const auto n = ::recvfrom(i.socket, bytes.data(), bytes.size(), MSG_TRUNC,
                                   reinterpret_cast<sockaddr*>(&from.storage), &from.size);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (transient(errno)) return;
            i.fail(UdpState::Error, systemError("UDP recvfrom")); return;
        }
        if (i.s.state == UdpState::WaitingOffer || !sameEndpoint(from, i.peerAddress)) { ++i.s.wrongPeer; continue; }
        if (static_cast<std::size_t>(n) > bytes.size()) { ++i.s.invalid; continue; }
        udpwire::Packet p; std::string error;
        if (!udpwire::decode({bytes.data(), static_cast<std::size_t>(n)}, p, error)) { ++i.s.invalid; continue; }
        if (p.session != i.session || p.sender != i.peer || p.target != i.local || p.token != i.receiveToken) {
            ++i.s.wrongPeer; continue;
        }
        if (p.type == udpwire::Type::ProbeAck && std::find(i.probes.begin(), i.probes.end(), p.acknowledgement) == i.probes.end()) {
            ++i.s.invalid; continue;
        }
        if ((p.type == udpwire::Type::Heartbeat || p.type == udpwire::Type::Player) && i.s.state != UdpState::Ready) {
            // A peer can become Ready before we receive its ACK. Do not advance the receive window yet.
            continue;
        }
        const auto result = i.window.accept(p.sequence);
        if (result == udpwire::SequenceWindow::Result::Duplicate) { ++i.s.duplicates; continue; }
        if (result == udpwire::SequenceWindow::Result::TooOld) { ++i.s.tooOld; continue; }
        if (result == udpwire::SequenceWindow::Result::Reordered) ++i.s.reordered;
        ++i.s.received; i.s.gapsObserved = i.window.gapsObserved(); i.lastRx = now;
        if (p.type == udpwire::Type::Probe) i.send(udpwire::Type::ProbeAck, now, nullptr, p.sequence);
        if (p.type == udpwire::Type::ProbeAck && i.s.state == UdpState::Probing) {
            i.s.state = UdpState::Ready; i.nextFrame = now;
        }
        if (p.type == udpwire::Type::Player && i.s.state == UdpState::Ready) {
            ++i.s.playerReceived;
            received.push_back({p.player, p.sequence, p.senderTimeUs, now});
        }
    }
}
std::optional<UdpTransport::Time> UdpTransport::deadline() const noexcept {
    const auto& i = *m_impl;
    if (i.socket < 0) return {};
    if (i.s.state == UdpState::WaitingOffer) return i.phaseDeadline;
    if (i.s.state == UdpState::Probing) return std::min(i.phaseDeadline, i.nextProbe);
    if (i.s.state == UdpState::Ready) return std::min(i.nextFrame, i.lastRx + i.settings.udpPeerTimeout);
    return {};
}
int UdpTransport::fd() const noexcept { return m_impl->socket; }
const UdpStatus& UdpTransport::status() const noexcept { return m_impl->s; }
} // namespace hyprwalk::mp
