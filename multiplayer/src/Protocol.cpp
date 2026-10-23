#include <hyprwalk/mp/Protocol.hpp>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace hyprwalk::mp::wire {
namespace {
constexpr std::uint32_t Magic = 0x48574d50U; // HWMP
struct Writer {
    std::vector<std::uint8_t> bytes;
    void integer(std::uint64_t v, unsigned n) {
        for (unsigned i = n; i > 0; --i) bytes.push_back(static_cast<std::uint8_t>(v >> ((i - 1) * 8)));
    }
    void id(const Id& v) { bytes.insert(bytes.end(), v.begin(), v.end()); }
    void text(const std::string& v) {
        integer(v.size(), 2);
        bytes.insert(bytes.end(), v.begin(), v.end());
    }
};
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t at = 0;
    std::uint64_t integer(unsigned n) {
        if (at + n > bytes.size()) throw std::runtime_error("truncated field");
        std::uint64_t v = 0;
        while (n--) v = (v << 8) | bytes[at++];
        return v;
    }
    Id id() {
        Id v{};
        for (auto& b : v) b = static_cast<std::uint8_t>(integer(1));
        if (!nonzero(v)) throw std::runtime_error("zero identifier");
        return v;
    }
    std::string text(std::size_t maximum) {
        const auto n = static_cast<std::size_t>(integer(2));
        if (n > maximum || at + n > bytes.size()) throw std::runtime_error("invalid string length");
        std::string s(bytes.begin() + at, bytes.begin() + at + n);
        at += n;
        if (!printable(s, maximum)) throw std::runtime_error("invalid text: printable ASCII required");
        return s;
    }
    std::uint64_t sequence() {
        const auto n = integer(8);
        if (!n) throw std::runtime_error("zero sequence");
        return n;
    }
};
bool known(std::uint64_t n) { return n >= 1 && n <= 16; }
} // namespace

bool nonzero(const Id& id) noexcept {
    return std::any_of(id.begin(), id.end(), [](auto v) { return v != 0; });
}
bool printable(const std::string& text, std::size_t maximum) noexcept {
    if (text.empty() || text.size() > maximum) return false;
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= 32 && c <= 126; });
}

