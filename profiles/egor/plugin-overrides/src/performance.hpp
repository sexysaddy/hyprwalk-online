#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>

namespace hyprwalk::performance {

// Zero means uncapped. Keep the choices discrete so the menu and settings file
// always expose predictable frame intervals.
inline constexpr std::array<int, 6> FPS_LIMITS{30, 45, 60, 90, 120, 0};
inline constexpr int DEFAULT_FPS_LIMIT = 60;

inline constexpr int normalizeFpsLimit(int value) {
    if (value <= 0)
        return 0;
    if (value >= 120)
        return 120;
    for (const int option : FPS_LIMITS)
        if (value == option)
            return option;
    int best = DEFAULT_FPS_LIMIT;
    int distance = 100000;
    for (const int option : FPS_LIMITS) {
        if (option == 0)
            continue;
        const int d = std::abs(value - option);
        if (d < distance) {
            distance = d;
            best = option;
        }
    }
    return best;
}

inline int fpsSetting(float value) {
    if (!std::isfinite(value)) return DEFAULT_FPS_LIMIT;
    return normalizeFpsLimit((int)std::lround(std::clamp(value, 0.f, 120.f)));
}

inline constexpr int nextFpsLimit(int current) {
    current = normalizeFpsLimit(current);
    for (std::size_t i = 0; i < FPS_LIMITS.size(); ++i)
        if (FPS_LIMITS[i] == current)
            return FPS_LIMITS[(i + 1) % FPS_LIMITS.size()];
    return DEFAULT_FPS_LIMIT;
}

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

// Phase is kept in nanoseconds, not rounded-up millisecond intervals (60 FPS != 17 ms).
// The <=1 ms early allowance matches the one-shot Wayland timer's resolution. It does
// not accumulate: every subsequent deadline is advanced from the same phase.
struct SFrameCadence {
    int limit = -1;
    TimePoint next{};
    static constexpr auto EARLY = std::chrono::milliseconds(1);

    void reset() { limit = -1; next = {}; }
    static std::chrono::nanoseconds period(int cap) {
        return std::chrono::nanoseconds(cap > 0 ? (1000000000LL + cap / 2) / cap : 0);
    }
    bool due(TimePoint now, int cap) {
        cap = normalizeFpsLimit(cap);
        if (limit != cap) { limit = cap; next = {}; }
        return cap == 0 || next == TimePoint{} || now + EARLY >= next;
    }
    void advance(TimePoint now) {
        if (limit <= 0) { next = {}; return; }
        const auto step = period(limit);
        if (next == TimePoint{} || now - next > step * 8)
            next = now + step; // first frame / resume after a long pause: never replay a backlog
        else {
            next += step;
            if (next <= now + EARLY)
                next += step * ((now + EARLY - next) / step + 1);
        }
    }
    int delayMs(TimePoint now) const {
        if (limit <= 0 || next == TimePoint{}) return 1;
        // Wake no later than the deadline, then let the compositor decide when to render.
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(next - now).count();
        return (int)std::clamp<long long>(ms, 1, 1000);
    }
};

} // namespace hyprwalk::performance
