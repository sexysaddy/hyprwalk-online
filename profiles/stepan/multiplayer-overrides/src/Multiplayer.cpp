#include <hyprwalk/mp/Multiplayer.hpp>
#include <hyprwalk/mp/Protocol.hpp>
#include <hyprwalk/mp/VideoTypes.hpp>
#include <hyprwalk/mp/UdpTransport.hpp>

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <netinet/tcp.h>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace hyprwalk::mp {
namespace {
using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;
using wire::Message;
using wire::Type;
constexpr std::size_t EventLimit = 256, CommandLimit = 32, PendingTestLimit = 32;

struct Fd {
    int value = -1;
    Fd() = default;
    explicit Fd(int v) : value(v) {}
    ~Fd() { reset(); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    void reset(int v = -1) noexcept { if (value >= 0) ::close(value); value = v; }
};
std::string systemError(const char* operation) {
    return std::string(operation) + ": " + std::strerror(errno);
}
void signalFd(int fd) noexcept {
    if (fd < 0) return;
    const std::uint64_t one = 1;
    for (unsigned tries = 0; tries < 16; ++tries) {
        if (::write(fd, &one, sizeof(one)) >= 0 || errno != EINTR) return;
    }
}
void drainFd(int fd) noexcept {
    if (fd < 0) return;
    std::uint64_t value = 0;
    for (unsigned tries = 0; tries < 16; ++tries) {
        if (::read(fd, &value, sizeof(value)) >= 0 || errno != EINTR) return;
    }
}
Id randomId() {
    Id id{};
    std::size_t at = 0;
    for (unsigned tries = 0; at < id.size() && tries < 32; ++tries) {
        const auto n = ::getrandom(id.data() + at, id.size() - at, GRND_NONBLOCK);
        if (n > 0) at += static_cast<std::size_t>(n);
        else if (n < 0 && errno != EINTR) throw std::runtime_error(systemError("getrandom"));
    }
    if (at != id.size() || !wire::nonzero(id)) throw std::runtime_error("could not generate random identifier");
    return id;
}
struct Address {
    sockaddr_storage storage{};
    socklen_t length = 0;
    int family = AF_UNSPEC;
};
Address addressOf(const Endpoint& ep, Role role) {
    if (ep.ip.empty() || ep.ip.size() >= INET6_ADDRSTRLEN || ep.ip.find('\0') != std::string::npos)
        throw std::invalid_argument("invalid numeric address length/content");
    Address out;
    auto* v4 = reinterpret_cast<sockaddr_in*>(&out.storage);
    if (::inet_pton(AF_INET, ep.ip.c_str(), &v4->sin_addr) == 1) {
        out.family = v4->sin_family = AF_INET;
        v4->sin_port = htons(ep.port); out.length = sizeof(sockaddr_in);
        if (role == Role::Client && v4->sin_addr.s_addr == htonl(INADDR_ANY))
            throw std::invalid_argument("join requires a non-wildcard address");
    } else {
        auto* v6 = reinterpret_cast<sockaddr_in6*>(&out.storage);
        if (::inet_pton(AF_INET6, ep.ip.c_str(), &v6->sin6_addr) != 1)
            throw std::invalid_argument("numeric IPv4 or IPv6 address required (no DNS, brackets or scope suffix)");
        out.family = v6->sin6_family = AF_INET6;
        v6->sin6_port = htons(ep.port); out.length = sizeof(sockaddr_in6);
        if (role == Role::Client && IN6_IS_ADDR_UNSPECIFIED(&v6->sin6_addr))
            throw std::invalid_argument("join requires a non-wildcard address");
    }
    if (role == Role::Client && ep.port == 0) throw std::invalid_argument("join port must be 1..65535");
    return out;
}
Endpoint socketEndpoint(int fd, bool peer) {
    sockaddr_storage address{}; socklen_t size = sizeof(address);
    const int rc = peer ? ::getpeername(fd, reinterpret_cast<sockaddr*>(&address), &size)
                        : ::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size);
    if (rc != 0) throw std::runtime_error(systemError("TCP address for UDP"));
    char ip[INET6_ADDRSTRLEN]{};
    Endpoint result;
    if (address.ss_family == AF_INET) {
        const auto* v = reinterpret_cast<const sockaddr_in*>(&address);
        if (!::inet_ntop(AF_INET, &v->sin_addr, ip, sizeof(ip))) throw std::runtime_error(systemError("inet_ntop"));
        result.port = ntohs(v->sin_port);
    } else if (address.ss_family == AF_INET6) {
        const auto* v = reinterpret_cast<const sockaddr_in6*>(&address);
        if (!::inet_ntop(AF_INET6, &v->sin6_addr, ip, sizeof(ip))) throw std::runtime_error(systemError("inet_ntop"));
        result.port = ntohs(v->sin6_port);
    } else throw std::runtime_error("unsupported TCP address family for UDP");
    result.ip = ip; return result;
}
void configurePeer(int fd) {
    const int one = 1;
    if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) != 0)
        throw std::runtime_error(systemError("TCP_NODELAY"));
}
bool validTransition(State from, State to) noexcept {
    if (from == to) return true;
    if (to == State::Error) return true;
    if (to == State::Stopping) return from != State::Off && from != State::Error;
    switch (from) {
        case State::Starting: return to == State::Listening || to == State::Connecting;
        case State::Listening: return to == State::Handshaking;
        case State::Connecting: return to == State::Handshaking;
        case State::Handshaking: return to == State::Connected || to == State::Listening || to == State::Off;
        case State::Connected: return to == State::Listening || to == State::Off;
        case State::Stopping: return to == State::Off;
        case State::Off: case State::Error: return to == State::Starting;
    }
    return false;
}
std::string quoted(const std::string& text) {
    constexpr char hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
struct PeerError : std::runtime_error { using std::runtime_error::runtime_error; };
struct PeerClosed { std::string reason; bool orderly = false; };
} // namespace

