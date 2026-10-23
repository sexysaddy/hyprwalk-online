#pragma once
#include "Types.hpp"
#include "PlayerSnapshot.hpp"
#include "ContentHash.hpp"
#include <optional>
#include <span>
#include <vector>

namespace hyprwalk::mp {
enum class MapKind : std::uint16_t { Courtyard = 1, EmbeddedGlb = 2 };
struct MapDescriptor {
    MapKind kind = MapKind::Courtyard;
    std::uint16_t geometryVersion = 1;
    Digest content{}, collision{};
    float aspect = 16.f/9.f, height = 2.4f, centerY = 1.65f, scale = 1.f;
    Vec3 spawn{};
    float spawnYaw = 0;
    Vec3 desktopCenter{0,1.65f,0}, desktopNormal{0,0,1};
    float desktopHeight = 2.4f;
};
struct WorldRequest { Id ticket{}; MapDescriptor map; };
struct WorldConfirmation { Id ticket{}; bool applied = false; std::string reason; };
inline constexpr std::size_t MapDescriptorSize = 128;
bool validMap(const MapDescriptor& map, std::string& error);
// Does not use filenames/paths. Courtyard collision must ALSO be checked after host-parameter rebuild.
bool compatibleMap(const MapDescriptor& local, const MapDescriptor& host, std::string& error);
std::vector<std::uint8_t> encodeMap(const MapDescriptor& map);
bool decodeMap(std::span<const std::uint8_t> bytes, MapDescriptor& map, std::string& error);
} // namespace hyprwalk::mp
