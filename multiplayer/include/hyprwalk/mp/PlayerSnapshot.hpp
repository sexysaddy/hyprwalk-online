#pragma once
#include "Types.hpp"

namespace hyprwalk::mp {
struct Vec3 { float x = 0, y = 0, z = 0; };
// Physical feet, metres, Y up, -Z forward at zero yaw. Angles are radians.
// This value contains no camera, renderer, animation or compositor resources.
struct PlayerSnapshot {
    Vec3 feet, velocity;
    float yaw = 0, pitch = 0, bodyYaw = 0;
    std::uint32_t teleport = 0; // Increment after a deliberate respawn/teleport.
    bool active = false, onGround = false, running = false, crouched = false, flying = false;
};
bool validPlayerSnapshot(const PlayerSnapshot& value) noexcept;
struct ReceivedPlayer {
    PlayerSnapshot player;
    std::uint32_t sequence = 0;
    std::uint64_t senderTimeUs = 0; // Relative to this UDP connection, not a shared clock.
    std::chrono::steady_clock::time_point receivedAt{};
};
} // namespace hyprwalk::mp
