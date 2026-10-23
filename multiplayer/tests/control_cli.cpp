#include <hyprwalk/mp/Multiplayer.hpp>
#include <charconv>
#include <csignal>
#include <iostream>
#include <poll.h>

namespace {
volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }
struct Adapter final : hyprwalk::mp::IAdapter {
    std::string name;
    explicit Adapter(std::string n) : name(std::move(n)) {}
    hyprwalk::mp::LocalInfo localInfo() const override { return {name}; }
    void onState(const hyprwalk::mp::Status& s) noexcept override {
        std::cout << "STATUS " << hyprwalk::mp::formatStatus(s) << std::endl;
    }
    void onDiagnostic(const hyprwalk::mp::Diagnostic& e) noexcept override {
        std::cout << "EVENT " << e.text << std::endl;
    }
};
}
int main(int argc, char** argv) {
    using namespace hyprwalk::mp;
    if (argc < 4 || argc > 6 || (std::string(argv[1]) != "host" && std::string(argv[1]) != "join")) {
        std::cerr << "usage: control_cli host|join <numeric-ip> <port> [name [--once]]\n"; return 2;
    }
    unsigned port = 0;
    const std::string text = argv[3];
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), port);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || port > 65535) {
        std::cerr << "invalid port\n"; return 2;
    }
    const bool once = argc == 6 && std::string(argv[5]) == "--once";
    if (argc == 6 && !once) { std::cerr << "unknown option\n"; return 2; }
    std::signal(SIGINT, interrupt); std::signal(SIGTERM, interrupt);
    Adapter adapter(argc >= 5 ? argv[4] : "standalone");
    Multiplayer core(adapter);
    std::string error;
    const Endpoint endpoint{argv[2], static_cast<std::uint16_t>(port)};
    if (!(std::string(argv[1]) == "host" ? core.host(endpoint, error) : core.join(endpoint, error))) {
        std::cerr << error << '\n'; return 1;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    bool success = false, leaving = false, failed = false;
    while (!core.finished()) {
        core.dispatch();
        const auto s = core.status();
        if (s.testsReceived >= 1 && s.testsAcknowledged >= 1) {
            if (!success) std::cout << "EXCHANGE_OK " << formatStatus(s) << std::endl;
            success = true;
        }
        if (once && std::chrono::steady_clock::now() >= deadline && !success) { failed = true; break; }
        if (!leaving && (interrupted || (once && success))) { core.leave(); leaving = true; }
        pollfd fd{core.notificationFd(), POLLIN, 0};
        ::poll(&fd, 1, 100);
    }
    core.dispatch();
    const auto final = core.status();
    if (!success && final.testsReceived >= 1 && final.testsAcknowledged >= 1) {
        success = true;
        std::cout << "EXCHANGE_OK " << formatStatus(final) << std::endl;
    }
    failed = failed || final.state == State::Error;
    std::cout << "FINAL " << formatStatus(final) << std::endl;
    core.shutdown();
    return failed || (once && !success) ? 1 : 0;
}
