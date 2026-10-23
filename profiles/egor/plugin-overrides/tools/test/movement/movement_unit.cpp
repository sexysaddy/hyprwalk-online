#include "movement.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using hyprwalk::V3;

namespace {
    bool near(float a, float b, float epsilon = 1e-4f) {
        return std::abs(a - b) <= epsilon;
    }
}

int main() {
    {
        V3 velocity{4.f, 0.f, 0.f};
        hyprwalk::applyGroundFriction(velocity, 6.f, 1.f, 0.1f);
        assert(near(velocity.x, 1.6f));
        assert(near(velocity.z, 0.f));
    }
    {
        V3 velocity{4.f, 0.f, 0.f};
        hyprwalk::accelerateHorizontal(velocity, {1.f, 0.f, 0.f}, 4.f, 12.f, 1.f / 60.f);
        // Already at wish speed: acceleration must not add speed in the same direction.
        assert(near(velocity.x, 4.f));
    }
    {
        V3 velocity{4.f, 0.f, 0.f};
        hyprwalk::accelerateAir(velocity, {0.f, 0.f, 1.f}, 4.5f, 0.8f, 10.f, 1.f / 60.f);
        // Sideways input builds a new component without erasing the old momentum.
        assert(near(velocity.x, 4.f));
        assert(velocity.z > 0.f && velocity.z <= 0.8f);
        assert(std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z) > 4.f);
    }
    {
        V3 velocity{8.f, 0.f, 6.f};
        hyprwalk::capHorizontalSpeed(velocity, 5.f);
        assert(near(std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z), 5.f));
    }
    {
        V3 velocity{3.f, 0.f, 2.f};
        hyprwalk::capHorizontalSpeed(velocity, 12.f);
        assert(near(velocity.x, 3.f) && near(velocity.z, 2.f));
    }

    std::cout << "movement physics checks passed\n";
    return 0;
}
