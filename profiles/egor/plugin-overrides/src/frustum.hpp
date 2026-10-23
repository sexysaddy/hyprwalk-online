#pragma once

// Conservative Stage-3D compatibility implementation.
//
// The original checkpoint containing this header was not present in the
// supplied archives.  This implementation keeps the renderer correct by
// failing open for invalid matrices and using a standard clip-space AABB test
// for valid matrices.  It never rejects geometry that cannot be bounded.

#include "math3d.hpp"

#include <cmath>

namespace hyprwalk {

    struct SFrustum {
        struct Plane {
            V3    normal{};
            float distance = 0;
            bool  valid = false;

            float signedDistance(const V3& point) const {
                return dot(normal, point) + distance;
            }
        };

        Plane planes[6];
        bool  valid = false;

        static SFrustum fromMatrix(const M4& m) {
            SFrustum out;
            // M4 is column-major. These are the rows of the clip matrix,
            // combined as row4 +/- row1/row2/row3.
            const float rows[4][4] = {
                {m.m[0], m.m[4], m.m[8], m.m[12]},
                {m.m[1], m.m[5], m.m[9], m.m[13]},
                {m.m[2], m.m[6], m.m[10], m.m[14]},
                {m.m[3], m.m[7], m.m[11], m.m[15]},
            };
            const int signs[6] = {1, -1, 1, -1, 1, -1};
            const int axis[6] = {0, 0, 1, 1, 2, 2};
            for (int i = 0; i < 6; ++i) {
                const int a = axis[i];
                const float s = (float)signs[i];
                V3 n{rows[3][0] + s * rows[a][0], rows[3][1] + s * rows[a][1], rows[3][2] + s * rows[a][2]};
                const float d = rows[3][3] + s * rows[a][3];
                const float len = length(n);
                if (!(len > 1e-8f) || !std::isfinite(len))
                    return out;
                out.planes[i] = {n / len, d / len, true};
            }
            out.valid = true;
            return out;
        }

        bool intersects(const SAABB& bounds) const {
            if (!valid || bounds.min.x > bounds.max.x || bounds.min.y > bounds.max.y || bounds.min.z > bounds.max.z)
                return true;
            for (const auto& plane : planes) {
                const V3 positive{
                    plane.normal.x >= 0 ? bounds.max.x : bounds.min.x,
                    plane.normal.y >= 0 ? bounds.max.y : bounds.min.y,
                    plane.normal.z >= 0 ? bounds.max.z : bounds.min.z,
                };
                if (plane.signedDistance(positive) < 0)
                    return false;
            }
            return true;
        }
    };

}