bool decode(const Frame& frame, Message& out, std::string& error) {
    error.clear();
    try {
        Reader r{frame.payload};
        Message m;
        m.type = frame.type;
        switch (m.type) {
            case Type::Video: m.videoChunk.assign(r.bytes.begin(),r.bytes.end()); r.at=r.bytes.size(); break;
            case Type::Windows:
                if (!decodeWindows(r.bytes, m.windows, error)) return false;
                r.at = r.bytes.size(); break;
            case Type::Hello:
                m.minVersion = static_cast<std::uint16_t>(r.integer(2));
                m.maxVersion = static_cast<std::uint16_t>(r.integer(2));
                if (m.minVersion == 0 || m.minVersion > m.maxVersion) throw std::runtime_error("invalid version range");
                m.capabilities = static_cast<std::uint32_t>(r.integer(4));
                m.participant = r.id();
                m.text = r.text(64);
                break;
            case Type::Welcome:
                m.selectedVersion = static_cast<std::uint16_t>(r.integer(2));
                m.capabilities = static_cast<std::uint32_t>(r.integer(4));
                m.session = r.id();
                m.participant = r.id();
                m.echo = r.id();
                m.text = r.text(64);
                break;
            case Type::Ready: case Type::ReadyAck:
                m.session = r.id(); m.participant = r.id(); break;
            case Type::UdpOffer:
                m.session = r.id(); m.participant = r.id();
                m.udpPort = static_cast<std::uint16_t>(r.integer(2));
                m.udpVersion = static_cast<std::uint16_t>(r.integer(2));
                m.udpToken = r.id();
                if (!m.udpPort || !m.udpVersion) throw std::runtime_error("invalid UDP offer");
                break;
            case Type::MapOffer: {
                m.session = r.id(); m.participant = r.id(); m.worldTicket = r.id();
                std::string why;
                if (!decodeMap(r.bytes.subspan(r.at), m.map, why)) throw std::runtime_error(why);
                r.at += MapDescriptorSize; break;
            }
            case Type::MapReady: case Type::MapReject:
                m.session = r.id(); m.participant = r.id(); m.worldTicket = r.id();
                if (m.type == Type::MapReject) m.text = r.text(256);
                break;
            case Type::Test:
                m.sequence = r.sequence(); m.text = r.text(MaxText); break;
            case Type::TestAck: case Type::Ping: case Type::Pong:
                m.sequence = r.sequence(); break;
            case Type::Bye: case Type::Error:
                m.text = r.text(256); break;
            default: throw std::runtime_error("unknown message type");
        }
        if (r.at != r.bytes.size()) throw std::runtime_error("trailing payload bytes");
        out = std::move(m);
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

std::vector<std::uint8_t> encode(const Message& m) {
    Writer p;
    switch (m.type) {
        case Type::Video: p.bytes = m.videoChunk; break;
        case Type::Windows: p.bytes = encodeWindows(m.windows); break;
        case Type::Hello:
            p.integer(m.minVersion, 2); p.integer(m.maxVersion, 2); p.integer(m.capabilities, 4);
            p.id(m.participant); p.text(m.text); break;
        case Type::Welcome:
            p.integer(m.selectedVersion, 2); p.integer(m.capabilities, 4);
            p.id(m.session); p.id(m.participant); p.id(m.echo); p.text(m.text); break;
        case Type::Ready: case Type::ReadyAck:
            p.id(m.session); p.id(m.participant); break;
        case Type::UdpOffer:
            p.id(m.session); p.id(m.participant); p.integer(m.udpPort, 2);
            p.integer(m.udpVersion, 2); p.id(m.udpToken); break;
        case Type::MapOffer: {
            p.id(m.session); p.id(m.participant); p.id(m.worldTicket);
            const auto descriptor = encodeMap(m.map);
            p.bytes.insert(p.bytes.end(), descriptor.begin(), descriptor.end()); break;
        }
        case Type::MapReady: case Type::MapReject:
            p.id(m.session); p.id(m.participant); p.id(m.worldTicket);
            if (m.type == Type::MapReject) p.text(m.text);
            break;
        case Type::Test:
            p.integer(m.sequence, 8); p.text(m.text); break;
        case Type::TestAck: case Type::Ping: case Type::Pong:
            p.integer(m.sequence, 8); break;
        case Type::Bye: case Type::Error:
            p.text(m.text); break;
        default: throw std::invalid_argument("unknown message type");
    }
    Message check;
    std::string error;
    if (p.bytes.size() > MaxPayload || !decode({m.type, p.bytes}, check, error))
        throw std::invalid_argument("cannot encode: " + error);
    Writer frame;
    frame.integer(Magic, 4); frame.integer(ProtocolVersion, 2);
    frame.integer(static_cast<std::uint16_t>(m.type), 2);
    frame.integer(p.bytes.size(), 4); frame.integer(0, 4);
    frame.bytes.insert(frame.bytes.end(), p.bytes.begin(), p.bytes.end());
    return std::move(frame.bytes);
}

bool Decoder::feed(std::span<const std::uint8_t> bytes, const Visitor& visitor, std::string& error) {
    error.clear();
    if (m_failed) { error = "decoder already failed"; return false; }
    while (!bytes.empty()) {
        const auto n = std::min(bytes.size(), m_expected - m_buffer.size());
        m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.begin() + n);
        bytes = bytes.subspan(n);
        if (m_buffer.size() < m_expected) continue;
        if (m_expected == HeaderSize) {
            Reader r{m_buffer};
            const auto magic = r.integer(4), version = r.integer(2), type = r.integer(2);
            const auto length = r.integer(4), flags = r.integer(4);
            if (magic != Magic) error = "bad magic";
            else if (version != ProtocolVersion) error = "unsupported envelope version";
            else if (!known(type)) error = "unknown message type";
            else if (length == 0 || length > MaxPayload) error = "invalid payload size";
            else if (flags != 0) error = "nonzero reserved flags";
            if (!error.empty()) { m_failed = true; return false; }
            m_type = static_cast<Type>(type);
            m_expected = HeaderSize + static_cast<std::size_t>(length);
            continue;
        }
        Frame f{m_type, {m_buffer.begin() + HeaderSize, m_buffer.end()}};
        m_buffer.clear();
        m_expected = HeaderSize;
        ++m_completed;
        if (!visitor(std::move(f))) return false;
    }
    return true;
}

void WriteQueue::discardUnsent(Type type) {
    for (auto it=m_frames.begin(); it!=m_frames.end();) {
        const auto& f=*it;
        if (!(it==m_frames.begin() && m_offset) && f.size()>=HeaderSize &&
            ((static_cast<unsigned>(f[6])<<8)|f[7])==static_cast<unsigned>(type)) {
            m_bytes-=f.size(); it=m_frames.erase(it);
        } else ++it;
    }
}
bool WriteQueue::push(std::vector<std::uint8_t> frame) {
    if (frame.empty() || frame.size() > MaxQueuedBytes - m_bytes) return false;
    m_bytes += frame.size(); m_frames.push_back(std::move(frame)); return true;
}
WriteQueue::Result WriteQueue::flush(const Send& send, std::string& error, std::size_t budget) {
    error.clear();
    // A bounded syscall count also prevents endless EINTR from starving shutdown.
    for (unsigned calls = 0; !m_frames.empty() && budget && calls < 64; ++calls) {
        auto& frame = m_frames.front();
        const auto count = std::min(budget, frame.size() - m_offset);
        const auto n = send(frame.data() + m_offset, count);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return Result::Pending;
            error = std::string("send: ") + std::strerror(errno); return Result::Failed;
        }
        if (n == 0 || static_cast<std::size_t>(n) > count) {
            error = "send returned invalid byte count"; return Result::Failed;
        }
        const auto used = static_cast<std::size_t>(n);
        m_offset += used; m_bytes -= used; budget -= used;
        if (m_offset == frame.size()) { m_frames.pop_front(); m_offset = 0; }
    }
    return empty() ? Result::Drained : Result::Pending;
}
} // namespace hyprwalk::mp::wire