const char* stateName(State state) noexcept {
    switch (state) {
        case State::Off: return "off"; case State::Starting: return "starting";
        case State::Listening: return "listening"; case State::Connecting: return "connecting";
        case State::Handshaking: return "handshaking"; case State::Connected: return "connected";
        case State::Stopping: return "stopping"; case State::Error: return "error";
    }
    return "invalid";
}
const char* roleName(Role role) noexcept {
    switch (role) { case Role::None: return "none"; case Role::Host: return "host"; case Role::Client: return "client"; }
    return "invalid";
}
std::string idString(const Id& id) {
    constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (auto v : id) { out += hex[v >> 4]; out += hex[v & 15]; }
    return out;
}
std::string formatStatus(const Status& s) {
    std::ostringstream out;
    out << "{\"state\":" << quoted(stateName(s.state)) << ",\"role\":" << quoted(roleName(s.role))
        << ",\"ip\":" << quoted(s.endpoint.ip) << ",\"port\":" << s.endpoint.port
        << ",\"session\":" << quoted(idString(s.session)) << ",\"local_id\":" << quoted(idString(s.localId))
        << ",\"peer_id\":" << quoted(idString(s.peerId)) << ",\"local_name\":" << quoted(s.localName)
        << ",\"peer_name\":" << quoted(s.peerName) << ",\"protocol\":" << s.version
        << ",\"capabilities\":" << s.capabilities << ",\"test_sent\":" << s.testsSent
        << ",\"test_received\":" << s.testsReceived << ",\"test_acked\":" << s.testsAcknowledged
        << ",\"last_message\":" << quoted(s.lastMessage) << ",\"last_error\":" << quoted(s.lastError)
        << ",\"video_error\":" << quoted(s.videoError) << ",\"video_signals_sent\":" << s.videoSignalsSent << ",\"video_signals_received\":" << s.videoSignalsReceived
        << ",\"windows_sent\":" << s.windowsSent << ",\"windows_received\":" << s.windowsReceived
        << ",\"published_windows\":" << s.publishedWindows << ",\"remote_windows\":" << s.remoteWindows
        << ",\"world_state\":" << quoted(worldStateName(s.world)) << ",\"world_error\":" << quoted(s.worldError)
        << ",\"udp_state\":" << quoted(udpStateName(s.udp.state))
        << ",\"udp_local_port\":" << s.udp.local.port << ",\"udp_peer_port\":" << s.udp.peer.port
        << ",\"udp_sent\":" << s.udp.sent << ",\"udp_received\":" << s.udp.received
        << ",\"player_sent\":" << s.udp.playerSent << ",\"player_received\":" << s.udp.playerReceived
        << ",\"udp_invalid\":" << s.udp.invalid << ",\"udp_wrong_peer\":" << s.udp.wrongPeer
        << ",\"udp_duplicates\":" << s.udp.duplicates << ",\"udp_reordered\":" << s.udp.reordered
        << ",\"udp_too_old\":" << s.udp.tooOld << ",\"udp_gaps_observed\":" << s.udp.gapsObserved
        << ",\"udp_send_dropped\":" << s.udp.sendDropped << ",\"player_queue_dropped\":" << s.udp.receiveDropped
        << ",\"udp_error\":" << quoted(s.udp.error)
        << ",\"dropped_events\":" << s.droppedEvents << ",\"worker_running\":" << (s.workerRunning ? "true" : "false") << '}';
    return out.str();
}

struct Multiplayer::Impl {
    struct Event { bool stateEvent = false; Status state; Diagnostic diagnostic; std::optional<WorldRequest> world; };
    IAdapter& adapter;
    Settings settings;
    mutable std::mutex mutex;
    Status snapshot;
    std::deque<Event> events;
    std::deque<std::string> commands;
    std::optional<MapDescriptor> localMap;
    std::optional<WorldConfirmation> worldConfirmation;
    std::optional<PlayerSnapshot> localPlayer;
    std::optional<WindowSnapshot> windowSubmission;
    std::optional<ReceivedWindows> windowEvent;
    std::deque<VideoSignal> videoCommands, videoEvents;
    std::size_t videoCommandBytes = 0, videoEventBytes = 0;
    std::uint64_t videoSerial = 0;
    std::deque<ReceivedPlayer> playerEvents;
    std::atomic<bool> abortRequested{false}, leaveRequested{false}, done{true};
    std::thread worker;
    int wakeFd = -1, eventFd = -1;

    explicit Impl(IAdapter& a, Settings c) : adapter(a), settings(c) {
        for (auto duration : {c.connectTimeout, c.handshakeTimeout, c.frameTimeout, c.writeTimeout,
                              c.heartbeatInterval, c.peerTimeout, c.leaveTimeout,
                              c.udpHandshakeTimeout, c.udpPeerTimeout, c.udpHeartbeatInterval, c.worldTimeout, c.windowLease})
            if (duration.count() <= 0 || duration > std::chrono::hours(1))
                throw std::invalid_argument("timeouts must be positive and no greater than one hour");
        if (c.enableVideo && !c.enableWindows) throw std::invalid_argument("video requires windows capability");
        if (c.enableWindows && !c.enableWorld) throw std::invalid_argument("windows require world admission");
        if (c.windowLease < std::chrono::milliseconds(1500)) throw std::invalid_argument("window lease must be at least 1500 ms");
        if (c.enableWorld && !c.enableUdp) throw std::invalid_argument("world admission requires UDP capability");
        if (c.udpHeartbeatInterval >= c.udpPeerTimeout)
            throw std::invalid_argument("UDP heartbeat interval must be shorter than UDP peer timeout");
        if (c.heartbeatInterval >= c.peerTimeout)
            throw std::invalid_argument("heartbeat interval must be shorter than peer timeout");
    }
    Status status() const { std::lock_guard lock(mutex); return snapshot; }
    void postLocked(Event event) {
        if (events.size() == EventLimit) { events.pop_front(); ++snapshot.droppedEvents; }
        if (event.stateEvent) event.state.droppedEvents = snapshot.droppedEvents;
        events.push_back(std::move(event));
    }
    void publish(const Status& value, bool stateEvent) {
        {
            std::lock_guard lock(mutex);
            const auto dropped = snapshot.droppedEvents;
            snapshot = value; snapshot.droppedEvents = dropped;
            if (stateEvent) postLocked({true, snapshot, {}, {}});
        }
        if (stateEvent) signalFd(eventFd);
    }
    void diagnostic(Level level, const std::string& text) {
        { std::lock_guard lock(mutex); postLocked({false, {}, {level, text}, {}}); }
        signalFd(eventFd);
    }
    struct Run {
        enum class Phase { None, Connecting, Hello, Welcome, Ready, ReadyAck, Established };
        Impl& owner;
        Status s;
        Address address;
        Fd listener, peer;
        UdpTransport udp;
        std::uint64_t playerQueueDropped = 0;
        Id worldTicket{};
        bool localWorldApplied = false, peerWorldApplied = false;
        Time worldDeadline{};
        std::vector<WindowMetadata> published;
        std::uint64_t windowSendRevision = 0, windowReceiveRevision = 0;
        Time nextWindowRefresh{}, windowReceivedAt{}, windowSubmittedAt{};
        bool windowStarted = false, windowExpired = false;
        VideoAssembler videoAssembler;
        std::deque<std::vector<std::uint8_t>> videoChunks;
        bool videoFailed = false;
        Time videoRateTime{};
        unsigned videoRateCount = 0;
        wire::Decoder decoder;
        wire::WriteQueue output;
        Phase phase = Phase::None;
        bool quit = false, leaving = false;
        Time phaseDeadline{}, lastRx{}, lastTx{}, partialSince{}, nextPing{}, pingSent{}, leaveDeadline{};
        std::uint64_t sentSequence = 0, receivedSequence = 0, pingSequence = 0, pendingPing = 0;
        std::map<std::uint64_t, Time> pendingTests;

