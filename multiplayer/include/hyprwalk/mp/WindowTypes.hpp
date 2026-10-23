#pragma once
#include "Types.hpp"
#include <array>
#include <compare>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hyprwalk::mp {
inline constexpr std::uint16_t WindowProtocolVersion = 1;
inline constexpr std::size_t MaxSharedWindows = 16, MaxLocalWindows = 256;
inline constexpr std::size_t MaxWindowTitle = 120, MaxApplicationId = 64;
// The generation is the freshly negotiated world ticket, NOT just the host session ID.
struct WindowKey {
    Id session{}, owner{}, generation{};
    std::uint64_t window = 0;
    auto operator<=>(const WindowKey&) const = default;
};
struct WindowGeometry {
    std::array<float, 3> origin{}, right{1, 0, 0}, down{0, -1, 0};
    float width = 1, height = 1; // metres; origin = top left, axes unit/orthogonal
    std::uint16_t pixelWidth = 1, pixelHeight = 1; // logical source pixels, not a video promise
    bool operator==(const WindowGeometry&) const = default;
};
struct WindowMetadata {
    std::uint64_t id = 0; // locally allocated monotonic ID, NEVER a pointer, PID or GL texture
    std::string application, title; // sanitised UTF-8 application identity and title
    WindowGeometry geometry;
    bool operator==(const WindowMetadata&) const = default;
};
// Atomic complete replacement: absence means withdrawn. No unbounded tombstone table.
struct WindowSnapshot {
    Id session{}, owner{}, generation{};
    std::uint64_t revision = 0;
    std::vector<WindowMetadata> windows;
};
struct ReceivedWindows {
    WindowSnapshot snapshot;
    bool expired = false; // local lease expiry, not a wire field
};
struct ReceiveWindowSettings {
    bool hidden = false; // local-only visibility preference
    bool operator==(const ReceiveWindowSettings&) const = default;
};
struct RemoteWindow {
    WindowKey key;
    WindowMetadata metadata;
    ReceiveWindowSettings receive;
    std::string video = "Placeholder - video unavailable (Stage 3D)";
};
struct WindowHit {
    WindowKey key;
    float distance = 0;
};
bool validWindowText(std::string_view text, std::size_t maxBytes) noexcept;
std::string cleanWindowText(std::string_view text, std::size_t maxBytes);
std::string applicationKey(std::string_view application);
bool validWindow(const WindowMetadata&, std::string& error);
bool validWindowSnapshot(const WindowSnapshot&, std::string& error);
std::vector<std::uint8_t> encodeWindows(const WindowSnapshot&); // payload only, <=4096
bool decodeWindows(std::span<const std::uint8_t>, WindowSnapshot&, std::string& error);
// Pure geometry. A hit can select a remote panel but never forwards input to the remote application.
std::optional<WindowHit> hitRemoteWindow(const RemoteWindow&, const std::array<float,3>& eye,
                                       const std::array<float,3>& direction);
} // namespace hyprwalk::mp
