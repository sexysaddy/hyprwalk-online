#pragma once

// Exact bounds and deterministic coarse chunking for map index batches.  The
// original file is part of the unavailable Stage-3D checkpoint; this helper
// preserves the map data and draw ordering while keeping chunk boundaries
// conservative.

#include "map.hpp"

#include <cmath>
#include <cstdint>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace hyprwalk::spatial {

    struct Chunk {
        std::vector<uint32_t> indices;
        SAABB                  bounds = SAABB::empty();
    };

    inline bool indexedBounds(const SMapModel& model, const std::vector<uint32_t>& indices, SAABB& out) {
        out = SAABB::empty();
        bool any = false;
        for (const uint32_t index : indices) {
            if (index >= model.vertices.size())
                continue;
            const auto& vertex = model.vertices[index];
            out.grow({vertex.pos[0], vertex.pos[1], vertex.pos[2]});
            any = true;
        }
        return any;
    }

    inline std::vector<std::pair<int64_t, Chunk>> splitIndices(const SMapModel& model, const std::vector<uint32_t>& indices, float cellSize) {
        std::map<std::tuple<int, int, int>, Chunk> grouped;
        if (!(cellSize > 0) || !std::isfinite(cellSize) || indices.size() % 3 != 0) {
            Chunk chunk;
            chunk.indices = indices;
            indexedBounds(model, indices, chunk.bounds);
            return {{0, std::move(chunk)}};
        }

        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            SAABB tri = SAABB::empty();
            const uint32_t ids[3] = {indices[i], indices[i + 1], indices[i + 2]};
            bool valid = true;
            for (const uint32_t id : ids) {
                if (id >= model.vertices.size()) {
                    valid = false;
                    break;
                }
                const auto& v = model.vertices[id];
                tri.grow({v.pos[0], v.pos[1], v.pos[2]});
            }
            if (!valid)
                continue;
            const V3 c = tri.center();
            const auto key = std::tuple{(int)std::floor(c.x / cellSize), (int)std::floor(c.y / cellSize), (int)std::floor(c.z / cellSize)};
            auto& chunk = grouped[key];
            chunk.indices.insert(chunk.indices.end(), ids, ids + 3);
            chunk.bounds.grow(tri.min);
            chunk.bounds.grow(tri.max);
        }

        std::vector<std::pair<int64_t, Chunk>> result;
        result.reserve(grouped.size());
        int64_t serial = 0;
        for (auto& [key, chunk] : grouped)
            result.emplace_back(serial++, std::move(chunk));
        return result;
    }

}
