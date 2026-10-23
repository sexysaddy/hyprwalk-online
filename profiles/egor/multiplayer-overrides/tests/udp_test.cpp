#include <hyprwalk/mp/Multiplayer.hpp>
#include <hyprwalk/mp/Protocol.hpp>
#include <hyprwalk/mp/UdpTransport.hpp>
#include <algorithm>
#include <arpa/inet.h>
#include <bit>
#include <cerrno>
#include <cmath>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace hyprwalk::mp;
using namespace std::chrono_literals;
namespace {
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + ": " #x); } while (0)
using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;
Id id(std::uint8_t byte) { Id i{}; i[0] = byte; return i; }
struct Fd { int n = -1; explicit Fd(int v) : n(v) { CHECK(n >= 0); } ~Fd() { ::close(n); } Fd(const Fd&) = delete; };
std::size_t count(const char* path) { return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator(path), {})); }
struct Adapter : IAdapter {
    std::string name;
    std::thread::id thread = std::this_thread::get_id();
    bool wrongThread = false;
    std::vector<ReceivedPlayer> players;
    explicit Adapter(std::string n) : name(std::move(n)) {}
    LocalInfo localInfo() const override { return {name}; }
    void onState(const Status&) noexcept override { wrongThread |= thread != std::this_thread::get_id(); }
    void onDiagnostic(const Diagnostic&) noexcept override { wrongThread |= thread != std::this_thread::get_id(); }
    void onPlayerSnapshot(const ReceivedPlayer& p) noexcept override { wrongThread |= thread != std::this_thread::get_id(); players.push_back(p); }
};
Settings settings() {
    Settings s; s.enableUdp = true; s.connectTimeout = s.handshakeTimeout = 1500ms;
    s.heartbeatInterval = 200ms; s.peerTimeout = 3000ms;
    s.udpHandshakeTimeout = 800ms; s.udpPeerTimeout = 600ms; s.udpHeartbeatInterval = 100ms;
    return s;
}
void wait(const std::function<bool()>& fn, std::initializer_list<Multiplayer*> cores = {}, std::chrono::milliseconds limit = 3000ms) {
    const auto end = Clock::now() + limit;
    while (!fn()) {
        if (Clock::now() >= end) {
            for (auto* c : cores) std::cerr << formatStatus(c->status()) << '\n';
            throw std::runtime_error("wait timed out");
        }
        for (auto* c : cores) c->dispatch();
        std::this_thread::sleep_for(1ms);
    }
    for (auto* c : cores) c->dispatch();
}
std::uint16_t host(Multiplayer& h, std::string ip = "127.0.0.1") {
    std::string error; CHECK(h.host({ip, 0}, error));
    wait([&] { return h.status().state == State::Listening; }, {&h});
    return h.status().endpoint.port;
}
void pair(Multiplayer& h, Multiplayer& c, std::string ip = "127.0.0.1") {
    const auto port = host(h, ip); std::string error; CHECK(c.join({ip, port}, error));
    wait([&] { return h.status().udp.state == UdpState::Ready && c.status().udp.state == UdpState::Ready &&
                      h.status().testsAcknowledged == 1 && c.status().testsAcknowledged == 1; }, {&h, &c});
}
PlayerSnapshot player(float x = 1) {
    PlayerSnapshot p; p.active = true; p.feet = {x, 2, -3}; p.velocity = {4, 5, 6};
    p.yaw = 0.5f; p.pitch = -0.25f; p.bodyYaw = -1.f; p.running = p.crouched = p.flying = p.onGround = true;
    p.teleport = 0xabcdef01; return p;
}
struct DatagramSocket {
    Fd socket{::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)};
    std::uint16_t port = 0;
    DatagramSocket() {
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        CHECK(::bind(socket.n, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0);
        socklen_t n = sizeof(a); CHECK(::getsockname(socket.n, reinterpret_cast<sockaddr*>(&a), &n) == 0); port = ntohs(a.sin_port);
    }
    void send(const std::vector<std::uint8_t>& bytes, std::uint16_t target) {
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(target);
        CHECK(::sendto(socket.n, bytes.data(), bytes.size(), 0, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == static_cast<ssize_t>(bytes.size()));
    }
    std::vector<udpwire::Packet> receive() {
        std::vector<udpwire::Packet> result;
        for (int i = 0; i < 128; ++i) {
            std::uint8_t bytes[1024]; const auto n = ::recv(socket.n, bytes, sizeof(bytes), 0);
            if (n < 0) { CHECK(errno == EAGAIN || errno == EWOULDBLOCK); break; }
            udpwire::Packet p; std::string error; CHECK(udpwire::decode({bytes, static_cast<std::size_t>(n)}, p, error)); result.push_back(p);
        }
        return result;
    }
};
struct Fixture {
    UdpTransport udp{settings()};
    DatagramSocket raw;
    Time now = Clock::now();
    std::vector<ReceivedPlayer> received;
    Fixture() { start(); }
    void start() {
        std::string e;
        CHECK(udp.start({"127.0.0.1", 0}, id(1), id(2), id(3), id(4), now, e));
        CHECK(udp.setPeer({"127.0.0.1", raw.port}, id(5), now, e));
    }
    udpwire::Packet packet(udpwire::Type type, std::uint32_t seq) {
        udpwire::Packet p; p.type = type; p.sequence = seq; p.session = id(1); p.sender = id(3); p.target = id(2); p.token = id(4); p.player = player();
        return p;
    }
    void send(udpwire::Packet p) { raw.send(udpwire::encode(p), udp.status().local.port); udp.receive(now, received); }
    void ready() {
        udp.tick(now, {}); const auto sent = raw.receive(); CHECK(sent.size() == 1 && sent[0].type == udpwire::Type::Probe);
        auto ack = packet(udpwire::Type::ProbeAck, 0); ack.acknowledgement = sent[0].sequence; send(ack);
        CHECK(udp.status().state == UdpState::Ready);
    }
};
void codecs() {
    udpwire::Packet p; p.session = id(1); p.sender = id(2); p.target = id(3); p.token = id(4);
    p.type = udpwire::Type::Player; p.sequence = 0xffffffff; p.senderTimeUs = 0x0102030405060708ULL; p.player = player();
    auto bytes = udpwire::encode(p); CHECK(bytes.size() == 132);
    const std::vector<std::uint8_t> header{0x48,0x57,0x55,0x44,0,1,0,4,0,44,0,0,255,255,255,255,1,2,3,4,5,6,7,8};
    CHECK(std::equal(header.begin(), header.end(), bytes.begin()));
    CHECK(bytes[88] == 0x3f && bytes[89] == 0x80 && bytes[90] == 0 && bytes[91] == 0); // 1.f, big endian
    CHECK(bytes[124] == 0 && bytes[125] == 31 && bytes[128] == 0xab && bytes[131] == 1);
    std::string error; udpwire::Packet out;
    CHECK(udpwire::decode(bytes, out, error)); CHECK(out.player.feet.x == 1 && out.player.velocity.z == 6);
    CHECK(out.player.teleport == p.player.teleport && out.player.flying && out.player.active && out.player.crouched);
    for (auto type : {udpwire::Type::Probe, udpwire::Type::ProbeAck, udpwire::Type::Heartbeat, udpwire::Type::Player}) {
        p.type = type; p.acknowledgement = 0xffffffff; CHECK(udpwire::decode(udpwire::encode(p), out, error)); CHECK(out.type == type);
    }
    p.type = udpwire::Type::Player; bytes = udpwire::encode(p);
    for (std::size_t len = 0; len < bytes.size(); ++len) CHECK(!udpwire::decode(std::span(bytes).first(len), out, error));
    auto corrupt = bytes; corrupt.push_back(0); CHECK(!udpwire::decode(corrupt, out, error));
    for (const auto& [offset, value] : std::vector<std::pair<unsigned,unsigned>>{{0,0},{5,2},{7,7},{9,43},{11,1},{125,255},{127,1}}) {
        corrupt = bytes; corrupt[offset] = static_cast<std::uint8_t>(value); CHECK(!udpwire::decode(corrupt, out, error));
    }
    for (auto invalid : {0x7f800000U, 0xff800000U, 0x7fc01234U}) {
        corrupt = bytes; for (unsigned j = 0; j < 4; ++j) corrupt[88+j] = static_cast<std::uint8_t>(invalid >> (24-8*j));
        CHECK(!udpwire::decode(corrupt, out, error));
    }
    p.player.feet.x = std::numeric_limits<float>::infinity(); bool threw = false;
    try { udpwire::encode(p); } catch (const std::invalid_argument&) { threw = true; } CHECK(threw);
}
void sequences() {
    using R = udpwire::SequenceWindow::Result; udpwire::SequenceWindow w;
    CHECK(w.accept(0xfffffffe) == R::Newest); CHECK(w.accept(0) == R::Newest); // lost UINT32_MAX
    CHECK(w.accept(0xffffffff) == R::Reordered); CHECK(w.accept(0xffffffff) == R::Duplicate);
    CHECK(w.accept(1) == R::Newest); CHECK(w.accept(0) == R::Duplicate);
    CHECK(w.accept(0x80000001) == R::TooOld); CHECK(!udpwire::newer(0x80000001, 1));
    CHECK(w.accept(100) == R::Newest); CHECK(w.accept(36) == R::TooOld); CHECK(w.accept(37) == R::Reordered);
    CHECK(w.gapsObserved() == 99); w.reset(); CHECK(w.accept(0) == R::Newest);
}
void offerCodec() {
    wire::Message m; m.type = wire::Type::UdpOffer; m.session = id(1); m.participant = id(2); m.udpToken = id(3); m.udpPort = 45000;
    auto bytes = wire::encode(m); CHECK(bytes.size() == 68); wire::Decoder d; std::string error; unsigned calls = 0;
    for (auto b : bytes) CHECK(d.feed({&b, 1}, [&](wire::Frame&& f) { wire::Message v; CHECK(wire::decode(f, v, error));
        CHECK(v.udpPort == 45000 && v.udpToken == id(3) && v.udpVersion == 1); ++calls; return true; }, error));
    CHECK(calls == 1);
    m.udpPort = 0; bool threw = false; try { wire::encode(m); } catch (const std::invalid_argument&) { threw = true; } CHECK(threw);
}
void transportFiltering() {
    Fixture f; f.ready(); auto p = f.packet(udpwire::Type::Player, 1);
    for (Id udpwire::Packet::* field : {&udpwire::Packet::session, &udpwire::Packet::sender, &udpwire::Packet::target, &udpwire::Packet::token}) {
        auto bad = p; bad.*field = id(20); f.send(bad);
    }
    DatagramSocket stranger; stranger.send(udpwire::encode(p), f.udp.status().local.port); f.udp.receive(f.now, f.received);
    CHECK(f.udp.status().wrongPeer == 5 && f.received.empty());
    auto malformed = udpwire::encode(p); malformed.resize(4096, 0); f.raw.send(malformed, f.udp.status().local.port);
    f.udp.receive(f.now, f.received); CHECK(f.udp.status().invalid == 1);
    f.send(p); CHECK(f.received.size() == 1); // invalid packets must not consume sequence 1
    f.send(p); CHECK(f.received.size() == 1 && f.udp.status().duplicates == 1);
    p.sequence = 3; f.send(p); p.sequence = 2; f.send(p);
    CHECK(f.received.size() == 3 && f.udp.status().reordered == 1 && f.udp.status().gapsObserved == 1);
    p.sequence = 0xffffff00; f.send(p); CHECK(f.udp.status().tooOld == 1);
}
void probeLossAndHeartbeat() {
    Fixture f;
    f.udp.tick(f.now, {}); auto packets = f.raw.receive(); CHECK(packets.size() == 1); // drop first probe
    f.now += 101ms; f.udp.tick(f.now, {}); packets = f.raw.receive(); CHECK(packets.size() == 1);
    auto ack = f.packet(udpwire::Type::ProbeAck, 0); ack.acknowledgement = packets[0].sequence + 123;
    f.send(ack); CHECK(f.udp.status().state == UdpState::Probing);
    ack.acknowledgement = packets[0].sequence; f.send(ack); CHECK(f.udp.status().state == UdpState::Ready);
    f.now += 150ms; f.udp.tick(f.now, {}); packets = f.raw.receive();
    CHECK(packets.size() == 1 && packets[0].type == udpwire::Type::Heartbeat);
}
void fixedRateAndNoBurst() {
    Fixture f; f.ready(); const auto start = f.now;
    for (unsigned i = 0; i < 1000; ++i) { f.now = start + std::chrono::milliseconds(i); f.udp.tick(f.now, player()); f.raw.receive();
        if (i == 500) f.send(f.packet(udpwire::Type::Heartbeat, 1)); }
    CHECK(f.udp.status().playerSent >= 20 && f.udp.status().playerSent <= 21);
    const auto before = f.udp.status().playerSent;
    f.now += 500ms; f.send(f.packet(udpwire::Type::Heartbeat, 2)); f.udp.tick(f.now, player());
    CHECK(f.udp.status().playerSent == before + 1); // One current frame, not 15 historical frames.
}
void timeoutsAndStop() {
    Fixture f; f.udp.tick(f.now + 801ms, {}); CHECK(f.udp.status().state == UdpState::TimedOut && f.udp.fd() == -1);
    f.start(); f.ready(); const auto packet = f.packet(udpwire::Type::Heartbeat, 1); f.send(packet);
    f.now += 500ms; f.send(packet); // Replay must not keep the connection alive.
    f.udp.tick(f.now + 101ms, player()); CHECK(f.udp.status().state == UdpState::TimedOut);
    f.udp.stop(); CHECK(f.udp.status().state == UdpState::Disabled && !f.udp.deadline() && f.udp.fd() == -1);
    f.start(); f.ready(); CHECK(f.udp.status().state == UdpState::Ready);
}
void occupiedUdpPort() {
    DatagramSocket occupied; UdpTransport u(settings()); std::string e;
    CHECK(!u.start({"127.0.0.1", occupied.port}, id(1), id(2), id(3), id(4), Clock::now(), e));
    CHECK(u.fd() == -1 && u.status().state == UdpState::Error && e.find("bind") != std::string::npos);
}
void staleTokenAfterRestart() {
    Fixture f; f.ready(); auto stale = f.packet(udpwire::Type::Player, 123);
    const auto oldPort = f.udp.status().local.port; f.udp.stop(); std::string e;
    CHECK(f.udp.start({"127.0.0.1", oldPort}, id(1), id(2), id(3), id(9), f.now, e));
    CHECK(f.udp.setPeer({"127.0.0.1", f.raw.port}, id(5), f.now, e));
    f.send(stale); CHECK(f.udp.status().wrongPeer == 1); // Same session/IDs/endpoint, NEW receive cookie.
}
void realCore(const std::string& hostName, const std::string& clientName, std::string ip = "127.0.0.1") {
    Adapter a(hostName), b(clientName); Multiplayer h(a, settings()), c(b, settings()); pair(h, c, ip);
    const auto hs = h.status(), cs = c.status(); CHECK(hs.capabilities == 3 && cs.capabilities == 3);
    CHECK(hs.udp.local.port == hs.endpoint.port && hs.udp.peer.port == cs.udp.local.port);
    CHECK(cs.udp.peer.port == hs.udp.local.port);
    CHECK(h.submitPlayer(player(10)) && c.submitPlayer(player(20)));
    wait([&] { return a.players.size() >= 5 && b.players.size() >= 5; }, {&h, &c});
    CHECK(a.players.back().player.feet.x == 20 && b.players.back().player.feet.x == 10);
    CHECK(!a.wrongThread && !b.wrongThread);
    auto bad = player(); bad.pitch = std::numeric_limits<float>::quiet_NaN(); CHECK(!h.submitPlayer(bad));
    auto hidden = player(); hidden.active = false; CHECK(c.submitPlayer(hidden));
    wait([&] { return !a.players.empty() && !a.players.back().player.active; }, {&h, &c});
    c.leave(); wait([&] { return c.finished() && h.status().state == State::Listening; }, {&h, &c});
    CHECK(c.reap()); CHECK(h.status().udp.state == UdpState::Disabled);
    const auto callbacks = a.players.size(); std::string e; CHECK(c.join({ip, hs.endpoint.port}, e));
    wait([&] { return h.status().udp.state == UdpState::Ready && c.status().udp.state == UdpState::Ready; }, {&h, &c});
    CHECK(c.status().localId != cs.localId); CHECK(a.players.size() == callbacks);
    CHECK(c.submitPlayer(player(30))); wait([&] { return a.players.size() > callbacks; }, {&h, &c});
    CHECK(a.players.back().player.feet.x == 30);
    h.shutdown(); c.shutdown(); CHECK(h.notificationFd() == -1 && c.notificationFd() == -1);
}
void backwardsCompatibility() {
    for (bool oldHost : {true, false}) {
        Adapter a("egor"), b("stepan"); Settings old; auto fresh = settings();
        Multiplayer h(a, oldHost ? old : fresh), c(b, oldHost ? fresh : old);
        const auto port = host(h); std::string e; CHECK(c.join({"127.0.0.1", port}, e));
        wait([&] { return h.status().testsAcknowledged == 1 && c.status().testsAcknowledged == 1; }, {&h, &c});
        CHECK(h.status().capabilities == 1 && c.status().capabilities == 1);
        CHECK(h.status().udp.state == UdpState::Disabled && c.status().udp.state == UdpState::Disabled);
        CHECK(!h.submitPlayer(player()) && !c.submitPlayer(player()));
    }
}
void boundedCallbacks() {
    Adapter a("egor"), b("stepan"); auto s = settings(); s.udpPeerTimeout = 5000ms;
    Multiplayer h(a, s), c(b, s); pair(h, c); CHECK(c.submitPlayer(player()));
    // No dispatch: the worker must continue at 20 Hz and the player queue must remain bounded.
    std::this_thread::sleep_for(3800ms);
    CHECK(h.status().udp.playerReceived >= 65); CHECK(h.status().udp.receiveDropped > 0);
    h.dispatch(); CHECK(a.players.size() <= 64 && !a.players.empty());
    CHECK(h.status().state == State::Connected && h.status().udp.state == UdpState::Ready);
}
void lifecycle() {
    const auto fds = count("/proc/self/fd"), threads = count("/proc/self/task");
    for (int i = 0; i < 12; ++i) {
        Adapter a("egor"), b("stepan"); Multiplayer h(a, settings()), c(b, settings());
        pair(h, c); CHECK(h.submitPlayer(player()) && c.submitPlayer(player()));
        if (i % 2) { h.leave(); wait([&] { return h.finished(); }, {&h,&c}); }
        h.shutdown(); c.shutdown(); CHECK(count("/proc/self/fd") == fds);
    }
    wait([&] { return count("/proc/self/task") == threads; });
    CHECK(count("/proc/self/fd") == fds);
}
// A TCP peer with its own UDP socket exposes exact cookies and can deliberately withhold UDP.
struct RawPeer {
    Fd tcp;
    DatagramSocket udp;
    wire::Decoder decoder;
    std::deque<wire::Message> messages;
    wire::Message welcome, offer;
    std::uint32_t udpSequence = 0;
    explicit RawPeer(std::uint16_t port) : tcp(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)) {
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(port);
        const int n = ::connect(tcp.n, reinterpret_cast<sockaddr*>(&a), sizeof(a)); CHECK(n == 0 || errno == EINPROGRESS);
        pollfd fd{tcp.n,POLLOUT,0}; CHECK(::poll(&fd,1,1000) == 1);
        wire::Message hello; hello.type = wire::Type::Hello; hello.participant = id(90); hello.text = "raw-stepan"; hello.capabilities = 3; send(hello);
    }
    void send(const wire::Message& m) {
        const auto bytes = wire::encode(m); CHECK(::send(tcp.n, bytes.data(), bytes.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(bytes.size()));
    }
    void read() {
        std::uint8_t bytes[4096];
        for (int i = 0; i < 16; ++i) {
            const auto n = ::recv(tcp.n, bytes, sizeof(bytes), 0);
            if (n <= 0) { CHECK(n == 0 || errno == EAGAIN || errno == EWOULDBLOCK); return; }
            std::string e; CHECK(decoder.feed({bytes, static_cast<std::size_t>(n)}, [&](wire::Frame&& f) {
                wire::Message m; CHECK(wire::decode(f,m,e));
                if (m.type == wire::Type::Ping || m.type == wire::Type::Test) {
                    wire::Message ack; ack.type = m.type == wire::Type::Ping ? wire::Type::Pong : wire::Type::TestAck;
                    ack.sequence = m.sequence; send(ack);
                } else messages.push_back(m);
                return true;
            },e));
        }
    }
    wire::Message take(wire::Type type, Multiplayer& h) {
        wait([&] { read(); return std::any_of(messages.begin(),messages.end(),[&](auto& m) { return m.type == type; }); }, {&h});
        auto it = std::find_if(messages.begin(),messages.end(),[&](auto& m) { return m.type == type; });
        auto m = *it; messages.erase(it); return m;
    }
    void handshake(Multiplayer& h, bool sendOffer = true) {
        welcome = take(wire::Type::Welcome,h); wire::Message ready; ready.type = wire::Type::Ready;
        ready.session = welcome.session; ready.participant = id(90); send(ready);
        take(wire::Type::ReadyAck,h); offer = take(wire::Type::UdpOffer,h);
        if (sendOffer) {
            wire::Message m; m.type = wire::Type::UdpOffer; m.session = welcome.session; m.participant = id(90);
            m.udpPort = udp.port; m.udpToken = id(91); send(m);
        }
    }
    void pumpUdp() {
        for (const auto& p : udp.receive()) {
            CHECK(p.token == id(91) && p.session == welcome.session && p.target == id(90));
            if (p.type != udpwire::Type::Probe) continue;
            udpwire::Packet ack; ack.type = udpwire::Type::ProbeAck; ack.session = welcome.session; ack.sender = id(90);
            ack.target = welcome.participant; ack.token = offer.udpToken; ack.acknowledgement = p.sequence; ack.sequence = udpSequence++;
            udp.send(udpwire::encode(ack), offer.udpPort);
        }
    }
};
void udpTimeoutKeepsTcp(bool negotiate, bool reachReady) {
    Adapter a("egor"); auto s = settings(); s.udpHandshakeTimeout = 250ms; s.udpPeerTimeout = 200ms;
    Multiplayer h(a,s); RawPeer p(host(h)); p.handshake(h,negotiate);
    if (reachReady) wait([&] { p.read(); p.pumpUdp(); return h.status().udp.state == UdpState::Ready; },{&h});
    wait([&] { p.read(); return h.status().udp.state == UdpState::TimedOut; }, {&h});
    CHECK(h.status().state == State::Connected && !h.status().udp.error.empty());
    const auto end = Clock::now() + 400ms;
    while (Clock::now() < end) { p.read(); h.dispatch(); std::this_thread::sleep_for(2ms); }
    CHECK(h.status().state == State::Connected);
}
void invalidOffer(int variant) {
    Adapter a("egor"); Multiplayer h(a,settings()); RawPeer p(host(h)); p.handshake(h,false);
    wire::Message m; m.type = wire::Type::UdpOffer; m.session = p.welcome.session; m.participant = id(90); m.udpToken = id(91); m.udpPort = p.udp.port;
    if (variant == 0) m.session = id(100);
    if (variant == 1) m.participant = id(100);
    if (variant == 2) m.udpVersion = 2;
    p.send(m); if (variant == 3) p.send(m);
    wait([&] { return h.status().state == State::Listening; }, {&h}); CHECK(!h.status().lastError.empty());
}
}
int main() {
    unsigned passed = 0;
    const std::vector<std::pair<std::string,std::function<void()>>> tests{
        {"UDP golden bytes, finite floats, flags, lengths, all message types",codecs},
        {"sequence loss/reorder/duplicates/64-window/UINT32 wrap",sequences},
        {"TCP UDP-offer codec and bytewise framing",offerCodec},
        {"UDP endpoint/session/participant/cookie filtering and oversized datagrams",transportFiltering},
        {"lost probe retransmission, forged ACK rejection and heartbeat",probeLossAndHeartbeat},
        {"20 Hz independent of 1000 Hz caller, no catch-up burst",fixedRateAndNoBurst},
        {"UDP deadlines, replay not a heartbeat, stop and restart",timeoutsAndStop},
        {"occupied UDP port",occupiedUdpPort},
        {"old connection cookie rejected on same endpoint/session/IDs",staleTokenAfterRestart},
        {"Egor host -> Stepan client UDP + reconnect",[] { realCore("egor","stepan"); }},
        {"Stepan host -> Egor client UDP + reconnect",[] { realCore("stepan","egor"); }},
        {"real IPv6 UDP and TCP",[] { realCore("egor","stepan","::1"); }},
        {"control-only capability compatibility in both roles",backwardsCompatibility},
        {"bounded main-thread player queue while dispatch paused",boundedCallbacks},
        {"missing UDP offer timeout preserves TCP",[] { udpTimeoutKeepsTcp(false,false); }},
        {"unreachable UDP endpoint timeout preserves TCP",[] { udpTimeoutKeepsTcp(true,false); }},
        {"UDP liveness timeout preserves TCP",[] { udpTimeoutKeepsTcp(true,true); }},
        {"reject wrong-session UDP offer",[] { invalidOffer(0); }},
        {"reject wrong-participant UDP offer",[] { invalidOffer(1); }},
        {"reject unsupported UDP version",[] { invalidOffer(2); }},
        {"reject duplicate UDP offer",[] { invalidOffer(3); }},
        {"12 active UDP shutdown/leave cycles, fd and thread cleanup",lifecycle}
    };
    for (const auto& [name,test] : tests) {
        try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { std::cerr << "FAIL " << name << ": " << e.what() << '\n'; return 1; }
    }
    std::cout << passed << '/' << tests.size() << " UDP test groups passed\n";
}
