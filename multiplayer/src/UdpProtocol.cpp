#include <hyprwalk/mp/UdpProtocol.hpp>
#include <hyprwalk/mp/Protocol.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace hyprwalk::mp {
bool validPlayerSnapshot(const PlayerSnapshot& p) noexcept {
    const auto bounded = [](float f, float limit) { return std::isfinite(f) && std::abs(f) <= limit; };
    return bounded(p.feet.x, 1.e6f) && bounded(p.feet.y, 1.e6f) && bounded(p.feet.z, 1.e6f) &&
           bounded(p.velocity.x, 1.e4f) && bounded(p.velocity.y, 1.e4f) && bounded(p.velocity.z, 1.e4f) &&
           bounded(p.yaw, 1.e6f) && bounded(p.bodyYaw, 1.e6f) && bounded(p.pitch, 3.141593f);
}
namespace udpwire {
namespace {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
constexpr std::uint32_t Magic = 0x48575544; // HWUD
struct Writer {
    std::vector<std::uint8_t> bytes;
    void integer(std::uint64_t v, unsigned n) {
        for (unsigned i = n; i; --i) bytes.push_back(static_cast<std::uint8_t>(v >> ((i - 1) * 8)));
    }
    void id(const Id& id) { bytes.insert(bytes.end(), id.begin(), id.end()); }
    void real(float f) { integer(std::bit_cast<std::uint32_t>(f), 4); }
    void vec(const Vec3& v) { real(v.x); real(v.y); real(v.z); }
};
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t at = 0;
    std::uint64_t integer(unsigned n) {
        if (n > bytes.size() - at) throw std::runtime_error("truncated UDP field");
        std::uint64_t v = 0;
        while (n--) v = (v << 8) | bytes[at++];
        return v;
    }
    Id id() { Id v{}; for (auto& b : v) b = static_cast<std::uint8_t>(integer(1)); return v; }
    float real() { return std::bit_cast<float>(static_cast<std::uint32_t>(integer(4))); }
    Vec3 vec() { return {real(), real(), real()}; }
};
std::size_t payloadSize(Type type) {
    switch (type) {
        case Type::Probe: case Type::Heartbeat: return 0;
        case Type::ProbeAck: return 4;
        case Type::Player: return PlayerSize;
    }
    throw std::invalid_argument("unknown UDP packet type");
}
}
bool decode(std::span<const std::uint8_t> bytes, Packet& out, std::string& error) {
    error.clear();
    try {
        if (bytes.size() < HeaderSize || bytes.size() > MaxDatagram) throw std::runtime_error("invalid datagram size");
        Reader r{bytes};
        if (r.integer(4) != Magic) throw std::runtime_error("bad UDP magic");
        if (r.integer(2) != Version) throw std::runtime_error("unsupported UDP version");
        Packet p; p.type = static_cast<Type>(r.integer(2));
        const auto length = r.integer(2);
        if (length != payloadSize(p.type) || bytes.size() != HeaderSize + length)
            throw std::runtime_error("UDP payload length mismatch");
        if (r.integer(2) != 0) throw std::runtime_error("nonzero UDP reserved flags");
        p.sequence = static_cast<std::uint32_t>(r.integer(4)); p.senderTimeUs = r.integer(8);
        if (p.senderTimeUs > static_cast<std::uint64_t>(INT64_MAX)) throw std::runtime_error("invalid UDP timestamp");
        p.session = r.id(); p.sender = r.id(); p.target = r.id(); p.token = r.id();
        if (!wire::nonzero(p.session) || !wire::nonzero(p.sender) || !wire::nonzero(p.target) ||
            !wire::nonzero(p.token) || p.sender == p.target) throw std::runtime_error("invalid UDP identifiers");
        if (p.type == Type::ProbeAck) p.acknowledgement = static_cast<std::uint32_t>(r.integer(4));
        if (p.type == Type::Player) {
            auto& v = p.player;
            v.feet = r.vec(); v.velocity = r.vec(); v.yaw = r.real(); v.pitch = r.real(); v.bodyYaw = r.real();
            const auto flags = r.integer(2);
            if ((flags & ~31U) || r.integer(2) != 0) throw std::runtime_error("invalid player flags");
            v.active = flags & 1; v.onGround = flags & 2; v.running = flags & 4;
            v.crouched = flags & 8; v.flying = flags & 16;
            v.teleport = static_cast<std::uint32_t>(r.integer(4));
            if (!validPlayerSnapshot(v)) throw std::runtime_error("nonfinite or out-of-range player state");
        }
        out = p; return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
std::vector<std::uint8_t> encode(const Packet& p) {
    Writer w;
    w.bytes.reserve(MaxDatagram);
    w.integer(Magic, 4); w.integer(Version, 2); w.integer(static_cast<std::uint16_t>(p.type), 2);
    w.integer(payloadSize(p.type), 2); w.integer(0, 2); w.integer(p.sequence, 4); w.integer(p.senderTimeUs, 8);
    w.id(p.session); w.id(p.sender); w.id(p.target); w.id(p.token);
    if (p.type == Type::ProbeAck) w.integer(p.acknowledgement, 4);
    if (p.type == Type::Player) {
        const auto& v = p.player;
        w.vec(v.feet); w.vec(v.velocity); w.real(v.yaw); w.real(v.pitch); w.real(v.bodyYaw);
        w.integer((v.active ? 1 : 0) | (v.onGround ? 2 : 0) | (v.running ? 4 : 0) |
                  (v.crouched ? 8 : 0) | (v.flying ? 16 : 0), 2);
        w.integer(0, 2); w.integer(v.teleport, 4);
    }
    Packet check; std::string error;
    if (!decode(w.bytes, check, error)) throw std::invalid_argument("cannot encode UDP: " + error);
    return std::move(w.bytes);
}
bool newer(std::uint32_t a, std::uint32_t b) noexcept {
    const auto delta = a - b;
    return delta != 0 && delta < 0x80000000U;
}
SequenceWindow::Result SequenceWindow::accept(std::uint32_t sequence) noexcept {
    if (!m_initialized) { m_initialized = true; m_latest = sequence; m_seen = 1; return Result::Newest; }
    if (newer(sequence, m_latest)) {
        const auto step = sequence - m_latest;
        m_gaps += step - 1; // Provisional gaps, NOT a claim of final packet loss.
        m_seen = step >= 64 ? 1 : (m_seen << step) | 1;
        m_latest = sequence; return Result::Newest;
    }
    const auto age = m_latest - sequence;
    if (age >= 64) return Result::TooOld;
    const std::uint64_t bit = std::uint64_t{1} << age;
    if (m_seen & bit) return Result::Duplicate;
    m_seen |= bit; return Result::Reordered;
}
} // namespace udpwire
} // namespace hyprwalk::mp
