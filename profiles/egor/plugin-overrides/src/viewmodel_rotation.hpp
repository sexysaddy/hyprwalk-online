#pragma once

#include <array>
#include <cmath>

namespace hyprwalk::viewmodel_rotation {

    struct Point {
        float x = 0.f, y = 0.f, z = 0.f;
    };

    // Column-major for glUniformMatrix3fv.  Rotate about GoldSrc model X,
    // then Y, then Z (Rz * Ry * Rx), AFTER skeletal skinning.
    inline std::array<float, 9> matrix(float xDegrees, float yDegrees, float zDegrees) {
        constexpr float DEG_TO_RAD = 0.01745329251994329577f;
        const float sx = std::sin(xDegrees * DEG_TO_RAD), cx = std::cos(xDegrees * DEG_TO_RAD);
        const float sy = std::sin(yDegrees * DEG_TO_RAD), cy = std::cos(yDegrees * DEG_TO_RAD);
        const float sz = std::sin(zDegrees * DEG_TO_RAD), cz = std::cos(zDegrees * DEG_TO_RAD);
        return {
            cz * cy,                   sz * cy,                  -sy,
            cz * sy * sx - sz * cx,    sz * sy * sx + cz * cx,    cy * sx,
            cz * sy * cx + sz * sx,    sz * sy * cx - cz * sx,    cy * cx
        };
    }

    inline Point transform(const std::array<float, 9>& m, Point p, Point pivot) {
        const float x = p.x - pivot.x, y = p.y - pivot.y, z = p.z - pivot.z;
        return {
            pivot.x + m[0] * x + m[3] * y + m[6] * z,
            pivot.y + m[1] * x + m[4] * y + m[7] * z,
            pivot.z + m[2] * x + m[5] * y + m[8] * z
        };
    }
}
