#include "movement.hpp"

#include <algorithm>
#include <cmath>

namespace hyprwalk {

    void applyGroundFriction(V3& velocity, float friction, float stopSpeed, float dt) {
        const float speed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
        if (speed < 1e-5f) {
            velocity.x = velocity.z = 0.f;
            return;
        }

        const float control = std::max(speed, std::max(0.f, stopSpeed));
        const float drop = control * std::max(0.f, friction) * std::max(0.f, dt);
        const float newSpeed = std::max(0.f, speed - drop);
        const float scale = newSpeed / speed;
        velocity.x *= scale;
        velocity.z *= scale;
    }

    void accelerateHorizontal(V3& velocity, const V3& wishDir, float wishSpeed, float acceleration, float dt) {
        if (wishSpeed <= 0.f || acceleration <= 0.f || dt <= 0.f)
            return;

        const float currentSpeed = velocity.x * wishDir.x + velocity.z * wishDir.z;
        const float addSpeed = wishSpeed - currentSpeed;
        if (addSpeed <= 0.f)
            return;

        const float accelSpeed = std::min(acceleration * dt * wishSpeed, addSpeed);
        velocity.x += wishDir.x * accelSpeed;
        velocity.z += wishDir.z * accelSpeed;
    }

    void accelerateAir(V3& velocity, const V3& wishDir, float wishSpeed, float wishSpeedCap, float acceleration, float dt) {
        if (wishSpeed <= 0.f || acceleration <= 0.f || dt <= 0.f)
            return;

        const float cappedWishSpeed = std::min(wishSpeed, std::max(0.f, wishSpeedCap));
        const float currentSpeed = velocity.x * wishDir.x + velocity.z * wishDir.z;
        const float addSpeed = cappedWishSpeed - currentSpeed;
        if (addSpeed <= 0.f)
            return;

        // Deliberately use uncapped wishSpeed here, as classic Source air acceleration does.
        const float accelSpeed = std::min(acceleration * dt * wishSpeed, addSpeed);
        velocity.x += wishDir.x * accelSpeed;
        velocity.z += wishDir.z * accelSpeed;
    }

    void capHorizontalSpeed(V3& velocity, float maxSpeed) {
        const float speed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
        maxSpeed = std::max(0.f, maxSpeed);
        if (speed > maxSpeed && speed > 1e-5f) {
            const float scale = maxSpeed / speed;
            velocity.x *= scale;
            velocity.z *= scale;
        }
    }

}
