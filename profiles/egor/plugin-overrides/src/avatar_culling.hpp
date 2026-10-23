#pragma once

// Conservative avatar bounds used by the renderer's optional frustum test.
// The original Stage-3D checkpoint was not present in the supplied source
// archives, so this implementation derives the same data from the public
// avatar model and shader layout.  Every malformed or incomplete input fails
// open: an avatar is rendered rather than being incorrectly discarded.

#include "avatar.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace hyprwalk::avatar_culling {

    inline bool finite(const V3& p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }

    inline bool valid(const SAABB& b) {
        return finite(b.min) && finite(b.max) && b.min.x <= b.max.x && b.min.y <= b.max.y && b.min.z <= b.max.z;
    }

    // The skin shader blends four affine joint matrices.  For a vertex with a
    // non-zero influence from joint j, the point after skinning is in the
    // convex hull of that joint's transformed point.  Keeping a mesh-space
    // envelope for every such joint and transforming all eight corners is
    // therefore conservative for every pose.
    inline bool buildJointInfluenceBounds(const SAvatarModel& model, std::vector<SAABB>& out) {
        out.assign(model.joints.size(), SAABB::empty());
        if (model.joints.empty() || model.vertices.empty()) {
            out.clear();
            return false;
        }

        const size_t n = model.vertices.size();
        std::vector<V3> morphLo(n), morphHi(n);
        for (const auto& d : model.morphDeltas) {
            if (d.vertex >= n || !std::isfinite(d.pos[0]) || !std::isfinite(d.pos[1]) || !std::isfinite(d.pos[2])) {
                out.clear();
                return false;
            }
            const V3 delta{d.pos[0], d.pos[1], d.pos[2]};
            morphLo[d.vertex].x += std::min(delta.x, 0.f);
            morphLo[d.vertex].y += std::min(delta.y, 0.f);
            morphLo[d.vertex].z += std::min(delta.z, 0.f);
            morphHi[d.vertex].x += std::max(delta.x, 0.f);
            morphHi[d.vertex].y += std::max(delta.y, 0.f);
            morphHi[d.vertex].z += std::max(delta.z, 0.f);
            if (!finite(morphLo[d.vertex]) || !finite(morphHi[d.vertex])) {
                out.clear();
                return false;
            }
        }

        bool any = false;
        for (size_t i = 0; i < n; ++i) {
            const auto& v = model.vertices[i];
            const V3     p{v.pos[0], v.pos[1], v.pos[2]};
            if (!finite(p)) {
                out.clear();
                return false;
            }
            const V3 lo = p + morphLo[i];
            const V3 hi = p + morphHi[i];
            if (!finite(lo) || !finite(hi) || lo.x > hi.x || lo.y > hi.y || lo.z > hi.z) {
                out.clear();
                return false;
            }

            unsigned weightSum = 0;
            for (size_t k = 0; k < 4; ++k) {
                const uint16_t joint = v.joints[k];
                const uint8_t  weight = v.weights[k];
                if (joint >= out.size()) {
                    out.clear();
                    return false;
                }
                weightSum += weight;
                if (!weight)
                    continue;
                out[joint].grow(lo);
                out[joint].grow(hi);
                any = true;
            }
            // SAvatarVertex::weights are normalized unsigned bytes and the
            // loader guarantees a total of 255. Any other total changes the
            // affine blend's implicit homogeneous coordinate; leave culling
            // disabled rather than proving a bound for malformed data.
            if (weightSum != 255) {
                out.clear();
                return false;
            }
        }

        return any;
    }

    inline bool weightsWithinEnvelope(const std::vector<float>& weights) {
        for (const float weight : weights)
            if (!std::isfinite(weight) || weight < 0.f || weight > 1.f)
                return false;
        return true;
    }

    inline V3 jointPoint(const float* matrix, const V3& p) {
        // `staging` stores the three rows of the shader's 3x4 matrix.
        return {matrix[0] * p.x + matrix[1] * p.y + matrix[2] * p.z + matrix[3],
                matrix[4] * p.x + matrix[5] * p.y + matrix[6] * p.z + matrix[7],
                matrix[8] * p.x + matrix[9] * p.y + matrix[10] * p.z + matrix[11]};
    }

    inline bool transformedWorldBounds(const std::vector<SAABB>& jointBounds, const std::vector<float>& staging, const M4& modelToWorld,
                                       SAABB& out) {
        out = SAABB::empty();
        for (const float value : modelToWorld.m)
            if (!std::isfinite(value))
                return false;
        if (jointBounds.empty() || staging.size() < jointBounds.size() * 12)
            return false;

        bool any = false;
        for (size_t joint = 0; joint < jointBounds.size(); ++joint) {
            const SAABB& b = jointBounds[joint];
            if (!valid(b))
                continue;
            const float* matrix = staging.data() + joint * 12;
            for (const float value : std::span<const float>{matrix, 12})
                if (!std::isfinite(value))
                    return false;
            const V3 corners[8] = {
                {b.min.x, b.min.y, b.min.z}, {b.max.x, b.min.y, b.min.z}, {b.min.x, b.max.y, b.min.z}, {b.max.x, b.max.y, b.min.z},
                {b.min.x, b.min.y, b.max.z}, {b.max.x, b.min.y, b.max.z}, {b.min.x, b.max.y, b.max.z}, {b.max.x, b.max.y, b.max.z},
            };
            for (const V3& corner : corners) {
                const V3 transformed = modelToWorld.point(jointPoint(matrix, corner));
                if (!finite(transformed))
                    return false;
                out.grow(transformed);
                any = true;
            }
        }
        return any && valid(out);
    }

    inline void expand(SAABB& bounds, float margin) {
        if (!valid(bounds))
            return;
        if (!(margin > 0) || !std::isfinite(margin))
            return;
        const V3 delta{margin, margin, margin};
        bounds.min -= delta;
        bounds.max += delta;
    }

}
