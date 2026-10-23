#pragma once
#include "world.hpp"
#include <array>
#include <cmath>

namespace hyprwalk {
// Render-only fallback. It has no Wayland window, input target, model, or network ownership.
struct SRemotePlayerFrame {
    V3 feet{}, velocity{};
    float yaw = 0, pitch = 0, bodyYaw = 0;
    bool visible = false, onGround = false, running = false, crouched = false, flying = false;
};
struct SRemotePlayerMesh {
    static constexpr size_t Capacity = 7 * 36; // Body, head, two arms, two legs, gaze marker.
    std::array<SVertex, Capacity> vertices{};
    size_t count = 0;
};
// Pure geometry, tested without Hyprland/GL. The renderer owns a separate VBO/VAO.
inline SRemotePlayerMesh remotePlayerMesh(const SRemotePlayerFrame& p, float time) {
    SRemotePlayerMesh mesh;
    if (!p.visible) return mesh;
    for (float v : {p.feet.x, p.feet.y, p.feet.z, p.velocity.x, p.velocity.y, p.velocity.z, p.yaw, p.pitch, p.bodyYaw, time})
        if (!std::isfinite(v)) return mesh;
    const auto box = [&](V3 center, V3 half, const std::array<V3, 3>& axes, float material) {
        const float h[] = {half.x, half.y, half.z};
        for (int face = 0; face < 6; ++face) {
            const int n = face / 2;
            const float sign = face % 2 ? -1.f : 1.f;
            const int a = face % 2 ? (n + 2) % 3 : (n + 1) % 3;
            const int b = face % 2 ? (n + 1) % 3 : (n + 2) % 3;
            const V3 normal = axes[n] * sign, c = center + normal * h[n];
            const V3 u = axes[a] * h[a], v = axes[b] * h[b];
            const V3 corners[] = {c - u - v, c + u - v, c + u + v, c - u + v};
            constexpr int indices[] = {0, 1, 2, 0, 2, 3};
            constexpr float uv[4][2] = {{0,0},{1,0},{1,1},{0,1}};
            for (int i : indices) {
                auto& out = mesh.vertices[mesh.count++];
                out = {{corners[i].x, corners[i].y, corners[i].z}, {normal.x, normal.y, normal.z},
                       {uv[i][0], uv[i][1]}, material, 1.f};
            }
        }
    };
    const V3 right{std::cos(p.bodyYaw), 0, std::sin(p.bodyYaw)}, up{0,1,0}, back{-std::sin(p.bodyYaw), 0, std::cos(p.bodyYaw)};
    const std::array<V3,3> body{right, up, back};
    const auto at = [&](float x, float y, float z) { return p.feet + right*x + up*y + back*z; };
    const float height = p.crouched ? 1.18f : 1.8f;
    const float leg = height * 0.4f;
    const float speed = std::min(std::hypot(p.velocity.x, p.velocity.z), 20.f);
    const float stride = p.onGround && !p.flying ? std::sin(time * std::max(speed, 1.f) * 4.f) * std::min(speed * 0.04f, p.running ? 0.23f : 0.16f) : 0.f;
    box(at(0,leg + (height - leg - 0.38f)*0.5f,0), {0.24f,(height-leg-0.38f)*0.5f,0.14f},body,MAT_PLASTER);
    for (float side : {-1.f,1.f}) {
        box(at(side*0.12f,leg*0.5f+std::max(0.f,side*stride)*0.25f,side*stride),{0.1f,leg*0.5f,0.13f},body,MAT_STONE);
        if (p.flying) box(at(side*0.47f,height*0.73f,0),{0.22f,0.075f,0.075f},body,MAT_PLASTER);
        else box(at(side*0.33f,leg+0.14f,-side*stride),{0.075f,0.27f,0.075f},body,MAT_PLASTER);
    }
    const V3 gaze{std::sin(p.yaw)*std::cos(p.pitch),std::sin(p.pitch),-std::cos(p.yaw)*std::cos(p.pitch)};
    const V3 headRight{std::cos(p.yaw),0,std::sin(p.yaw)};
    const std::array<V3,3> head{headRight,cross(headRight,gaze),-gaze};
    const V3 headCenter = at(0,height-0.18f,0);
    box(headCenter,{0.18f,0.18f,0.18f},head,MAT_PLASTER);
    box(headCenter+gaze*0.43f,{0.035f,0.035f,0.25f},head,MAT_CRATE);
    return mesh;
}
} // namespace hyprwalk