        Run(Impl& o, Address a) : owner(o), s(o.status()), address(a), udp(o.settings) {}
        void state(State next) {
            if (!validTransition(s.state, next)) throw std::logic_error("illegal session state transition");
            s.state = next; owner.publish(s, true);
        }
        void queue(Message message) {
            const bool wasEmpty = output.empty();
            if (!output.push(wire::encode(message))) throw PeerError("outbound queue limit exceeded");
            if (wasEmpty) lastTx = Clock::now();
        }
        void clearConnection() {
            udp.stop(); s.udp = {}; s.world = WorldState::Disabled;
            clearWindows();
            worldTicket = {}; s.worldTicket = {}; localWorldApplied = peerWorldApplied = false;
            { std::lock_guard lock(owner.mutex); owner.worldConfirmation.reset(); }
            { std::lock_guard lock(owner.mutex); owner.playerEvents.clear(); owner.localPlayer.reset(); }
            peer.reset(); decoder = {}; output = {}; phase = Phase::None;
            pendingTests.clear(); pendingPing = 0; partialSince = {};
            { std::lock_guard lock(owner.mutex); owner.commands.clear(); }
        }
        void resetPeerStatus() {
            s.peerId = {}; s.peerName.clear(); s.lastError.clear(); s.lastMessage.clear();
            s.version = 0; s.capabilities = 0; s.worldError.clear();
            s.testsSent = s.testsReceived = s.testsAcknowledged = 0;
            s.windowsSent = s.windowsReceived = 0;
            sentSequence = receivedSequence = pingSequence = 0;
        }
        void handshake(Phase next) {
            phase = next; phaseDeadline = Clock::now() + owner.settings.handshakeTimeout;
            lastRx = lastTx = Clock::now(); state(State::Handshaking);
        }
        void sendHello() {
            handshake(Phase::Welcome);
            Message hello; hello.type = Type::Hello; hello.participant = s.localId; hello.text = s.localName;
            hello.capabilities = supportedCapabilities();
            queue(std::move(hello));
        }
        void setup() {
            s.localId = randomId();
            if (s.role == Role::Host) s.session = randomId();
            owner.publish(s, false);
            const int fd = ::socket(address.family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            if (fd < 0) throw std::runtime_error(systemError("socket"));
            if (s.role == Role::Host) {
                listener.reset(fd);
                const int one = 1;
                if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0)
                    throw std::runtime_error(systemError("SO_REUSEADDR"));
                if (address.family == AF_INET6 && ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one)) != 0)
                    throw std::runtime_error(systemError("IPV6_V6ONLY"));
                if (::bind(fd, reinterpret_cast<const sockaddr*>(&address.storage), address.length) != 0)
                    throw std::runtime_error(systemError("bind"));
                if (::listen(fd, 4) != 0) throw std::runtime_error(systemError("listen"));
                auto bound = address;
                if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound.storage), &bound.length) != 0)
                    throw std::runtime_error(systemError("getsockname"));
                s.endpoint.port = address.family == AF_INET
                    ? ntohs(reinterpret_cast<const sockaddr_in*>(&bound.storage)->sin_port)
                    : ntohs(reinterpret_cast<const sockaddr_in6*>(&bound.storage)->sin6_port);
                state(State::Listening);
                owner.diagnostic(Level::Info, "listening on " + s.endpoint.ip + ":" + std::to_string(s.endpoint.port));
            } else {
                peer.reset(fd); configurePeer(fd);
                phase = Phase::Connecting; phaseDeadline = Clock::now() + owner.settings.connectTimeout;
                state(State::Connecting);
                if (::connect(fd, reinterpret_cast<const sockaddr*>(&address.storage), address.length) == 0) sendHello();
                else if (errno != EINPROGRESS && errno != EINTR) throw std::runtime_error(systemError("connect"));
            }
        }
        void acceptPeers() {
            for (unsigned i = 0; i < 16 && !owner.abortRequested.load(); ++i) {
                const int fd = ::accept4(listener.value, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (fd < 0) {
                    if (errno == EINTR) continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK) return;
                    if (errno == ECONNABORTED || errno == EPROTO) continue;
                    throw std::runtime_error(systemError("accept4"));
                }
                if (peer.value >= 0) { ::close(fd); continue; } // Exactly one guest, no admission side effects.
                peer.reset(fd); configurePeer(fd); resetPeerStatus(); handshake(Phase::Hello);
            }
        }
        void test(const std::string& text) {
            if (pendingTests.size() >= PendingTestLimit) {
                owner.diagnostic(Level::Warning, "test request dropped: too many unacknowledged messages"); return;
            }
            if (sentSequence == UINT64_MAX) throw PeerError("test sequence exhausted; reconnect required");
            Message m; m.type = Type::Test; m.sequence = ++sentSequence; m.text = text;
            queue(m); pendingTests.emplace(m.sequence, Clock::now());
            ++s.testsSent; owner.publish(s, false);
        }
        std::uint32_t supportedCapabilities() const {
            auto capabilities = ControlCapability | (owner.settings.enableUdp ? UdpCapability : 0) |
                (owner.settings.enableWorld ? WorldCapability : 0) |
                (owner.settings.enableWindows ? WindowsCapability : 0) |
                (owner.settings.enableVideo ? VideoCapability : 0);
            return capabilities;
        }
        void clearVideo() {
            videoAssembler.reset();videoChunks.clear();videoFailed=false;videoRateTime={};videoRateCount=0;
            s.videoError.clear();s.videoSignalsSent=s.videoSignalsReceived=0;
            output.discardUnsent(Type::Video);
            std::lock_guard lock(owner.mutex);owner.videoCommands.clear();owner.videoEvents.clear();
            owner.videoCommandBytes=owner.videoEventBytes=0;owner.videoSerial=0;
        }
        void failVideo(const std::string& why) {
            if(videoFailed)return;
            videoFailed=true;s.videoError=why;videoAssembler.reset();videoChunks.clear();output.discardUnsent(Type::Video);
            {std::lock_guard lock(owner.mutex);owner.videoCommands.clear();owner.videoEvents.clear();owner.videoCommandBytes=owner.videoEventBytes=0;}
            owner.diagnostic(Level::Warning,"video signaling disabled: "+why);owner.publish(s,true);
        }
        void handleVideo(const std::vector<std::uint8_t>& bytes) {
            if(videoFailed)return;
            if(!windowsReady()||!(s.capabilities&VideoCapability)){failVideo("peer did not negotiate video/world");return;}
            const auto now=Clock::now();
            if(now-videoRateTime>=std::chrono::seconds(1)){videoRateTime=now;videoRateCount=0;}
            if(++videoRateCount>256){failVideo("video signaling rate limit");return;}
            std::string why;VideoChunk header;
            if(!decodeVideoChunk(bytes,header,why)){failVideo(why);return;}
            if(!videoScope(header.signal,s,true))return; // stale scope cannot poison current reassembly
            auto signal=videoAssembler.accept(bytes,now,why);
            if(!why.empty()){failVideo(why);return;}
            if(!signal)return;
            // Old worlds are ignored, never interpreted as current grants.
            if(!videoScope(*signal,s,true))return;
            bool overflow=false;
            {std::lock_guard lock(owner.mutex);
             overflow=owner.videoEvents.size()>=MaxVideoSignalQueue || signal->body.size()>MaxVideoSignalQueueBytes-owner.videoEventBytes;
             if(!overflow){owner.videoEventBytes+=signal->body.size();owner.videoEvents.push_back(std::move(*signal));}}
            if(overflow){failVideo("main-thread video queue overflow");return;}
            ++s.videoSignalsReceived;owner.publish(s,false);signalFd(owner.eventFd);
        }
        void videoTick(Time now) {
            if(videoFailed||!windowsReady()||!(s.capabilities&VideoCapability))return;
            if(videoAssembler.expire(now)){failVideo("video reassembly timeout");return;}
            // Reserve half the existing TCP queue for world, revocations and heartbeat.
            for(unsigned budget=0;budget<8&&output.bytes()<24576;++budget){
                if(videoChunks.empty()){
                    std::optional<VideoSignal> signal;
                    {std::lock_guard lock(owner.mutex);if(!owner.videoCommands.empty()){
                        signal=std::move(owner.videoCommands.front());owner.videoCommands.pop_front();owner.videoCommandBytes-=signal->body.size();}}
                    if(!signal)break;
                    if(!videoScope(*signal,s,false))continue;
                    for(auto& chunk:fragmentVideoSignal(*signal))videoChunks.push_back(std::move(chunk));
                    ++s.videoSignalsSent;owner.publish(s,false);
                }
                Message m;m.type=Type::Video;m.videoChunk=std::move(videoChunks.front());videoChunks.pop_front();queue(std::move(m));
            }
        }
        void clearWindows() {
            clearVideo();
            published.clear(); windowSendRevision=windowReceiveRevision=0;
            nextWindowRefresh=windowReceivedAt=windowSubmittedAt={}; windowStarted=windowExpired=false;
            s.publishedWindows=s.remoteWindows=0;
            output.discardUnsent(Type::Windows);
            std::lock_guard lock(owner.mutex); owner.windowSubmission.reset(); owner.windowEvent.reset();
        }
        bool windowsReady() const { return s.state==State::Connected && s.world==WorldState::Ready && (s.capabilities & WindowsCapability); }
        void sendWindows(const Time now) {
            if (windowSendRevision==UINT64_MAX) throw PeerError("window revision exhausted; reconnect required");
            Message message; message.type=Type::Windows;
            message.windows={s.session,s.localId,worldTicket,++windowSendRevision,published};
            output.discardUnsent(Type::Windows);
            queue(std::move(message)); ++s.windowsSent; s.publishedWindows=published.size();
            windowStarted=true; nextWindowRefresh=now+std::chrono::seconds(1);
            owner.publish(s,false);
        }
        void windowTick(Time now) {
            if(!windowsReady()) return;
            std::optional<WindowSnapshot> submission;
            {std::lock_guard lock(owner.mutex); submission.swap(owner.windowSubmission);}
            bool changed=false;
            if(submission && submission->session==s.session && submission->owner==s.localId && submission->generation==worldTicket) {
                changed=published!=submission->windows; published=std::move(submission->windows); windowSubmittedAt=now;
            }
            // A blocked main thread may not renew old permissions forever via worker heartbeats.
            if(!published.empty() && windowSubmittedAt!=Time{} && now-windowSubmittedAt>=owner.settings.windowLease) {
                published.clear(); changed=true;
                owner.diagnostic(Level::Warning,"local window source lease expired; metadata withdrawn");
            }
            if(changed || !windowStarted || (now>=nextWindowRefresh && output.empty())) sendWindows(now);
            // A live TCP socket is not proof that the publishing adapter is advancing.
            if(!windowExpired && windowReceivedAt!=Time{} && now-windowReceivedAt>=owner.settings.windowLease) {
                windowExpired=true; s.remoteWindows=0;
                {std::lock_guard lock(owner.mutex); owner.windowEvent=ReceivedWindows{{s.session,s.peerId,worldTicket,windowReceiveRevision,{}},true};}
                owner.publish(s,false); signalFd(owner.eventFd);
            }
        }
        void handleWindows(const WindowSnapshot& incoming) {
            if(!windowsReady() || incoming.session!=s.session || incoming.owner!=s.peerId || incoming.generation!=worldTicket)
                throw PeerError("window snapshot outside negotiated owner/world session");
            if(incoming.revision<=windowReceiveRevision) throw PeerError("repeated or regressed window revision");
            windowReceiveRevision=incoming.revision; windowReceivedAt=Clock::now(); windowExpired=false;
            s.remoteWindows=incoming.windows.size(); ++s.windowsReceived;
            {std::lock_guard lock(owner.mutex); owner.windowEvent=ReceivedWindows{incoming,false};}
            owner.publish(s,false); signalFd(owner.eventFd);
        }
        bool worldPlayable() const { return !owner.settings.enableWorld || s.world == WorldState::Ready; }
        void worldState(WorldState next) {
            s.world = next; s.worldTicket = worldTicket;
            if (next != WorldState::Ready) clearWindows();
            if (next != WorldState::Ready) {
                std::lock_guard lock(owner.mutex); owner.playerEvents.clear(); owner.localPlayer.reset();
            }
            owner.publish(s, true);
        }
        Message worldMessage(Type type) const {
            Message m; m.type = type; m.session = s.session; m.participant = s.localId; m.worldTicket = worldTicket;
            return m;
        }
        void rejectWorld(const std::string& reason, bool tellPeer = true) {
            s.worldError = reason; worldState(WorldState::Rejected);
            owner.diagnostic(Level::Warning, "world rejected: " + reason);
            if (tellPeer && wire::nonzero(worldTicket)) {
                auto m = worldMessage(Type::MapReject); m.text = reason; queue(m);
            }
        }
        void requestWorld(const MapDescriptor& map) {
            worldState(WorldState::Applying);
            { std::lock_guard lock(owner.mutex); owner.postLocked({false, {}, {}, WorldRequest{worldTicket, map}}); }
            signalFd(owner.eventFd);
        }
        void setupWorld() {
            if (!owner.settings.enableWorld) return;
            if ((s.capabilities & (WorldCapability | UdpCapability)) != (WorldCapability | UdpCapability)) { rejectWorld("peer lacks required world/UDP capabilities", false); return; }
            worldDeadline = Clock::now() + owner.settings.worldTimeout;
            if (s.role == Role::Host) {
                worldTicket = randomId();
                auto m = worldMessage(Type::MapOffer); m.map = *owner.localMap;
                queue(m); requestWorld(m.map);
            } else worldState(WorldState::WaitingOffer);
        }
        void worldConfirmed(const WorldConfirmation& confirmation) {
            if (confirmation.ticket != worldTicket || s.world != WorldState::Applying) return; // Stale main-thread completion.
            if (!confirmation.applied) { rejectWorld(confirmation.reason); return; }
            localWorldApplied = true; worldState(WorldState::WaitingPeer);
            if (s.role == Role::Client) queue(worldMessage(Type::MapReady));
            else if (peerWorldApplied) { queue(worldMessage(Type::MapReady)); worldState(WorldState::Ready); }
        }
        void handleWorld(const Message& m) {
            if (!(s.capabilities & WorldCapability) || m.session != s.session || m.participant != s.peerId)
                throw PeerError("invalid world message for this TCP session");
            if (m.type == Type::MapOffer) {
                if (s.role != Role::Client || s.world != WorldState::WaitingOffer) throw PeerError("unexpected map offer");
                worldTicket = m.worldTicket;
                std::string why;
                if (!compatibleMap(*owner.localMap, m.map, why)) { rejectWorld(why); return; }
                requestWorld(m.map); return;
            }
            if (m.worldTicket != worldTicket) throw PeerError("stale world admission ticket");
            if (m.type == Type::MapReject) { rejectWorld("peer: " + m.text.substr(0, 245), false); return; }
            if (s.world == WorldState::Rejected) return; // A READY may cross our rejection in flight.
            if (peerWorldApplied) throw PeerError("duplicate map ready");
            if (s.role == Role::Client) {
                if (s.world != WorldState::WaitingPeer || !localWorldApplied) throw PeerError("world ready before local application");
                peerWorldApplied = true; worldState(WorldState::Ready);
            } else {
                if (s.world != WorldState::Applying && s.world != WorldState::WaitingPeer) throw PeerError("unexpected map ready");
                peerWorldApplied = true;
                if (localWorldApplied) { queue(worldMessage(Type::MapReady)); worldState(WorldState::Ready); }
            }
        }
        void publishUdp() {
            const bool changed = s.udp.state != udp.status().state;
            s.udp = udp.status(); s.udp.receiveDropped = playerQueueDropped;
            owner.publish(s, changed);
            if (changed) {
                owner.diagnostic(s.udp.error.empty() ? Level::Info : Level::Warning,
                    std::string("UDP ") + udpStateName(s.udp.state) + (s.udp.error.empty() ? "" : ": " + s.udp.error));
                if (s.udp.state != UdpState::Ready) {
                    std::lock_guard lock(owner.mutex); owner.playerEvents.clear();
                }
            }
        }
        void setupUdp() {
            if (!(s.capabilities & UdpCapability)) return;
            auto local = socketEndpoint(peer.value, false);
            // A host uses its configured TCP port for UDP; the client gets an ephemeral UDP port.
            local.port = s.role == Role::Host ? s.endpoint.port : 0;
            const auto token = randomId(); std::string error;
            playerQueueDropped = 0;
            if (!udp.start(local, s.session, s.localId, s.peerId, token, Clock::now(), error)) {
                publishUdp(); return; // Keep TCP alive for status and diagnostics.
            }
            Message offer; offer.type = Type::UdpOffer; offer.session = s.session; offer.participant = s.localId;
            offer.udpPort = udp.status().local.port; offer.udpVersion = udpwire::Version; offer.udpToken = token;
            queue(offer); publishUdp();
        }
        void receiveUdp() {
            std::vector<ReceivedPlayer> received;
            udp.receive(Clock::now(), received);
            if (!worldPlayable()) received.clear();
            if (!received.empty()) {
                {
                    std::lock_guard lock(owner.mutex);
                    for (const auto& value : received) {
                        if (owner.playerEvents.size() == 64) { owner.playerEvents.pop_front(); ++playerQueueDropped; }
                        owner.playerEvents.push_back(value);
                    }
                }
                signalFd(owner.eventFd);
            }
            publishUdp();
        }
        void established() {
            phase = Phase::Established; lastRx = Clock::now(); nextPing = lastRx + owner.settings.heartbeatInterval;
            state(State::Connected);
            owner.diagnostic(Level::Info, "handshake confirmed with " + s.peerName + " session=" + idString(s.session));
            test("hello from " + s.localName);
            setupUdp(); setupWorld();
        }
        void handle(wire::Frame&& frame) {
            Message m; std::string error;
            if (!wire::decode(frame, m, error)) throw PeerError("malformed message: " + error);
            if (m.type == Type::Bye) throw PeerClosed{"peer left: " + m.text, true};
            if (m.type == Type::Error) throw PeerError("peer error: " + m.text);
            if (phase == Phase::Hello && m.type == Type::Hello) {
                if (m.minVersion > ProtocolVersion || m.maxVersion < ProtocolVersion)
                    throw PeerError("incompatible protocol version range");
                if (!(m.capabilities & ControlCapability)) throw PeerError("required control capability missing");
                if (m.participant == s.localId) throw PeerError("duplicate participant identifier");
                s.peerId = m.participant; s.peerName = m.text;
                s.version = ProtocolVersion; s.capabilities = m.capabilities & supportedCapabilities();
                Message welcome; welcome.type = Type::Welcome; welcome.session = s.session;
                welcome.participant = s.localId; welcome.echo = s.peerId; welcome.text = s.localName;
                welcome.capabilities = s.capabilities;
                queue(welcome); phase = Phase::Ready;
                owner.publish(s, false);
            } else if (phase == Phase::Welcome && m.type == Type::Welcome) {
                if (m.selectedVersion != ProtocolVersion || !(m.capabilities & ControlCapability) ||
                    (m.capabilities & ~supportedCapabilities()))
                    throw PeerError("invalid negotiated protocol/capabilities");
                if (m.echo != s.localId || m.participant == s.localId) throw PeerError("invalid welcome identifiers");
                s.session = m.session; s.peerId = m.participant; s.peerName = m.text;
                s.version = m.selectedVersion; s.capabilities = m.capabilities;
                Message ready; ready.type = Type::Ready; ready.session = s.session; ready.participant = s.localId;
                queue(ready); phase = Phase::ReadyAck;
                owner.publish(s, false);
            } else if (phase == Phase::Ready && m.type == Type::Ready) {
                if (m.session != s.session || m.participant != s.peerId) throw PeerError("invalid ready identifiers");
                Message ack; ack.type = Type::ReadyAck; ack.session = s.session; ack.participant = s.localId;
                queue(ack); established();
            } else if (phase == Phase::ReadyAck && m.type == Type::ReadyAck) {
                if (m.session != s.session || m.participant != s.peerId) throw PeerError("invalid ready acknowledgement");
                established();
            } else if (phase == Phase::Established) {
                switch (m.type) {
                    case Type::Windows: handleWindows(m.windows); break;
                    case Type::Video: handleVideo(m.videoChunk); break;
                    case Type::MapOffer: case Type::MapReady: case Type::MapReject: handleWorld(m); break;
                    case Type::UdpOffer: {
                        if (!(s.capabilities & UdpCapability) || m.session != s.session || m.participant != s.peerId ||
                            m.udpVersion != udpwire::Version) throw PeerError("invalid UDP offer for this TCP session");
                        // A failed local bind must not turn a healthy TCP channel into a failure.
                        if (udp.status().state == UdpState::Error || udp.status().state == UdpState::TimedOut) break;
                        auto endpoint = socketEndpoint(peer.value, true); endpoint.port = m.udpPort;
                        if (!udp.setPeer(endpoint, m.udpToken, Clock::now(), error)) throw PeerError(error);
                        publishUdp(); break;
                    }
                    case Type::Test: {
                        if (m.sequence <= receivedSequence) throw PeerError("duplicate or regressed test sequence");
                        receivedSequence = m.sequence;
                        Message ack; ack.type = Type::TestAck; ack.sequence = m.sequence; queue(ack);
                        ++s.testsReceived; s.lastMessage = m.text; owner.publish(s, false);
                        owner.diagnostic(Level::Info, "test received #" + std::to_string(m.sequence) + ": " + m.text);
                        break;
                    }
                    case Type::TestAck:
                        if (pendingTests.erase(m.sequence) != 1) throw PeerError("unknown test acknowledgement");
                        ++s.testsAcknowledged; owner.publish(s, false);
                        owner.diagnostic(Level::Info, "test acknowledged #" + std::to_string(m.sequence));
                        break;
                    case Type::Ping: {
                        Message pong; pong.type = Type::Pong; pong.sequence = m.sequence; queue(pong); break;
                    }
                    case Type::Pong:
                        if (pendingPing != m.sequence) throw PeerError("unexpected heartbeat acknowledgement");
                        pendingPing = 0; nextPing = Clock::now() + owner.settings.heartbeatInterval; break;
                    default: throw PeerError("message not allowed in connected state");
                }
            } else throw PeerError("message not allowed in handshake phase");
            lastRx = Clock::now();
        }
        void readPeer() {
            std::array<std::uint8_t, 4096> bytes{};
            // Limit each turn so a flooding peer cannot indefinitely postpone a stop request.
            for (unsigned reads = 0; reads < 16 && !owner.abortRequested.load(); ++reads) {
                const auto n = ::recv(peer.value, bytes.data(), bytes.size(), 0);
                if (n == 0) throw PeerClosed{decoder.partial() ? "EOF inside a frame" : "peer closed TCP without BYE", false};
                if (n < 0) {
                    if (errno == EINTR) continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK) return;
                    throw PeerError(systemError("recv"));
                }
                std::string error;
                const auto previous = decoder.completed();
                if (!decoder.feed({bytes.data(), static_cast<std::size_t>(n)}, [this](wire::Frame&& f) {
                        handle(std::move(f)); return true;
                    }, error)) throw PeerError("invalid frame: " + error);
                if (!decoder.partial()) partialSince = {};
                else if (partialSince == Time{} || previous != decoder.completed()) partialSince = Clock::now();
            }
        }
        void writePeer() {
            const auto before = output.bytes();
            std::string error;
            const auto result = output.flush([this](const std::uint8_t* bytes, std::size_t n) {
                return ::send(peer.value, bytes, n, MSG_NOSIGNAL);
            }, error);
            if (output.bytes() != before) lastTx = Clock::now();
            if (result == wire::WriteQueue::Result::Failed) throw PeerError(error);
        }
        void dropPeer(const std::string& reason, bool orderly) {
            clearConnection();
            if (!orderly) s.lastError = reason;
            owner.diagnostic(orderly ? Level::Info : Level::Error, reason);
            if (leaving) { state(State::Off); quit = true; }
            else if (s.role == Role::Host) {
                s.peerId = {}; s.peerName.clear(); s.version = 0; s.capabilities = 0;
                state(State::Listening);
            } else { state(orderly ? State::Off : State::Error); quit = true; }
        }
        void beginLeave() {
            leaving = true; udp.stop(); s.udp = {}; s.world = WorldState::Disabled;
            clearWindows();
            { std::lock_guard lock(owner.mutex); owner.playerEvents.clear(); }
            state(State::Stopping); listener.reset();
            leaveDeadline = Clock::now() + owner.settings.leaveTimeout;
            if (peer.value < 0 || phase == Phase::Connecting) { quit = true; return; }
            Message bye; bye.type = Type::Bye; bye.text = "local leave"; queue(bye);
        }
        void tick() {
            const auto now = Clock::now();
            if (owner.leaveRequested.exchange(false) && !leaving) beginLeave();
            if (quit) return;
            if (leaving) { if (output.empty() || now >= leaveDeadline) quit = true; return; }
            if (peer.value < 0) return;
            if (phase != Phase::Established && now >= phaseDeadline)
                throw PeerError(phase == Phase::Connecting ? "connect timeout" : "handshake timeout");
            if (partialSince != Time{} && now - partialSince >= owner.settings.frameTimeout)
                throw PeerError("incomplete frame timeout");
            if (!output.empty() && now - lastTx >= owner.settings.writeTimeout) throw PeerError("write stalled");
            if (phase != Phase::Established) return;
            if (now - lastRx >= owner.settings.peerTimeout) throw PeerError("peer receive timeout");
            if (pendingPing && now - pingSent >= owner.settings.peerTimeout) throw PeerError("heartbeat timeout");
            if (!pendingTests.empty() && now - pendingTests.begin()->second >= owner.settings.peerTimeout)
                throw PeerError("test acknowledgement timeout");
            if (!pendingPing && now >= nextPing) {
                if (pingSequence == UINT64_MAX) throw PeerError("heartbeat sequence exhausted");
                Message ping; ping.type = Type::Ping; ping.sequence = pendingPing = ++pingSequence;
                queue(ping); pingSent = now;
            }
            std::deque<std::string> queued;
            { std::lock_guard lock(owner.mutex); queued.swap(owner.commands); }
            for (const auto& text : queued) test(text);
            if (owner.settings.enableWorld) {
                std::optional<WorldConfirmation> confirmation;
                { std::lock_guard lock(owner.mutex); confirmation.swap(owner.worldConfirmation); }
                if (confirmation) worldConfirmed(*confirmation);
                if (s.world != WorldState::Ready && s.world != WorldState::Rejected && now >= worldDeadline)
                    rejectWorld("map admission/application timeout");
            }
            if (s.capabilities & UdpCapability) {
                std::optional<PlayerSnapshot> local;
                { std::lock_guard lock(owner.mutex); if (worldPlayable()) local = owner.localPlayer; }
                udp.tick(now, local); publishUdp();
            }
            windowTick(now);
            videoTick(now);
        }
        int timeout() const {
            if (leaving) return static_cast<int>(std::max<std::int64_t>(0,
                std::chrono::duration_cast<std::chrono::milliseconds>(leaveDeadline - Clock::now()).count()));
            if (peer.value < 0) return -1;
            Time deadline = phase != Phase::Established ? phaseDeadline : lastRx + owner.settings.peerTimeout;
            if (!output.empty()) deadline = std::min(deadline, lastTx + owner.settings.writeTimeout);
            if (partialSince != Time{}) deadline = std::min(deadline, partialSince + owner.settings.frameTimeout);
            if (phase == Phase::Established) {
                deadline = std::min(deadline, pendingPing ? pingSent + owner.settings.peerTimeout : nextPing);
                if (!pendingTests.empty()) deadline = std::min(deadline, pendingTests.begin()->second + owner.settings.peerTimeout);
                if (windowsReady()) {
                    // Never busy-poll when TCP is congested; write readiness retries the tick.
                    const auto refresh=nextWindowRefresh>Clock::now()?nextWindowRefresh:Clock::now()+std::chrono::milliseconds(50);
                    deadline=std::min(deadline,refresh);
                    if (!windowExpired && windowReceivedAt!=Time{}) deadline=std::min(deadline,windowReceivedAt+owner.settings.windowLease);
                    if (!published.empty() && windowSubmittedAt!=Time{}) deadline=std::min(deadline,windowSubmittedAt+owner.settings.windowLease);
                }
                if (const auto d = udp.deadline()) deadline = std::min(deadline, *d);
                if (owner.settings.enableWorld && s.world != WorldState::Ready && s.world != WorldState::Rejected)
                    deadline = std::min(deadline, worldDeadline);
            }
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
            return static_cast<int>(std::clamp<std::int64_t>(ms + 1, 0, INT_MAX));
        }
        void execute() {
            if (owner.abortRequested.load()) { state(State::Stopping); state(State::Off); return; }
            setup();
            while (!quit && !owner.abortRequested.load()) {
                try {
                    tick(); if (quit) break;
                    const short peerEvents = static_cast<short>((leaving ? 0 : POLLIN) |
                        ((phase == Phase::Connecting || !output.empty()) ? POLLOUT : 0));
                    pollfd fds[4]{{owner.wakeFd, POLLIN, 0}, {listener.value, POLLIN, 0}, {peer.value, peerEvents, 0},
                                  {udp.fd(), POLLIN, 0}};
                    const int result = ::poll(fds, 4, timeout());
                    if (result < 0) { if (errno == EINTR) continue; throw std::runtime_error(systemError("poll")); }
                    if (owner.abortRequested.load()) break;
                    if (fds[0].revents & POLLIN) { drainFd(owner.wakeFd); continue; }
                    if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) throw std::runtime_error("invalid wake eventfd");
                    if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) throw std::runtime_error("listener poll error");
                    if (fds[1].revents & POLLIN) acceptPeers();
                    // The poll snapshot may predate acceptPeers; do not interpret old revents for a new fd.
                    if (fds[2].fd != peer.value || peer.value < 0) continue;
                    const short ready = fds[2].revents;
                    if (ready & POLLNVAL) throw PeerError("invalid peer descriptor");
                    if (phase == Phase::Connecting && ready) {
                        int error = 0; socklen_t len = sizeof(error);
                        if (::getsockopt(peer.value, SOL_SOCKET, SO_ERROR, &error, &len) != 0) throw PeerError(systemError("SO_ERROR"));
                        if (error) { errno = error; throw PeerError(systemError("connect")); }
                        sendHello();
                    }
                    if (!leaving && (ready & (POLLIN | POLLHUP | POLLERR))) readPeer();
                    if (ready & POLLOUT) writePeer();
                    if (leaving && (ready & (POLLHUP | POLLERR))) quit = true;
                    if (!leaving && udp.fd() >= 0 && fds[3].fd == udp.fd() && (fds[3].revents & (POLLIN | POLLERR))) receiveUdp();
                    if (fds[3].revents & POLLNVAL) throw PeerError("invalid UDP descriptor");
                } catch (const PeerClosed& e) { dropPeer(e.reason, e.orderly); }
                  catch (const PeerError& e) { dropPeer(e.what(), false); }
            }
            // No waiting for the network in the unload path. Fds are closed before done is published.
            listener.reset(); clearConnection();
            if (owner.abortRequested.load() || leaving) {
                if (s.state != State::Stopping && s.state != State::Off) state(State::Stopping);
                if (s.state != State::Off) state(State::Off);
            }
        }
    };
    void entry(Address address) noexcept {
        try { Run run(*this, address); run.execute(); }
        catch (const std::exception& e) {
            try {
                auto s = status(); s.state = State::Error; s.lastError = e.what();
                publish(s, true); diagnostic(Level::Error, s.lastError);
            } catch (...) {} // Failure reporting must not strand a joinable worker.
        } catch (...) {
            try { auto s = status(); s.state = State::Error; s.lastError = "unknown worker failure"; publish(s, true); }
            catch (...) {}
        }
        // Run (and all network fds) is gone. Joining now can only wait for this final local notification.
        {
            std::lock_guard lock(mutex); snapshot.workerRunning = false; snapshot.udp.state = UdpState::Disabled; snapshot.world = WorldState::Disabled; playerEvents.clear(); videoCommands.clear();videoEvents.clear();videoCommandBytes=videoEventBytes=0; windowEvent.reset(); windowSubmission.reset(); snapshot.remoteWindows=snapshot.publishedWindows=0;
            done.store(true, std::memory_order_release);
            try { postLocked({true, snapshot, {}, {}}); } catch (...) {}
        }
        signalFd(eventFd);
    }
    bool start(Role role, const Endpoint& endpoint, std::string& error) {
        error.clear();
        if (worker.joinable() || eventFd >= 0) { error = "session not reaped; leave and remove event source before restarting"; return false; }
        try {
            const auto addr = addressOf(endpoint, role);
            const auto info = adapter.localInfo();
            localMap = settings.enableWorld ? adapter.localMap() : std::nullopt;
            if (settings.enableWorld) {
                std::string why;
                if (!localMap || !validMap(*localMap, why)) throw std::invalid_argument("local map unavailable: " + why);
            }
            if (!wire::printable(info.name, 64)) throw std::invalid_argument("participant name must be 1..64 printable ASCII bytes");
            wakeFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            if (wakeFd < 0) throw std::runtime_error(systemError("wake eventfd"));
            eventFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            if (eventFd < 0) throw std::runtime_error(systemError("notification eventfd"));
            abortRequested = leaveRequested = false; done = false;
            Status initial; initial.state = State::Starting; initial.role = role;
            initial.endpoint = endpoint; initial.localName = info.name; initial.workerRunning = true;
            { std::lock_guard lock(mutex); events.clear(); commands.clear(); playerEvents.clear(); windowEvent.reset(); windowSubmission.reset(); localPlayer.reset(); worldConfirmation.reset(); snapshot = {}; }
            publish(initial, true);
            worker = std::thread([this, addr] { entry(addr); });
            return true;
        } catch (const std::exception& e) {
            error = e.what(); done = true;
            if (eventFd >= 0) ::close(eventFd);
            if (wakeFd >= 0) ::close(wakeFd);
            eventFd = wakeFd = -1;
            { std::lock_guard lock(mutex); snapshot.state = State::Error; snapshot.workerRunning = false;
              snapshot.lastError = error; events.clear(); commands.clear(); }
            return false;
        }
    }
    void closeLocalFds() noexcept {
        if (eventFd >= 0) ::close(eventFd);
        if (wakeFd >= 0) ::close(wakeFd);
        eventFd = wakeFd = -1;
    }
};

