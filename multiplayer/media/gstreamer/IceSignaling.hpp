#pragma once

// Each immutable VideoTrackKey has one video m-line (0). Pure bounded signaling
// policy: no GStreamer, sockets, threads or native handles.
#include <array>
#include <charconv>
#include <cstdint>
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace hyprwalk::mp::media::detail {
inline constexpr std::size_t MaxIceBody = 2048;
inline constexpr unsigned MaxLocalIceCandidates = 64;
inline constexpr unsigned MaxRemoteIceCandidates = 128;

struct IceCandidate {
    unsigned mline = 0;
    std::string candidate; // Empty is end-of-candidates, NOT an invalid candidate.
    bool end() const noexcept { return candidate.empty(); }
    std::string wire() const { return std::to_string(mline) + "|" + candidate; }
};

inline bool iceNumber(std::string_view text, std::uint32_t low, std::uint32_t high) {
    std::uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return !text.empty() && result.ec == std::errc{} && result.ptr == text.data() + text.size() && value >= low && value <= high;
}

inline IceCandidate parseIce(std::string_view body) {
    if (body.size() > MaxIceBody)
        throw std::runtime_error("ICE message exceeds 2048 bytes");
    // This video-only peer has exactly one media section.
    if (body.size() < 2 || body[0] != '0' || body[1] != '|')
        throw std::runtime_error("invalid ICE mline: only m-line 0 (video) is supported");
    constexpr unsigned mline = 0;
    auto candidate = body.substr(2);
    if (candidate.empty())
        return {mline, {}};
    // Accept the SDP-attribute spelling at the boundary; emit canonical candidate:.
    if (candidate.starts_with("a=candidate:"))
        candidate.remove_prefix(2);
    if (!candidate.starts_with("candidate:"))
        throw std::runtime_error("invalid ICE candidate prefix");
    for (const unsigned char ch : candidate)
        if (ch < 0x20 || ch > 0x7e || ch == '|')
            throw std::runtime_error("invalid ICE candidate text");

    // Validate the candidate grammar, not the network/interface. IPv4, IPv6 and
    // mDNS/FQDN address tokens are left to libnice; do not filter VPN/WARP routes.
    std::array<std::string_view, 128> fields{};
    std::size_t count = 0, at = 0;
    while (at < candidate.size()) {
        while (at < candidate.size() && candidate[at] == ' ') ++at;
        if (at == candidate.size()) break;
        const auto end = candidate.find(' ', at);
        if (count == fields.size()) throw std::runtime_error("too many ICE fields");
        const auto length = end == std::string_view::npos ? candidate.size() - at : end - at;
        fields[count++] = candidate.substr(at, length);
        at += length;
    }
    if (count < 8 || (count - 8) % 2 != 0 || fields[0].size() <= 10 || fields[0].size() > 42 ||
        !iceNumber(fields[1], 1, 256) ||
        (fields[2] != "UDP" && fields[2] != "udp" && fields[2] != "TCP" && fields[2] != "tcp") ||
        !iceNumber(fields[3], 1, UINT32_MAX) || fields[4].empty() || fields[4].size() > 253 ||
        !iceNumber(fields[5], 1, 65535) || fields[6] != "typ" ||
        (fields[7] != "host" && fields[7] != "srflx" && fields[7] != "prflx" && fields[7] != "relay"))
        throw std::runtime_error("invalid ICE candidate fields");
    for (const char ch : fields[0].substr(10))
        if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '+' || ch == '/'))
            throw std::runtime_error("invalid ICE candidate foundation");
    return {mline, std::string(candidate)};
}

// Owned per Track. Local use is under iceMutex; remote use is worker-only.
// Ready is set only after the corresponding SDP operation succeeds. End markers
// have one reserved slot, do not consume candidate quota, and are idempotent.
class IceQueue {
public:
    explicit IceQueue(unsigned limit) : m_limit(limit) {}
    bool push(std::string_view body) {
        if (m_stopped) return false;
        auto item = parseIce(body);
        if (m_endSeen[item.mline]) return false; // EOC is scoped to a single m-line
        if (item.end()) {
            m_pending.push_back(std::move(item));
            m_endSeen[item.mline] = true;
        } else {
            if (m_count == m_limit) throw std::runtime_error("ICE candidate limit exceeded");
            m_pending.push_back(std::move(item));
            ++m_count;
        }
        return true;
    }
    void ready() noexcept { if (!m_stopped) m_ready = true; }
    std::optional<IceCandidate> pop() {
        if (m_stopped || !m_ready || m_pending.empty()) return std::nullopt;
        auto item = std::move(m_pending.front());
        m_pending.pop_front();
        if (item.end()) m_endDelivered[item.mline] = true;
        return item;
    }
    void stop() noexcept {
        m_stopped = true;
        m_ready = false;
        m_pending.clear();
    }
    std::size_t pending() const noexcept { return m_pending.size(); }
    unsigned candidates() const noexcept { return m_count; }
    bool endSeen() const noexcept { return m_endSeen[0] || m_endSeen[1]; }
    bool endDelivered() const noexcept { return m_endDelivered[0] || m_endDelivered[1]; }
    bool endSeen(unsigned mline) const noexcept { return mline < 2 && m_endSeen[mline]; }
    bool endDelivered(unsigned mline) const noexcept { return mline < 2 && m_endDelivered[mline]; }
private:
    unsigned m_limit, m_count = 0;
    bool m_ready = false, m_stopped = false;
    std::array<bool, 2> m_endSeen{}, m_endDelivered{};
    std::deque<IceCandidate> m_pending;
};
} // namespace hyprwalk::mp::media::detail
