#include <hyprwalk/mp/OnlineMenu.hpp>

#include <arpa/inet.h>
#include <algorithm>
#include <charconv>
#include <limits>
#include <utility>

namespace hyprwalk::mp {
namespace {
std::string endpointText(const Endpoint& e) {
    if (e.ip.empty()) return "Not connected";
    return (e.ip.find(':') == std::string::npos ? e.ip : "[" + e.ip + "]") + ":" + std::to_string(e.port);
}
OnlineItem info(std::string label, std::string hint, std::string detail) {
    return {std::move(label), std::move(hint), {}, std::move(detail), OnlineAction::Details};
}
std::string errorText(const Status& s, const std::string& local) {
    std::string out;
    const auto add = [&](const char* name, const std::string& text) {
        if (text.empty()) return;
        if (!out.empty()) out += "\n\n";
        out += name; out += text;
    };
    add("Command: ", local);
    add("TCP: ", s.lastError);
    add("UDP: ", s.udp.error);
    add("World: ", s.worldError);
    if (!out.empty()) return out;
    // Do not manufacture a generic TCP/UDP/Map warning when there is no recorded
    // error. Show the three real state values so the user can see which gate is not ready.
    return std::string("No explicit error was reported.\n\nTCP: ") + stateName(s.state) +
        "\nUDP: " + udpStateName(s.udp.state) + "\nMap: " + worldStateName(s.world) +
        "\n\nWindow sharing needs a live UDP route and a synchronized map.";
}
std::string syncText(const Status& s) {
    return std::string("TCP: ") + stateName(s.state) + "\nUDP: " + udpStateName(s.udp.state) +
        "\nMap: " + worldStateName(s.world) + "\n\nPlayers can share the world only when UDP and Map are ready.";
}
std::string bounded(std::string_view s, std::size_t max) {
    std::size_t end = std::min(s.size(), max);
    while (end > 0 && end < s.size() && (static_cast<unsigned char>(s[end]) & 0xc0U) == 0x80U) --end;
    return std::string(s.substr(0, end));
}
} // namespace

bool OnlineMenu::busy(const Status& s) noexcept {
    return s.workerRunning || (s.state != State::Off && s.state != State::Error);
}
bool OnlineMenu::onlinePath(std::string_view path) noexcept {
    // Match complete menu path components, never a substring in a window or avatar name.
    while (!path.empty()) {
        const auto slash = path.find('/');
        const auto part = path.substr(0, slash);
        if (part == "online" || part == "online-status" || part == "online-players" || part == "online-windows" || part.starts_with("online-windows:")) return true;
        if (slash == std::string_view::npos) break;
        path.remove_prefix(slash + 1);
    }
    return false;
}
std::string OnlineMenu::validateAddress(std::string_view ip) {
    if (ip.empty() || ip.size() > 45 || ip.find('\0') != std::string_view::npos)
        return "Enter a numeric IPv4 or IPv6 address (1..45 characters).";
    const std::string text(ip);
    in_addr v4{}; in6_addr v6{};
    if (::inet_pton(AF_INET, text.c_str(), &v4) != 1 && ::inet_pton(AF_INET6, text.c_str(), &v6) != 1)
        return "Invalid IP. Use a numeric IPv4/IPv6 address without a hostname, brackets or %scope.";
    return {};
}
std::string OnlineMenu::validatePort(std::string_view text, Role role) {
    unsigned p = 0;
    if (text.empty()) return "Enter a port number.";
    const auto r = std::from_chars(text.data(), text.data() + text.size(), p);
    if (text.size() > 5 || r.ec != std::errc{} || r.ptr != text.data() + text.size() ||
        p > 65535 || (p == 0 && role != Role::Host))
        return role == Role::Host ? "Host port must be 0..65535. Zero chooses a free port; read it in Status." :
                                    "Join port must be 1..65535.";
    return {};
}
std::string OnlineMenu::toggleRole(const Status& s) {
    if (busy(s)) return "Disconnect and wait for off before changing the connection form.";
    m_mode = m_mode == Role::Host ? Role::Client : Role::Host;
    m_error.clear();
    return {};
}
std::string OnlineMenu::setField(OnlineField field, const std::string& value, const Status& s) {
    if (busy(s)) return "The session started while editing. Disconnect before changing this field.";
    const std::string error = field == OnlineField::Address ? validateAddress(value) :
                              field == OnlineField::Port ? validatePort(value, m_mode) : "This field is read-only.";
    if (!error.empty()) return error;
    if (field == OnlineField::Address) m_address = value;
    else m_port = value;
    m_error.clear();
    return {};
}
std::optional<OnlineRequest> OnlineMenu::request(OnlineAction action, const Status& s, std::string& error) const {
    error.clear();
    if (action == OnlineAction::Disconnect) {
        if (!busy(s)) { error = "No active session to disconnect."; return {}; }
        return OnlineRequest{Role::None, {}, true};
    }
    if (action != OnlineAction::Start) { error = "Not a connection action."; return {}; }
    if (busy(s)) { error = "Session active. Disconnect and wait for off before connecting again."; return {}; }
    if (!(error = validateAddress(m_address)).empty() || !(error = validatePort(m_port, m_mode)).empty()) return {};
    unsigned port = 0;
    const auto parsed = std::from_chars(m_port.data(), m_port.data() + m_port.size(), port);
    if (parsed.ec != std::errc{}) { error = "Invalid port."; return {}; }
    return OnlineRequest{m_mode, {m_address, static_cast<std::uint16_t>(port)}, false};
}
void OnlineMenu::result(std::string_view result) {
    if (result.starts_with("error: ")) m_error = bounded(result.substr(7), 1024);
    else if (result.starts_with("ok:")) m_error.clear();
}
OnlinePage OnlineMenu::page(std::string_view id, const Status& s, std::string_view name) const {
    const bool active = busy(s);
    const bool peer = s.state == State::Connected && !s.peerName.empty();
    const OnlineItem leave{"Disconnect", active ? "Leave this session" : "No session", {}, {}, OnlineAction::Disconnect, !active};
    const auto errors = errorText(s, m_error);
    const auto where = endpointText(s.endpoint);
    if (id == "online") {
        OnlinePage p{"Online", {
            {"Mode", m_mode == Role::Host ? "Host" : "Join", {}, {}, OnlineAction::ToggleRole, active, m_mode == Role::Host},
            {m_mode == Role::Host ? "Bind IP" : "Server IP", m_address, {}, {}, OnlineAction::EditAddress, active},
            {"Port", m_port, {}, {}, OnlineAction::EditPort, active},
            {m_mode == Role::Host ? "Create Server" : "Connect", active ? stateName(s.state) : "Start connection", {}, {}, OnlineAction::Start, active},
            leave,
            {"Status", stateName(s.state), "online-status", {}, OnlineAction::None},
            {"Players", peer ? s.peerName : "No remote player", "online-players", {}, OnlineAction::None},
            info("Errors", (!m_error.empty() || !s.lastError.empty() || !s.udp.error.empty() || !s.worldError.empty()) ? "View error details" : "None", errors),
            {"Windows", "Sharing off by default", "online-windows", {}, OnlineAction::None}
        }};
        return p;
    }
    if (id == "online-status") {
        return {"Connection", {
            info("TCP", stateName(s.state), "TCP: " + std::string(stateName(s.state)) + "\nRole: " + roleName(s.role) +
                 "\nProtocol: " + std::to_string(s.version) + "\nCapabilities: " + std::to_string(s.capabilities) +
                 "\n\nTest sent: " + std::to_string(s.testsSent) + "\nReceived: " + std::to_string(s.testsReceived) +
                 "\nAcknowledged: " + std::to_string(s.testsAcknowledged)),
            info("UDP", udpStateName(s.udp.state), "UDP: " + std::string(udpStateName(s.udp.state)) +
                 "\nLocal: " + endpointText(s.udp.local) + "\nPeer: " + endpointText(s.udp.peer) +
                 "\n\nPlayer sent: " + std::to_string(s.udp.playerSent) + "\nPlayer received: " + std::to_string(s.udp.playerReceived) +
                 "\n\n" + s.udp.error),
            info("Map", worldStateName(s.world), syncText(s) + (s.worldError.empty() ? "" : "\n\n" + s.worldError)),
            info("Server", where, "Server / bound address:\n" + where +
                 "\n\n127.0.0.1 / ::1 only reach this machine. To connect two machines, bind the host to its LAN/VPN IP and enter that IP on the client.\n\nTCP is not encrypted. Use a trusted LAN or VPN."),
            info("Peer", peer ? s.peerName : "Not connected", peer ? s.peerName + "\n\nID: " + idString(s.peerId) : "No remote player connected."),
            info("Ping", "Not measured", "Ping is not measured by this version.\n\nTCP/UDP ready and packet counters are not latency measurements."),
            info("Session", active ? "View IDs" : "No active session", "Session: " + idString(s.session) + "\n\nLocal: " + idString(s.localId) + "\n\nPeer: " + idString(s.peerId)),
            leave
        }};
    }
    if (id == "online-players") {
        return {"Players", {
            info("Local player", s.localName.empty() ? std::string(name) : s.localName,
                 "Local player: " + (s.localName.empty() ? std::string(name) : s.localName) + "\nRole: " + roleName(s.role) + "\n\n" + syncText(s)),
            info(peer ? "Remote player" : "No remote player", peer ? s.peerName : "Create or join a session",
                 peer ? "Remote player: " + s.peerName + "\n\n" + syncText(s) : "No remote player connected.\n\nReturn to Online to create or join a session."),
            info("Synchronization", worldStateName(s.world), syncText(s)),
            info("Ping", "Not measured", "No RTT measurement is implemented. No estimated or fabricated ping is displayed."),
            leave
        }};
    }
    return {};
}

std::string OnlineMenu::details(std::string_view key, const Status& status, std::string_view localName) const {
    const auto split = key.find('#');
    if (split == std::string_view::npos) return "No details available.";
    const auto view = page(key.substr(0, split), status, localName);
    const auto label = key.substr(split + 1);
    for (const auto& row : view.items)
        if (row.label == label) return row.detail;
    return "This item is no longer available. Return to the previous page.";
}

OnlineEditor::OnlineEditor(OnlineField f, std::string t, std::string v) : field(f), title(std::move(t)), value(std::move(v)) {
    value = bounded(value, readOnly() ? std::size_t{4096} : field == OnlineField::Address ? std::size_t{45} : std::size_t{5});
    caret = value.size(); selected = !readOnly();
}
void OnlineEditor::clearSelection() {
    if (selected) { value.clear(); caret = 0; selected = false; }
}
bool OnlineEditor::insert(char c) {
    const bool digit = c >= '0' && c <= '9';
    if (!digit && !(field == OnlineField::Address && ((c >= 'a' && c <= 'f') || c == '.' || c == ':'))) return false;
    const std::size_t limit = field == OnlineField::Address ? 45 : 5;
    if (!selected && value.size() >= limit) return false;
    clearSelection();
    value.insert(caret, 1, c); ++caret; error.clear();
    return true;
}
void OnlineEditor::scroll(int steps) noexcept {
    if (readOnly()) {
        const auto delta = std::clamp(steps, -256, 256);
        scrollLine = std::clamp(scrollLine + delta, 0, std::clamp(scrollLimit, 0, 256));
    }
}
std::string OnlineEditor::displayValue() const {
    if (readOnly()) return value;
    std::string display = value;
    display.insert(std::min(caret, value.size()), "|");
    return display;
}
EditorResult OnlineEditor::key(std::uint32_t k, bool shift, bool ctrl, bool alt, bool meta) {
    (void)shift; // Address keys are layout independent; semicolon always enters a colon.
    if (k == 1) return EditorResult::Cancel; // Escape
    if (alt || meta) return EditorResult::Ignored;
    if (readOnly()) {
        if (k == 28 || k == 96 || k == 14) return EditorResult::Cancel;
        if (k == 103 || k == 104) { scroll(k == 103 ? -1 : -8); return EditorResult::Changed; }
        if (k == 108 || k == 109) { scroll(k == 108 ? 1 : 8); return EditorResult::Changed; }
        return EditorResult::Ignored;
    }
    if (ctrl) {
        if (k == 30) { selected = true; caret = value.size(); return EditorResult::Changed; } // Ctrl+A
        return EditorResult::Ignored; // No clipboard or compositor shortcuts in a field.
    }
    switch (k) {
        case 28: case 96: return EditorResult::Accept;
        case 105: caret = selected ? 0 : caret > 0 ? caret - 1 : 0; selected = false; return EditorResult::Changed;
        case 106: caret = selected ? value.size() : std::min(caret + 1, value.size()); selected = false; return EditorResult::Changed;
        case 102: caret = 0; selected = false; return EditorResult::Changed;
        case 107: caret = value.size(); selected = false; return EditorResult::Changed;
        case 14:
            if (selected) clearSelection();
            else if (caret > 0) value.erase(--caret, 1);
            error.clear(); return EditorResult::Changed;
        case 111:
            if (selected) clearSelection();
            else if (caret < value.size()) value.erase(caret, 1);
            error.clear(); return EditorResult::Changed;
        default: break;
    }
    char ch = 0;
    if (k >= 2 && k <= 10) ch = static_cast<char>('1' + k - 2);
    else if (k == 11 || k == 82) ch = '0';
    else if (k == 79) ch = '1'; else if (k == 80) ch = '2'; else if (k == 81) ch = '3';
    else if (k == 75) ch = '4'; else if (k == 76) ch = '5'; else if (k == 77) ch = '6';
    else if (k == 71) ch = '7'; else if (k == 72) ch = '8'; else if (k == 73) ch = '9';
    else if (k == 52 || k == 83) ch = '.';
    else if (k == 39) ch = ':';
    else if (k == 30) ch = 'a'; else if (k == 48) ch = 'b'; else if (k == 46) ch = 'c';
    else if (k == 32) ch = 'd'; else if (k == 18) ch = 'e'; else if (k == 33) ch = 'f';
    return ch && insert(ch) ? EditorResult::Changed : EditorResult::Ignored;
}
} // namespace hyprwalk::mp
