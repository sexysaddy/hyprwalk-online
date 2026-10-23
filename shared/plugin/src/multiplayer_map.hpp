#pragma once
// Value conversion only. Network framing and admission stay in the shared module.
#include "map.hpp"
#include <hyprwalk/mp/MapSync.hpp>

namespace hyprwalk {
inline mp::Digest multiplayerCourtyardCollision(const SWorld& world) {
    mp::ContentHash h;
    h.integer(0x4857434f55525431ULL); // HWCOURT1: collision interpreter/generator fingerprint.
    h.integer(world.collision.boxes().size());
    for (const auto& b : world.collision.boxes())
        for (const auto v : {b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z})
            h.floating(v);
    return h.finish();
}
inline mp::MapDescriptor multiplayerMapDescriptor(const SWorld& world, float scale, const SScreenSpec& spec) {
    mp::MapDescriptor d;
    d.aspect = spec.aspect; d.height = spec.height; d.centerY = spec.centerY;
    if (world.model) {
        d.kind = mp::MapKind::EmbeddedGlb;
        d.content = world.model->multiplayerContent;
        d.collision = world.model->multiplayerCollision;
        d.scale = scale;
    } else d.collision = multiplayerCourtyardCollision(world);
    d.spawn = {world.spawn.x, world.spawn.y, world.spawn.z}; d.spawnYaw = world.spawnYaw;
    d.desktopCenter = {world.desktop.center.x, world.desktop.center.y, world.desktop.center.z};
    d.desktopNormal = {world.desktop.normal.x, world.desktop.normal.y, world.desktop.normal.z};
    d.desktopHeight = world.desktop.height;
    return d;
}
} // namespace hyprwalk