Multiplayer::Multiplayer(IAdapter& adapter, Settings settings) : m_impl(std::make_unique<Impl>(adapter, settings)) {}
Multiplayer::~Multiplayer() { shutdown(); }
bool Multiplayer::host(const Endpoint& ep, std::string& error) { return m_impl->start(Role::Host, ep, error); }
bool Multiplayer::join(const Endpoint& ep, std::string& error) { return m_impl->start(Role::Client, ep, error); }
Status Multiplayer::status() const { return m_impl->status(); }
int Multiplayer::notificationFd() const noexcept { return m_impl->eventFd; }
bool Multiplayer::finished() const noexcept { return m_impl->done.load(std::memory_order_acquire); }
bool Multiplayer::sendTest(const std::string& text, std::string& error) {
    error.clear();
    if (!wire::printable(text, wire::MaxText)) { error = "test text must be 1..1024 printable ASCII bytes"; return false; }
    {
        std::lock_guard lock(m_impl->mutex);
        if (m_impl->snapshot.state != State::Connected || m_impl->leaveRequested.load()) { error = "not connected"; return false; }
        if (m_impl->commands.size() >= CommandLimit) { error = "command queue full"; return false; }
        m_impl->commands.push_back(text);
    }
    signalFd(m_impl->wakeFd); return true;
}
bool Multiplayer::submitPlayer(const PlayerSnapshot& player) {
    if (!validPlayerSnapshot(player)) return false;
    std::lock_guard lock(m_impl->mutex);
    if (m_impl->snapshot.state != State::Connected || !(m_impl->snapshot.capabilities & UdpCapability) ||
        m_impl->leaveRequested.load() || (m_impl->settings.enableWorld && m_impl->snapshot.world != WorldState::Ready)) return false;
    m_impl->localPlayer = player; return true;
}
bool Multiplayer::confirmWorld(const WorldConfirmation& confirmation) {
    if (!wire::nonzero(confirmation.ticket) || (!confirmation.applied && !wire::printable(confirmation.reason, 256))) return false;
    { std::lock_guard lock(m_impl->mutex);
      if (m_impl->snapshot.state != State::Connected || m_impl->snapshot.world != WorldState::Applying || confirmation.ticket != m_impl->snapshot.worldTicket || m_impl->worldConfirmation) return false;
      m_impl->worldConfirmation = confirmation; }
    signalFd(m_impl->wakeFd); return true;
}
void Multiplayer::leave() noexcept {
    if (finished()) return;
    m_impl->leaveRequested = true; signalFd(m_impl->wakeFd);
}
bool Multiplayer::submitWindows(const WindowSnapshot& publication) {
    std::string error;
    if (!validWindowSnapshot(publication,error)) return false;
    {
        std::lock_guard lock(m_impl->mutex);
        const auto& s=m_impl->snapshot;
        if(s.state!=State::Connected || s.world!=WorldState::Ready || !(s.capabilities&WindowsCapability) ||
           publication.session!=s.session || publication.owner!=s.localId || publication.generation!=s.worldTicket) return false;
        m_impl->windowSubmission=publication;
    }
    signalFd(m_impl->wakeFd); return true;
}
bool Multiplayer::sendVideoSignal(VideoSignal signal, std::string& error) {
    error.clear();
    {
        std::lock_guard lock(m_impl->mutex);
        const auto& s=m_impl->snapshot;
        signal.sender=s.localId;
        if(!videoScope(signal,s,false)||!s.videoError.empty()){error="video capability/world unavailable";return false;}
        if(m_impl->videoSerial==UINT64_MAX){error="video signal sequence exhausted";return false;}
        signal.serial=m_impl->videoSerial+1;
        if(!validVideoSignal(signal,error))return false;
        if(m_impl->videoCommands.size()>=MaxVideoSignalQueue||signal.body.size()>MaxVideoSignalQueueBytes-m_impl->videoCommandBytes){error="video signaling queue full";return false;}
        ++m_impl->videoSerial;m_impl->videoCommandBytes+=signal.body.size();m_impl->videoCommands.push_back(std::move(signal));
    }
    signalFd(m_impl->wakeFd);return true;
}
bool Multiplayer::dispatch(std::size_t maxEvents) {
    drainFd(m_impl->eventFd);
    maxEvents = std::min<std::size_t>(maxEvents, 64);
    for (std::size_t i = 0; i < maxEvents; ++i) {
        Impl::Event event;
        {
            std::lock_guard lock(m_impl->mutex);
            if (m_impl->events.empty()) break;
            event = std::move(m_impl->events.front()); m_impl->events.pop_front();
        }
        if (event.world) {
            const auto current = m_impl->status();
            if (current.world == WorldState::Applying && current.worldTicket == event.world->ticket)
                m_impl->adapter.onWorldRequest(*event.world);
        }
        else if (event.stateEvent) m_impl->adapter.onState(event.state);
        else m_impl->adapter.onDiagnostic(event.diagnostic);
    }
    // State transitions are delivered before snapshots. Old samples never survive a dropped connection.
    std::deque<ReceivedPlayer> players;
    {
        std::lock_guard lock(m_impl->mutex);
        if (m_impl->events.empty() && m_impl->snapshot.state == State::Connected && m_impl->snapshot.udp.state == UdpState::Ready &&
            (!m_impl->settings.enableWorld || m_impl->snapshot.world == WorldState::Ready))
            players.swap(m_impl->playerEvents);
        else if (m_impl->snapshot.state != State::Connected || m_impl->snapshot.udp.state != UdpState::Ready ||
                 (m_impl->settings.enableWorld && m_impl->snapshot.world != WorldState::Ready))
            m_impl->playerEvents.clear();
    }
    for (const auto& p : players) m_impl->adapter.onPlayerSnapshot(p);
    std::optional<ReceivedWindows> windows;
    {
        std::lock_guard lock(m_impl->mutex);
        const auto& s=m_impl->snapshot;
        if(s.state!=State::Connected || s.world!=WorldState::Ready || !(s.capabilities&WindowsCapability)) m_impl->windowEvent.reset();
        else if(m_impl->events.empty()) windows.swap(m_impl->windowEvent);
    }
    if(windows) {
        const auto current=m_impl->status();
        if(current.state==State::Connected && current.world==WorldState::Ready &&
           windows->snapshot.session==current.session && windows->snapshot.owner==current.peerId &&
           windows->snapshot.generation==current.worldTicket) m_impl->adapter.onWindows(*windows);
    }
    std::deque<VideoSignal> media;
    {
        std::lock_guard lock(m_impl->mutex);
        if(m_impl->snapshot.state!=State::Connected||m_impl->snapshot.world!=WorldState::Ready||!m_impl->snapshot.videoError.empty()){
            m_impl->videoEvents.clear();m_impl->videoEventBytes=0;
        } else if(m_impl->events.empty()) for(unsigned i=0;i<16&&!m_impl->videoEvents.empty();++i){
            m_impl->videoEventBytes-=m_impl->videoEvents.front().body.size();media.push_back(std::move(m_impl->videoEvents.front()));m_impl->videoEvents.pop_front();
        }
    }
    for(const auto& signal:media)if(videoScope(signal,m_impl->status(),true))m_impl->adapter.onVideoSignal(signal);
    std::lock_guard lock(m_impl->mutex);
    if (!m_impl->events.empty() || !m_impl->playerEvents.empty() || m_impl->windowEvent || !m_impl->videoEvents.empty()) signalFd(m_impl->eventFd);
    // done and the final queued snapshot are published under this same mutex.
    // The caller can now remove the event source and reap without losing final-state delivery.
    return m_impl->done.load(std::memory_order_acquire) && m_impl->events.empty();
}
bool Multiplayer::reap() noexcept {
    if (!finished()) return false;
    if (m_impl->worker.joinable()) m_impl->worker.join();
    m_impl->closeLocalFds();
    return true;
}
void Multiplayer::shutdown() noexcept {
    m_impl->abortRequested = true; signalFd(m_impl->wakeFd);
    if (m_impl->worker.joinable()) m_impl->worker.join();
    m_impl->closeLocalFds(); m_impl->done = true;
    std::lock_guard lock(m_impl->mutex);
    m_impl->events.clear(); m_impl->commands.clear(); m_impl->playerEvents.clear(); m_impl->localPlayer.reset();
    m_impl->videoCommands.clear();m_impl->videoEvents.clear();m_impl->videoCommandBytes=m_impl->videoEventBytes=0;
    m_impl->windowSubmission.reset(); m_impl->windowEvent.reset(); m_impl->snapshot.publishedWindows=m_impl->snapshot.remoteWindows=0;
    m_impl->snapshot.udp = {}; m_impl->snapshot.world = WorldState::Disabled; m_impl->worldConfirmation.reset();
    m_impl->snapshot.state = State::Off; m_impl->snapshot.workerRunning = false;
}
} // namespace hyprwalk::mp
