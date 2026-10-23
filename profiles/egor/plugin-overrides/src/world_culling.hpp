#pragma once

// Conservative replacement for the missing Stage-3D world-culling helper.
// Bounds are exact for the supplied CPU vertices; an empty input fails open.

#include "map.hpp"

namespace hyprwalk::world_culling {

    template <typename Vertex>
    inline bool buildBounds(const std::vector<Vertex>& vertices, SAABB& out) {
        out = SAABB::empty();
        for (const auto& vertex : vertices)
            out.grow({vertex.pos[0], vertex.pos[1], vertex.pos[2]});
        return !vertices.empty() && out.min.x <= out.max.x && out.min.y <= out.max.y && out.min.z <= out.max.z;
    }

}
