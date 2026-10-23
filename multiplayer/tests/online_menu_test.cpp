#include <hyprwalk/mp/OnlineMenu.hpp>
#include <hyprwalk/mp/Multiplayer.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace hyprwalk::mp;
using namespace std::chrono_literals;
namespace {
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + ": " #x); } while (0)
const OnlineItem& item(const OnlinePage& p, const std::string& label) {
    const auto i = std::find_if(p.items.begin(), p.items.end(), [&](const auto& row) { return row.label == label; });
    CHECK(i != p.items.end()); return *i;
}
std::size_t count(const char* dir) { return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator(dir), {})); }
void defaults() {
    const auto fds = count("/proc/self/fd"), threads = count("/proc/self/task");
    for (int i = 0; i < 100; ++i) {
        OnlineMenu m; CHECK(m.mode() == Role::Host && m.address() == "127.0.0.1" && m.port() == "45000");
        const auto page = m.page("online", {}, "egor"); CHECK(page.items.size() == 9);
        CHECK(item(page, "Disconnect").disabled && !item(page, "Create Server").disabled);
    }
    CHECK(fds == count("/proc/self/fd") && threads == count("/proc/self/task"));
}
void addresses() {
    for (const auto* ip : {"127.0.0.1", "192.168.1.10", "10.5.0.3", "0.0.0.0", "::", "::1", "2001:db8::ab", "::ffff:192.0.2.1"})
        CHECK(OnlineMenu::validateAddress(ip).empty());
    for (const auto* ip : {"", "localhost", "http://1.2.3.4", "1.2.3.999", "1.2.3", "[::1]", "fe80::1%eth0", "127.0.0.1 ", "1.2.3.4;bad", "01.2.3.4"})
        CHECK(!OnlineMenu::validateAddress(ip).empty());
    CHECK(!OnlineMenu::validateAddress(std::string("127.0.0.1\0junk", 14)).empty());
    CHECK(!OnlineMenu::validateAddress(std::string(4096, 'a')).empty());
}
void ports() {
    CHECK(OnlineMenu::validatePort("0", Role::Host).empty());
    CHECK(!OnlineMenu::validatePort("0", Role::Client).empty());
    for (const auto* p : {"1", "45000", "65535"}) CHECK(OnlineMenu::validatePort(p, Role::Client).empty());
    for (const auto* p : {"", "65536", "-1", "+5", "5 ", " 5", "1a", "123456789012345"}) CHECK(!OnlineMenu::validatePort(p, Role::Host).empty());
}
void saveAndRequest() {
    OnlineMenu m; std::string err;
    CHECK(m.setField(OnlineField::Address, "10.42.0.1", {}).empty());
    CHECK(m.setField(OnlineField::Port, "0", {}).empty());
    auto r = m.request(OnlineAction::Start, {}, err); CHECK(r && err.empty() && r->role == Role::Host && r->endpoint.port == 0);
    CHECK(m.toggleRole({}).empty()); CHECK(!m.request(OnlineAction::Start, {}, err) && !err.empty());
    CHECK(m.setField(OnlineField::Port, "45001", {}).empty());
    r = m.request(OnlineAction::Start, {}, err); CHECK(r && r->role == Role::Client && r->endpoint.ip == "10.42.0.1" && r->endpoint.port == 45001);
    CHECK(!m.setField(OnlineField::Address, "bad", {}).empty()); CHECK(m.address() == "10.42.0.1");
    CHECK(!m.setField(OnlineField::ReadOnly, "", {}).empty());
}
void busyStates() {
    for (const auto state : {State::Starting, State::Listening, State::Connecting, State::Handshaking, State::Connected, State::Stopping}) {
        OnlineMenu m; Status s; s.state = state; std::string err;
        CHECK(OnlineMenu::busy(s)); CHECK(!m.toggleRole(s).empty()); CHECK(!m.setField(OnlineField::Address, "1.2.3.4", s).empty());
        CHECK(!m.request(OnlineAction::Start, s, err)); CHECK(m.request(OnlineAction::Disconnect, s, err)->disconnect);
        const auto p = m.page("online", s, "stepan"); CHECK(item(p, "Create Server").disabled && !item(p, "Disconnect").disabled);
    }
    Status s; s.workerRunning = true; CHECK(OnlineMenu::busy(s));
    s.workerRunning = false; s.state = State::Error; CHECK(!OnlineMenu::busy(s));
}
void staleAction() {
    OnlineMenu m; Status s; const auto old = m.page("online", s, "egor"); CHECK(!item(old, "Create Server").disabled);
    s.state = State::Connecting; std::string err; CHECK(!m.request(item(old, "Create Server").action, s, err));
    CHECK(!m.setField(OnlineField::Port, "1234", s).empty() && m.port() == "45000");
}
void views() {
    OnlineMenu m; Status s; s.state = State::Connected; s.workerRunning = true; s.localName = "egor"; s.peerName = "stepan";
    s.udp.state = UdpState::Ready; s.world = WorldState::Ready; s.endpoint = {"2001:db8::1", 45000};
    s.testsAcknowledged = 1; s.udp.playerReceived = 34;
    const auto p = m.page("online-status", s, "fallback"); CHECK(p.items.size() == 8);
    CHECK(item(p, "UDP").hint == "ready" && item(p, "Map").hint == "ready");
    CHECK(item(p, "Server").detail.find("[2001:db8::1]:45000") != std::string::npos);
    CHECK(item(p, "UDP").detail.find("34") != std::string::npos);
    const auto players = m.page("online-players", s, "fallback");
    CHECK(item(players, "Local player").hint == "egor" && item(players, "Remote player").hint == "stepan");
    s.state = State::Listening; CHECK(item(m.page("online-players", s, "egor"), "No remote player").hint != "stepan");
    CHECK(m.page("other", {}, "egor").title.empty());
}
void errors() {
    OnlineMenu m; Status s; s.lastError = "connection refused"; s.udp.error = "UDP timeout"; s.worldError = "GLB mismatch";
    m.result("error: invalid endpoint"); const auto e = item(m.page("online", s, "egor"), "Errors").detail;
    for (const auto* text : {"connection refused", "UDP timeout", "GLB mismatch", "invalid endpoint"}) CHECK(e.find(text) != std::string::npos);
    m.result("ok: host requested"); CHECK(item(m.page("online", {}, "egor"), "Errors").hint == "None");
    CHECK(item(m.page("online", s, "egor"), "Errors").detail.find("connection refused") != std::string::npos);
}
void noFakePing() {
    OnlineMenu m;
    for (const auto* page : {"online-status", "online-players"}) {
        const auto row = item(m.page(page, {}, "egor"), "Ping"); CHECK(row.hint == "Not measured");
        CHECK(row.hint.find("ms") == std::string::npos);
    }
}
void paths() {
    for (const auto* p : {"online", "main/online", "main/online/online-status", "main/online/~Server IP", "main/online/online-players"})
        CHECK(OnlineMenu::onlinePath(p));
    for (const auto* p : {"", "main", "windows", "main/win:online", "main/avatars/online-person", "notonline"}) CHECK(!OnlineMenu::onlinePath(p));
}
void editorReplace() {
    OnlineEditor e(OnlineField::Address, "IP", "127.0.0.1"); CHECK(e.selected);
    CHECK(e.key(3) == EditorResult::Changed && e.value == "2");
    e.key(11); e.key(11); e.key(39); e.key(39); e.key(30);
    CHECK(e.value == "200::a" && e.caret == 6 && !e.selected);
    CHECK(e.key(28) == EditorResult::Accept); CHECK(e.key(1) == EditorResult::Cancel);
}
void editorNavigation() {
    OnlineEditor e(OnlineField::Port, "Port", "45000");
    e.key(105); CHECK(!e.selected && e.caret == 0); e.key(111); CHECK(e.value == "5000");
    e.key(107); CHECK(e.caret == 4); e.key(14); CHECK(e.value == "500");
    e.key(102); e.key(3); CHECK(e.value == "2500");
    e.key(106); e.key(14); CHECK(e.value == "200");
    e.key(30, false, true); CHECK(e.selected); e.key(14); CHECK(e.value.empty() && e.caret == 0);
}
void editorFilters() {
    OnlineEditor p(OnlineField::Port, "Port", "");
    for (auto k : {30U, 39U, 52U, 17U, 31U, 32U, 57U}) p.key(k);
    CHECK(p.value.empty());
    for (int i = 0; i < 20; ++i) { p.key(10); }
    CHECK(p.value == "99999");
    OnlineEditor ip(OnlineField::Address, "IP", "");
    for (int i = 0; i < 100; ++i) { ip.key(30); }
    CHECK(ip.value.size() == 45);
    ip.key(30, false, true); ip.key(2); CHECK(ip.value == "1");
}
void editorKeypad() {
    OnlineEditor e(OnlineField::Address, "IP", "");
    for (auto key : {79U, 80U, 81U, 75U, 76U, 77U, 71U, 72U, 73U, 82U, 83U}) e.key(key);
    CHECK(e.value == "1234567890.");
    CHECK(e.key(96) == EditorResult::Accept);
}
void editorShortcuts() {
    OnlineEditor e(OnlineField::Address, "IP", "127.0.0.1");
    CHECK(e.key(47, false, true) == EditorResult::Ignored); // Ctrl+V is not clipboard access.
    CHECK(e.key(30, false, false, true) == EditorResult::Ignored);
    CHECK(e.key(30, false, false, false, true) == EditorResult::Ignored);
    CHECK(e.value == "127.0.0.1" && e.selected);
    e.key(30, true); CHECK(e.value == "a");
}
void messageBounds() {
    OnlineEditor e(OnlineField::ReadOnly, "Error", std::string(10000, 'x'));
    CHECK(e.value.size() == 4096 && !e.selected);
    e.key(2); CHECK(e.value.size() == 4096); e.scroll(1000000000); CHECK(e.scrollLine == 256);
    e.scroll(-1000000000); CHECK(e.scrollLine == 0);
    e.key(109); CHECK(e.scrollLine == 8); e.key(104); CHECK(e.scrollLine == 0);
    CHECK(e.key(28) == EditorResult::Cancel);
}
void staleEditValidation() {
    OnlineMenu m; OnlineEditor e(OnlineField::Port, "Port", m.port());
    e.key(2); Status active; active.state = State::Connected;
    CHECK(!m.setField(e.field, e.value, active).empty()); CHECK(m.port() == "45000");
}
struct Adapter : IAdapter {
    std::string name; std::optional<WorldRequest> pending; int samples = 0;
    explicit Adapter(std::string n) : name(std::move(n)) {}
    LocalInfo localInfo() const override { return {name}; }
    std::optional<MapDescriptor> localMap() const override { MapDescriptor d; d.collision[0] = 1; return d; }
    void onState(const Status&) noexcept override {}
    void onDiagnostic(const Diagnostic&) noexcept override {}
    void onWorldRequest(const WorldRequest& r) noexcept override { pending = r; }
    void onPlayerSnapshot(const ReceivedPlayer&) noexcept override { ++samples; }
};
void wait(const std::function<bool()>& fn, Multiplayer& h, Multiplayer& c) {
    const auto end = std::chrono::steady_clock::now() + 4s;
    while (!fn()) { h.dispatch(); c.dispatch(); CHECK(std::chrono::steady_clock::now() < end); std::this_thread::sleep_for(1ms); }
    h.dispatch(); c.dispatch();
}
void realConnection(bool reverse) {
    Adapter a(reverse ? "stepan" : "egor"), b(reverse ? "egor" : "stepan");
    Settings settings; settings.enableUdp = settings.enableWorld = true;
    Multiplayer host(a, settings), client(b, settings); OnlineMenu hm, cm; std::string err;
    CHECK(hm.setField(OnlineField::Port, "0", {}).empty());
    CHECK(cm.toggleRole({}).empty());
    for (int round = 0; round < 2; ++round) {
        const auto hr = hm.request(OnlineAction::Start, host.status(), err); CHECK(hr && host.host(hr->endpoint, err));
        wait([&] { return host.status().state == State::Listening; }, host, client);
        CHECK(cm.setField(OnlineField::Port, std::to_string(host.status().endpoint.port), client.status()).empty());
        const auto cr = cm.request(OnlineAction::Start, client.status(), err); CHECK(cr && client.join(cr->endpoint, err));
        wait([&] { return a.pending && b.pending; }, host, client);
        CHECK(host.confirmWorld({a.pending->ticket, true, {}})); CHECK(client.confirmWorld({b.pending->ticket, true, {}}));
        a.pending.reset(); b.pending.reset();
        wait([&] { return host.status().world == WorldState::Ready && client.status().world == WorldState::Ready &&
                          host.status().udp.state == UdpState::Ready && client.status().udp.state == UdpState::Ready; }, host, client);
        CHECK(item(hm.page("online-players", host.status(), a.name), "Remote player").hint == b.name);
        PlayerSnapshot p; p.active = true; p.feet.x = 3;
        const auto old = a.samples + b.samples; CHECK(host.submitPlayer(p) && client.submitPlayer(p));
        wait([&] { return a.samples + b.samples > old + 4; }, host, client);
        CHECK(!cm.request(OnlineAction::Start, client.status(), err));
        CHECK(cm.request(OnlineAction::Disconnect, client.status(), err)->disconnect); client.leave();
        wait([&] { return client.finished() && host.status().state == State::Listening; }, host, client); client.reap();
        CHECK(item(hm.page("online-players", host.status(), a.name), "No remote player").hint == "Create or join a session");
        CHECK(hm.request(OnlineAction::Disconnect, host.status(), err)->disconnect); host.leave();
        wait([&] { return host.finished(); }, host, client); host.reap();
    }
    host.shutdown(); client.shutdown();
}
}
int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"safe defaults and zero UI sockets/threads", defaults}, {"numeric IPv4/IPv6 validation", addresses},
        {"port validation incl ephemeral host", ports}, {"validated form to typed request", saveAndRequest},
        {"all active states lock connection form", busyStates}, {"stale actions recheck live state", staleAction},
        {"status and players projections", views}, {"local/TCP/UDP/world errors retained", errors},
        {"ping explicitly unmeasured", noFakePing}, {"exact online path routing", paths},
        {"editor replaces initial selection", editorReplace}, {"editor caret/delete/home/end/select-all", editorNavigation},
        {"field alphabet and length bounds", editorFilters}, {"numeric keypad", editorKeypad},
        {"no clipboard or modified shortcut insertion", editorShortcuts}, {"read-only details and bounded scroll", messageBounds},
        {"external start while editing rejects commit", staleEditValidation},
        {"form -> real TCP+UDP+world: Egor host, reconnect", [] { realConnection(false); }},
        {"form -> real TCP+UDP+world: Stepan host, reconnect", [] { realConnection(true); }}
    };
    unsigned passed = 0;
    for (const auto& [name, test] : tests) {
        try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { std::cerr << "FAIL " << name << ": " << e.what() << '\n'; return 1; }
    }
    std::cout << passed << '/' << tests.size() << " OnlineMenu groups passed\n";
}
