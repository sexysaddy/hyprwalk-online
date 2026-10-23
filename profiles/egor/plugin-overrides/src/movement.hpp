#pragma once

#include "math3d.hpp"

namespace hyprwalk {

    // Source-style horizontal velocity helpers. Speeds are metres/second and dt is seconds.
    void applyGroundFriction(V3& velocity, float friction, float stopSpeed, float dt);
    void accelerateHorizontal(V3& velocity, const V3& wishDir, float wishSpeed, float acceleration, float dt);
    // Source air acceleration caps the speed along the requested direction, but scales acceleration by the full wish speed.
    void accelerateAir(V3& velocity, const V3& wishDir, float wishSpeed, float wishSpeedCap, float acceleration, float dt);
    void capHorizontalSpeed(V3& velocity, float maxSpeed);

}
