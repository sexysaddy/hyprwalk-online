#include <hyprwalk/mp/Multiplayer.hpp>
#include <hyprwalk/mp/Protocol.hpp>
#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <utility>

using namespace hyprwalk::mp;
using namespace std::chrono_literals;
namespace {
#define CHECK(expr) do { if (!(expr)) throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + ": " #expr); } while (0)
using Clock = std::chrono::steady_clock;
std::size_t baselineThreads = 0;
struct Fd {
    int fd = -1;
    explicit Fd(int n = -1) : fd(n) {}
    ~Fd() { if (fd >= 0) ::close(fd); }
    Fd(Fd&& other) noexcept : fd(std::exchange(other.fd, -1)) {}
    Fd(const Fd&) = delete;
};
struct Adapter final : IAdapter {
    std::string name;
    std::thread::id main = std::this_thread::get_id();
    bool wrongThread = false;
    std::vector<State> states;
    std::vector<std::string> diagnostics;
    explicit Adapter(std::string n = "test") : name(std::move(n)) {}
    LocalInfo localInfo() const override { CHECK(std::this_thread::get_id() == main); return {name}; }
    void onState(const Status& s) noexcept override {
        wrongThread |= std::this_thread::get_id() != main; states.push_back(s.state);
    }
    void onDiagnostic(const Diagnostic& d) noexcept override {
        wrongThread |= std::this_thread::get_id() != main; diagnostics.push_back(d.text);
    }
};
Settings fast() {
    Settings c;
    c.connectTimeout = c.handshakeTimeout = c.frameTimeout = c.writeTimeout = 250ms;
    c.heartbeatInterval = 80ms; c.peerTimeout = 700ms; c.leaveTimeout = 60ms;
    return c;
}
void pump(const std::vector<Multiplayer*>& cores) {
    std::vector<pollfd> fds;
    for (auto* core : cores) {
        core->dispatch();
        if (core->notificationFd() >= 0) fds.push_back({core->notificationFd(), POLLIN, 0});
    }
    ::poll(fds.data(), fds.size(), 2);
}
void wait(const std::function<bool()>& predicate, const std::vector<Multiplayer*>& cores = {}, std::chrono::milliseconds limit = 2500ms) {
    const auto deadline = Clock::now() + limit;
    while (!predicate()) {
        if (Clock::now() >= deadline) {
            std::string details;
            for (auto* c : cores) details += "\n" + formatStatus(c->status());
            throw std::runtime_error("wait deadline exceeded" + details);
        }
        pump(cores);
    }
    for (auto* core : cores) core->dispatch();
}
std::size_t count(const std::string& path) {
    return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator(path), std::filesystem::directory_iterator{}));
}
std::uint16_t host(Multiplayer& core, const std::string& ip = "127.0.0.1") {
    std::string error; CHECK(core.host({ip, 0}, error));
    wait([&] { return core.status().state == State::Listening; }, {&core});
    CHECK(core.status().endpoint.port != 0); return core.status().endpoint.port;
}
void connectPair(Multiplayer& server, Multiplayer& client, const std::string& ip = "127.0.0.1") {
    const auto port = host(server, ip);
    std::string error; CHECK(client.join({ip, port}, error));
    wait([&] { const auto h = server.status(), c = client.status();
        return h.state == State::Connected && c.state == State::Connected && h.testsReceived == 1 && c.testsReceived == 1 &&
               h.testsAcknowledged == 1 && c.testsAcknowledged == 1; }, {&server, &client});
}
void leave(Multiplayer& core, const std::vector<Multiplayer*>& others = {}) {
    core.leave();
    auto all = others; all.push_back(&core);
    wait([&] { return core.finished(); }, all);
    CHECK(core.status().state == State::Off);
    CHECK(core.reap()); CHECK(core.notificationFd() == -1); CHECK(!core.status().workerRunning);
}
wire::Message hello() {
    wire::Message m; m.type = wire::Type::Hello; m.participant[0] = 42; m.text = "raw-peer"; return m;
}
struct Raw {
    Fd socket;
    wire::Decoder decoder;
    std::deque<wire::Message> received;
    bool eof = false;
    explicit Raw(std::uint16_t port) : socket(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)) {
        CHECK(socket.fd >= 0);
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const int rc = ::connect(socket.fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        CHECK(rc == 0 || errno == EINPROGRESS);
        pollfd p{socket.fd, POLLOUT, 0}; CHECK(::poll(&p, 1, 1000) == 1);
        int error = 0; socklen_t n = sizeof(error);
        CHECK(::getsockopt(socket.fd, SOL_SOCKET, SO_ERROR, &error, &n) == 0 && error == 0);
    }
    explicit Raw(Fd accepted) : socket(std::move(accepted)) {}
    void sendBytes(const std::vector<std::uint8_t>& bytes, std::size_t slice = SIZE_MAX) {
        std::size_t at = 0;
        const auto deadline = Clock::now() + 2s;
        while (at < bytes.size()) {
            CHECK(Clock::now() < deadline);
            const auto n = ::send(socket.fd, bytes.data() + at, std::min(slice, bytes.size() - at), MSG_NOSIGNAL);
            if (n > 0) { at += static_cast<std::size_t>(n); if (slice == 1) std::this_thread::sleep_for(1ms); }
            else { CHECK(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR));
                pollfd p{socket.fd, POLLOUT, 0}; ::poll(&p, 1, 10); }
        }
    }
    void send(const wire::Message& m, std::size_t slice = SIZE_MAX) { sendBytes(wire::encode(m), slice); }
    void read() {
        std::uint8_t bytes[4096];
        for (int i = 0; i < 16; ++i) {
            const auto n = ::recv(socket.fd, bytes, sizeof(bytes), 0);
            if (n == 0) { eof = true; return; }
            if (n < 0) {
                if (errno == ECONNRESET) { eof = true; return; }
                CHECK(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR); return;
            }
            std::string error;
            CHECK(decoder.feed({bytes, static_cast<std::size_t>(n)}, [&](wire::Frame&& frame) {
                wire::Message m; CHECK(wire::decode(frame, m, error)); received.push_back(std::move(m)); return true;
            }, error));
        }
    }
    wire::Message take(wire::Type type, Multiplayer& server) {
        wait([&] { read(); return std::any_of(received.begin(), received.end(), [=](auto& m) { return m.type == type; }); }, {&server});
        auto it = std::find_if(received.begin(), received.end(), [=](auto& m) { return m.type == type; });
        auto m = *it; received.erase(it); return m;
    }
    wire::Message handshake(Multiplayer& server, bool fragmented = false) {
        send(hello(), fragmented ? 1 : SIZE_MAX);
        auto welcome = take(wire::Type::Welcome, server);
        wire::Message ready; ready.type = wire::Type::Ready; ready.session = welcome.session; ready.participant = hello().participant;
        send(ready, fragmented ? 1 : SIZE_MAX); take(wire::Type::ReadyAck, server);
        auto test = take(wire::Type::Test, server);
        wire::Message ack; ack.type = wire::Type::TestAck; ack.sequence = test.sequence; send(ack);
        wait([&] { return server.status().testsAcknowledged == 1; }, {&server});
        return welcome;
    }
};
struct BoundSocket {
    Fd socket{::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    std::uint16_t port = 0;
    explicit BoundSocket(bool listening, int backlog = 4) {
        CHECK(socket.fd >= 0);
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        CHECK(::bind(socket.fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0);
        socklen_t n = sizeof(a); CHECK(::getsockname(socket.fd, reinterpret_cast<sockaddr*>(&a), &n) == 0);
        port = ntohs(a.sin_port);
        if (listening) CHECK(::listen(socket.fd, backlog) == 0);
    }
};

void offIsInert() {
    const auto fds = count("/proc/self/fd"), threads = count("/proc/self/task");
    Adapter a;
    { Multiplayer c(a); CHECK(c.status().state == State::Off); CHECK(c.notificationFd() == -1); CHECK(c.finished());
      CHECK(count("/proc/self/fd") == fds); CHECK(count("/proc/self/task") == threads); }
    CHECK(count("/proc/self/fd") == fds);
}
void bidirectional(const std::string& hostName, const std::string& clientName, const std::string& ip = "127.0.0.1") {
    Adapter a(hostName), b(clientName); Multiplayer h(a, fast()), c(b, fast());
    connectPair(h, c, ip);
    const auto hs = h.status(), cs = c.status();
    CHECK(hs.session == cs.session); CHECK(wire::nonzero(hs.session));
    CHECK(hs.localId != cs.localId && hs.peerId == cs.localId && cs.peerId == hs.localId);
    CHECK(hs.peerName == clientName && cs.peerName == hostName);
    CHECK(hs.version == 1 && cs.version == 1 && hs.capabilities == ControlCapability);
    std::string error;
    CHECK(h.sendTest("explicit host message", error)); CHECK(c.sendTest(std::string(wire::MaxText, 'x'), error));
    CHECK(!h.sendTest(std::string(wire::MaxText + 1, 'x'), error)); CHECK(!h.sendTest("bad\ntext", error));
    wait([&] { return h.status().testsReceived == 2 && c.status().testsReceived == 2 &&
        h.status().testsAcknowledged == 2 && c.status().testsAcknowledged == 2; }, {&h, &c});
    CHECK(c.status().lastMessage == "explicit host message");
    CHECK(h.status().lastMessage.size() == wire::MaxText);
    const auto until = Clock::now() + 250ms; // More than two heartbeat intervals.
    while (Clock::now() < until) pump({&h, &c});
    CHECK(h.status().state == State::Connected && c.status().state == State::Connected);
    CHECK(!a.wrongThread && !b.wrongThread);
    leave(c, {&h}); wait([&] { return h.status().state == State::Listening; }, {&h});
    CHECK(h.status().session == hs.session); leave(h);
}
void reconnect() {
    Adapter a("egor"), b("stepan"); Multiplayer h(a, fast()), c(b, fast()); connectPair(h, c);
    const auto before = c.status(); const auto port = h.status().endpoint.port;
    leave(c, {&h}); wait([&] { return h.status().state == State::Listening; }, {&h});
    std::string error; CHECK(c.join({"127.0.0.1", port}, error));
    wait([&] { return c.status().testsAcknowledged == 1 && h.status().testsAcknowledged == 1 && c.status().testsReceived == 1; }, {&c, &h});
    CHECK(c.status().localId != before.localId); CHECK(c.status().session == before.session);
    leave(h, {&c}); wait([&] { return c.finished(); }, {&c}); CHECK(c.status().state == State::Off); CHECK(c.reap());
}
void protocolFraming() {
    auto m = hello(); m.participant = {}; m.participant[0] = 1; m.text = "egor";
    const auto bytes = wire::encode(m);
    const std::vector<std::uint8_t> golden{
        0x48,0x57,0x4d,0x50,0,1,0,1,0,0,0,30,0,0,0,0,
        0,1,0,1,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,'e','g','o','r'};
    CHECK(bytes == golden);
    for (std::size_t split = 0; split <= bytes.size(); ++split) {
        wire::Decoder d; std::string error; int seen = 0;
        auto visitor = [&](wire::Frame&& f) { wire::Message out; CHECK(wire::decode(f, out, error)); CHECK(out.text == "egor"); ++seen; return true; };
        CHECK(d.feed(std::span(bytes).first(split), visitor, error));
        CHECK(d.feed(std::span(bytes).subspan(split), visitor, error)); CHECK(seen == 1 && !d.partial());
    }
    std::vector<std::uint8_t> together;
    for (int i = 0; i < 40; ++i) together.insert(together.end(), bytes.begin(), bytes.end());
    wire::Decoder d; int seen = 0; std::string error;
    for (auto b : together) CHECK(d.feed({&b, 1}, [&](wire::Frame&&) { ++seen; return true; }, error));
    CHECK(seen == 40 && d.completed() == 40 && !d.partial());
    wire::Decoder coalesced; seen = 0;
    CHECK(coalesced.feed(together, [&](wire::Frame&&) { ++seen; return true; }, error)); CHECK(seen == 40);
}
void malformedProtocol() {
    const auto valid = wire::encode(hello());
    for (const auto& [offset, value] : std::vector<std::pair<std::size_t, std::uint8_t>>{{0,0},{5,2},{7,255},{8,255},{15,1}}) {
        auto bytes = valid; bytes[offset] = value; wire::Decoder d; std::string error;
        CHECK(!d.feed(bytes, [](wire::Frame&&) { return true; }, error)); CHECK(!error.empty());
        CHECK(!d.feed(valid, [](wire::Frame&&) { return true; }, error));
    }
    for (const unsigned size : {0U, 4097U, 0xffffffffU}) {
        auto bytes = valid; for (unsigned i = 0; i < 4; ++i) bytes[8+i] = static_cast<std::uint8_t>(size >> (24 - 8*i));
        wire::Decoder d; std::string error;
        CHECK(!d.feed(bytes, [](wire::Frame&&) { return true; }, error));
    }
    wire::Frame frame; frame.type = wire::Type::Hello; frame.payload.assign(valid.begin()+16, valid.end());
    wire::Message out; std::string error;
    auto bad = frame; bad.payload.push_back(0); CHECK(!wire::decode(bad, out, error));
    bad = frame; bad.payload.resize(3); CHECK(!wire::decode(bad, out, error));
    bad = frame; std::fill(bad.payload.begin()+8, bad.payload.begin()+24, 0); CHECK(!wire::decode(bad, out, error));
    bad = frame; bad.payload.back() = '\n'; CHECK(!wire::decode(bad, out, error));
    bad = frame; bad.payload[24] = 0xff; CHECK(!wire::decode(bad, out, error));
}
void partialWrites() {
    wire::WriteQueue q;
    auto first = wire::encode(hello()), second = first;
    std::vector<std::uint8_t> expected = first; expected.insert(expected.end(), second.begin(), second.end());
    CHECK(q.push(first)); CHECK(q.push(second));
    unsigned calls = 0; std::vector<std::uint8_t> actual; std::string error;
    bool blocked = false;
    while (!q.empty()) {
        const auto result = q.flush([&](const std::uint8_t* data, std::size_t n) -> ssize_t {
            ++calls;
            if (calls == 1) { errno = EINTR; return -1; }
            if (calls == 3) { errno = EAGAIN; return -1; }
            const auto used = std::min<std::size_t>(n, calls % 7 + 1);
            actual.insert(actual.end(), data, data+used); return static_cast<ssize_t>(used);
        }, error, 17);
        CHECK(result != wire::WriteQueue::Result::Failed);
        blocked |= result == wire::WriteQueue::Result::Pending;
    }
    CHECK(actual == expected && blocked && q.bytes() == 0);
    CHECK(q.push(first));
    CHECK(q.flush([](auto*, auto) -> ssize_t { errno = EPIPE; return -1; }, error) == wire::WriteQueue::Result::Failed);
    wire::WriteQueue zero; CHECK(zero.push(first));
    CHECK(zero.flush([](auto*, auto) -> ssize_t { return 0; }, error) == wire::WriteQueue::Result::Failed);
    wire::WriteQueue limit; CHECK(limit.push(std::vector<std::uint8_t>(wire::MaxQueuedBytes, 1))); CHECK(!limit.push({1}));
    wire::WriteQueue interrupted; CHECK(interrupted.push(first)); calls = 0;
    CHECK(interrupted.flush([&](auto*, auto) -> ssize_t { ++calls; errno = EINTR; return -1; }, error) == wire::WriteQueue::Result::Pending);
    CHECK(calls == 64);
}
void socketBackpressure() {
    int sockets[2]; CHECK(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets) == 0);
    Fd tx(sockets[0]), rx(sockets[1]); int small = 1024;
    CHECK(::setsockopt(tx.fd, SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)) == 0);
    wire::WriteQueue q; std::vector<std::uint8_t> expected(wire::MaxQueuedBytes);
    for (std::size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<std::uint8_t>(i);
    CHECK(q.push(expected)); std::string error;
    const auto sender = [&](const std::uint8_t* d, std::size_t n) { return ::send(tx.fd,d,n,MSG_NOSIGNAL); };
    CHECK(q.flush(sender, error) == wire::WriteQueue::Result::Pending); CHECK(q.bytes() > 0);
    std::vector<std::uint8_t> actual;
    wait([&] {
        std::uint8_t b[2048]; ssize_t n;
        while ((n = ::recv(rx.fd,b,sizeof(b),0)) > 0) actual.insert(actual.end(),b,b+n);
        CHECK(q.flush(sender,error) != wire::WriteQueue::Result::Failed);
        return q.empty() && actual.size() == expected.size();
    });
    CHECK(actual == expected);
}
void rejectedPeer(const std::function<std::vector<std::uint8_t>()>& packet, const std::string& reason) {
    Adapter a("host"), b("new-peer"); Multiplayer h(a, fast()), c(b, fast()); const auto port = host(h);
    { Raw raw(port); wait([&] { return h.status().state == State::Handshaking; }, {&h}); raw.sendBytes(packet());
      wait([&] { return h.status().state == State::Listening && h.status().lastError.find(reason) != std::string::npos; }, {&h});
      wait([&] { raw.read(); return raw.eof; }, {&h}); }
    std::string error; CHECK(c.join({"127.0.0.1",port},error));
    wait([&] { return c.status().testsAcknowledged == 1; }, {&h,&c}); leave(c,{&h}); leave(h);
}
void realFragmentation() {
    Adapter a; Multiplayer h(a, fast()); Raw raw(host(h)); raw.handshake(h, true);
    wire::Message test; test.type = wire::Type::Test; test.sequence = 1; test.text = "fragmented TCP payload";
    raw.send(test, 1); auto ack = raw.take(wire::Type::TestAck,h); CHECK(ack.sequence == 1);
    test.sequence = 2; auto two = wire::encode(test); test.sequence = 3; const auto three = wire::encode(test);
    two.insert(two.end(),three.begin(),three.end()); raw.sendBytes(two);
    wait([&] { return h.status().testsReceived == 3; }, {&h}); leave(h);
}
void badReady() {
    Adapter a; Multiplayer h(a, fast()); Raw raw(host(h)); raw.send(hello()); const auto welcome = raw.take(wire::Type::Welcome,h);
    wire::Message ready; ready.type = wire::Type::Ready; ready.session = welcome.session; ready.session[0] ^= 1;
    ready.participant = hello().participant; raw.send(ready);
    wait([&] { return h.status().state == State::Listening && h.status().lastError.find("ready identifiers") != std::string::npos; }, {&h}); leave(h);
}
void unexpectedAck() {
    Adapter a; Multiplayer h(a, fast()); Raw raw(host(h)); raw.handshake(h);
    wire::Message m; m.type = wire::Type::TestAck; m.sequence = 999; raw.send(m);
    wait([&] { return h.status().state == State::Listening && h.status().lastError.find("unknown test acknowledgement") != std::string::npos; }, {&h}); leave(h);
}
void portErrors() {
    BoundSocket occupied(true), refused(false); Adapter a,b; Multiplayer h(a,fast()), c(b,fast()); std::string error;
    CHECK(h.host({"127.0.0.1",occupied.port},error));
    wait([&] { return h.finished(); },{&h}); CHECK(h.status().state == State::Error); CHECK(h.status().lastError.find("bind:") != std::string::npos); CHECK(h.reap());
    CHECK(c.join({"127.0.0.1",refused.port},error));
    wait([&] { return c.finished(); },{&c}); CHECK(c.status().state == State::Error); CHECK(c.status().lastError.find("connect:") != std::string::npos); CHECK(c.reap());
    host(h); leave(h); // Error is recoverable after reap.
}
void invalidInputs() {
    Adapter a; Multiplayer c(a); std::string error;
    CHECK(!c.join({std::string("127.0.0.1\0suffix",16),99},error));
    CHECK(!c.join({"localhost",99},error)); CHECK(!c.join({"127.0.0.1",0},error));
    CHECK(!c.join({"0.0.0.0",99},error)); CHECK(!c.join({"::",99},error)); CHECK(c.notificationFd() == -1);
    a.name = "bad\nname"; CHECK(!c.host({"127.0.0.1",0},error)); CHECK(c.finished());
    a.name = "valid"; host(c); CHECK(!c.host({"127.0.0.1",0},error)); leave(c);
    auto settings = fast(); settings.heartbeatInterval = settings.peerTimeout;
    bool threw = false; try { Multiplayer bad(a,settings); } catch (const std::invalid_argument&) { threw = true; } CHECK(threw);
}
void handshakeTimeout() {
    Adapter a,b; Multiplayer h(a,fast()), c(b,fast()); Raw raw(host(h));
    wait([&] { return h.status().state == State::Listening && h.status().lastError == "handshake timeout"; }, {&h});
    BoundSocket silent(true); std::string error; CHECK(c.join({"127.0.0.1",silent.port},error));
    wait([&] { return c.finished(); }, {&c}); CHECK(c.status().lastError == "handshake timeout"); CHECK(c.reap()); leave(h);
}
void connectTimeout() {
    BoundSocket full(true,1); Raw first(full.port), second(full.port);
    Adapter a; auto settings = fast(); settings.connectTimeout = 120ms; Multiplayer c(a,settings); std::string error;
    CHECK(c.join({"127.0.0.1",full.port},error));
    wait([&] { return c.finished(); }, {&c}); CHECK(c.status().lastError == "connect timeout"); CHECK(c.reap());
}
void frameTimeout() {
    Adapter a; auto settings = fast(); settings.frameTimeout = 100ms; settings.handshakeTimeout = 1s;
    Multiplayer h(a,settings); Raw raw(host(h)); raw.sendBytes({0x48});
    wait([&] { return h.status().state == State::Listening && h.status().lastError == "incomplete frame timeout"; },{&h}); leave(h);
}
void heartbeatTimeout() {
    Adapter a; Multiplayer h(a,fast()); Raw raw(host(h)); raw.handshake(h);
    wait([&] { return h.status().state == State::Listening && h.status().lastError.find("timeout") != std::string::npos; }, {&h}); leave(h);
}
void abruptEof() {
    Adapter a; Multiplayer h(a,fast()); const auto port = host(h);
    { Raw raw(port); raw.handshake(h); raw.sendBytes({0x48,0x57}); }
    wait([&] { return h.status().state == State::Listening && h.status().lastError == "EOF inside a frame"; },{&h}); leave(h);
}
void singleGuest() {
    Adapter a("one"),b("two"); Multiplayer h(a,fast()), c(b,fast()); connectPair(h,c);
    const auto peer = h.status().peerId; Raw third(h.status().endpoint.port);
    wait([&] { third.read(); return third.eof; },{&h,&c}); CHECK(h.status().peerId == peer);
    CHECK(c.status().state == State::Connected); leave(c,{&h}); leave(h);
}
void repeatedLifecycle() {
    const auto fds = count("/proc/self/fd"), threads = baselineThreads;
    // pthread join precedes /proc bookkeeping settling. Check both joined ownership (reap)
    // and eventual task disappearance, never assume one immediate directory snapshot is final.
    wait([&] { return count("/proc/self/task") == threads; }, {}, 500ms);
    Adapter a; Multiplayer c(a,fast()); Id previous{};
    for (int i = 0; i < 40; ++i) {
        host(c); CHECK(previous != c.status().session); previous = c.status().session;
        leave(c); CHECK(count("/proc/self/fd") == fds); wait([&] { return count("/proc/self/task") == threads; }, {}, 500ms);
    }
    for (int i = 0; i < 12; ++i) {
        Adapter b; Multiplayer h(a,fast()), guest(b,fast()); connectPair(h,guest);
        const auto begin = Clock::now(); h.shutdown(); CHECK(Clock::now()-begin < 500ms);
        wait([&] { return guest.finished(); },{&guest}); CHECK(guest.status().state == State::Error); guest.shutdown();
        CHECK(count("/proc/self/fd") == fds); wait([&] { return count("/proc/self/task") == threads; }, {}, 500ms);
    }
    for (int i = 0; i < 20; ++i) { Multiplayer transient(a,fast()); std::string e; CHECK(transient.host({"127.0.0.1",0},e)); }
    CHECK(count("/proc/self/fd") == fds); wait([&] { return count("/proc/self/task") == threads; }, {}, 500ms);
}
void clientRejectsBadServer() {
    for (int mode = 0; mode < 5; ++mode) {
        BoundSocket listener(true); Adapter a; Multiplayer c(a,fast()); std::string error;
        CHECK(c.join({"127.0.0.1",listener.port},error));
        pollfd p{listener.socket.fd,POLLIN,0}; CHECK(::poll(&p,1,1000) == 1);
        Fd accepted(::accept4(listener.socket.fd,nullptr,nullptr,SOCK_NONBLOCK|SOCK_CLOEXEC)); CHECK(accepted.fd >= 0);
        Raw raw(std::move(accepted)); const auto peerHello = raw.take(wire::Type::Hello,c);
        wire::Message welcome; welcome.type=wire::Type::Welcome; welcome.session[0]=91; welcome.participant[0]=92;
        welcome.echo=peerHello.participant; welcome.text="different-implementation";
        if (mode == 0) welcome.selectedVersion=2;
        if (mode == 1) welcome.capabilities=ControlCapability|2U;
        if (mode == 2) welcome.echo[0]^=1;
        if (mode == 3) welcome.participant=peerHello.participant;
        raw.send(welcome);
        if (mode == 4) {
            raw.take(wire::Type::Ready,c);
            wire::Message ack; ack.type=wire::Type::ReadyAck; ack.session=welcome.session; ack.session[0]^=1;
            ack.participant=welcome.participant; raw.send(ack);
        }
        wait([&] { return c.finished(); },{&c}); CHECK(c.status().state == State::Error);
        CHECK(c.status().lastError.find("invalid") != std::string::npos); CHECK(c.reap());
    }
}
void samePortReuse() {
    Adapter a("host"),b("guest"); Multiplayer h(a,fast()),c(b,fast()); std::uint16_t port=0; Id previous{};
    for (int i=0;i<8;++i) {
        std::string error; CHECK(h.host({"127.0.0.1",port},error));
        wait([&] { return h.status().state == State::Listening; },{&h}); port=h.status().endpoint.port;
        CHECK(h.status().session != previous); previous=h.status().session;
        CHECK(c.join({"127.0.0.1",port},error));
        wait([&] { return c.status().testsAcknowledged==1 && h.status().testsAcknowledged==1 &&
            c.status().testsReceived==1 && h.status().testsReceived==1; },{&h,&c});
        leave(h,{&c}); wait([&] { return c.finished(); },{&c}); CHECK(c.status().state == State::Off); CHECK(c.reap());
    }
}
void cancelPending() {
    BoundSocket full(true,1); Raw first(full.port),second(full.port);
    Adapter a; Multiplayer c(a,fast()); std::string error; CHECK(c.join({"127.0.0.1",full.port},error));
    wait([&] { return c.status().state == State::Connecting; },{&c}); leave(c);
    Multiplayer h(a,fast()); Raw raw(host(h));
    wait([&] { return h.status().state == State::Handshaking; },{&h});
    const auto start=Clock::now(); h.shutdown(); CHECK(Clock::now()-start < 500ms);
    CHECK(h.notificationFd() == -1 && h.finished()); wait([&] { raw.read(); return raw.eof; });
}
void eventQueueBounds() {
    Adapter a; Multiplayer h(a,fast()); Raw raw(host(h)); raw.handshake(h);
    // Do not dispatch during the burst. The raw peer drains ACKs so only the event queue is pressured.
    for (std::uint64_t batch = 0; batch < 12; ++batch) {
        std::vector<std::uint8_t> bytes;
        for (std::uint64_t i = 1; i <= 40; ++i) {
            wire::Message m; m.type = wire::Type::Test; m.sequence = batch*40+i; m.text = "bounded";
            auto frame = wire::encode(m); bytes.insert(bytes.end(),frame.begin(),frame.end());
        }
        raw.sendBytes(bytes);
        const auto deadline = Clock::now()+150ms;
        while (h.status().testsReceived < (batch+1)*40) { CHECK(Clock::now() < deadline); raw.read(); std::this_thread::sleep_for(1ms); }
        raw.read();
    }
    CHECK(h.status().droppedEvents > 0); CHECK(h.status().state == State::Connected);
    const auto before = a.diagnostics.size()+a.states.size(); h.dispatch();
    CHECK(a.diagnostics.size()+a.states.size()-before <= 64);
    CHECK(h.status().testsReceived == 480);
    h.leave();
    // Intentionally stop pumping until the worker has completed; dispatch must then
    // retain the eventfd until the tail (final status) of the bounded queue is delivered.
    wait([&] { return h.finished(); });
    bool drained = false;
    for (int i = 0; i < 8 && !drained; ++i) drained = h.dispatch();
    CHECK(drained && !a.states.empty() && a.states.back() == State::Off);
    CHECK(h.reap());
}
} // namespace
int main() {
    // Warm sanitizer/runtime thread bookkeeping before taking the leak-check baseline.
    std::thread([] {}).join();
    std::this_thread::sleep_for(20ms);
    baselineThreads = count("/proc/self/task");
    const std::vector<std::pair<std::string,std::function<void()>>> tests{
        {"off creates no fds or threads",offIsInert},
        {"golden bytes, every split, coalescing",protocolFraming},
        {"invalid framing and payloads",malformedProtocol},
        {"partial write, EINTR, EAGAIN, EPIPE, bounds",partialWrites},
        {"real socket backpressure",socketBackpressure},
        {"Egor host -> Stepan client, bidirectional exchange",[]{bidirectional("egor","stepan");}},
        {"Stepan host -> Egor client, bidirectional exchange",[]{bidirectional("stepan","egor");}},
        {"IPv6 loopback",[]{bidirectional("ipv6-host","ipv6-client","::1");}},
        {"reconnect and graceful host leave",reconnect},
        {"real TCP fragmented and coalesced messages",realFragmentation},
        {"reject unsupported envelope and recover",[]{rejectedPeer([]{auto b=wire::encode(hello());b[5]=2;return b;},"envelope version");}},
        {"reject incompatible protocol range and recover",[]{rejectedPeer([]{auto m=hello();m.minVersion=m.maxVersion=2;return wire::encode(m);},"version range");}},
        {"reject missing capability and recover",[]{rejectedPeer([]{auto m=hello();m.capabilities=0;return wire::encode(m);},"capability");}},
        {"reject oversized payload before allocation",[]{rejectedPeer([]{auto b=wire::encode(hello());b[8]=255;return b;},"payload size");}},
        {"reject message in wrong phase",[]{rejectedPeer([]{wire::Message m;m.type=wire::Type::Ping;m.sequence=1;return wire::encode(m);},"handshake phase");}},
        {"reject wrong session in ready",badReady},
        {"reject forged acknowledgement",unexpectedAck},
        {"occupied port and refused connection",portErrors},
        {"input validation and double start",invalidInputs},
        {"client rejects invalid welcome/ready acknowledgement (5 variants)",clientRejectsBadServer},
        {"same-port restart after active sessions (8 cycles)",samePortReuse},
        {"cancel pending connect and unload during handshake",cancelPending},
        {"host/client handshake deadlines",handshakeTimeout},
        {"real connect timeout with full loopback backlog",connectTimeout},
        {"partial-frame deadline",frameTimeout},
        {"silent peer heartbeat/receive deadline",heartbeatTimeout},
        {"truncated EOF",abruptEof},
        {"only one guest is admitted",singleGuest},
        {"bounded event queue and callback batch",eventQueueBounds},
        {"40 host/leave, 12 active unloads, 20 immediate destructors; fd/thread cleanup",repeatedLifecycle},
    };
    int failed = 0;
    for (const auto& [name,test] : tests) {
        try { test(); std::cout << "PASS " << name << std::endl; }
        catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << std::endl; }
    }
    std::cout << tests.size()-failed << "/" << tests.size() << " test groups passed\n";
    return failed ? 1 : 0;
}
