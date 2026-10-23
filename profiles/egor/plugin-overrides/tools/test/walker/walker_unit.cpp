#include "walker.hpp"
#include "collision.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using hyprwalk::SWalker;
using hyprwalk::V3;

namespace {
    bool near(float a, float b, float epsilon = 1e-3f) {
        return std::abs(a - b) <= epsilon;
    }

    auto flatFloor() {
        return [](const V3& feet, float /*height*/) { return feet.y < 0.f; };
    }
}

int main() {
    // A descending player just above the floor should recover contact after a short missed floor check.
    {
        SWalker walker;
        walker.feet = {0.f, 0.05f, 0.f};
        walker.vel = {5.f, -0.1f, 0.f};
        walker.onGround = false;
        walker.overlaps = flatFloor();

        walker.move(1.f / 60.f, 1.8f, false);
        assert(walker.onGround);
        assert(walker.feet.y >= 0.f && walker.feet.y < 0.01f);
        assert(near(walker.vel.x, 5.f)); // the ground-contact recovery must not kill bhop's horizontal speed
        assert(walker.feet.x > 0.f);      // horizontal motion still advances during the recovered landing
        assert(near(walker.vel.y, 0.f));
    }

    // A probe that finds no floor must be observational only; it must not advance the fall by another 0.12 m.
    {
        SWalker walker;
        walker.feet = {0.f, 1.f, 0.f};
        walker.vel = {0.f, -1.f, 0.f};
        walker.onGround = false;
        walker.overlaps = flatFloor();

        walker.move(0.016f, 1.8f, false);
        assert(!walker.onGround);
        assert(near(walker.feet.y, 0.984f));
        assert(near(walker.vel.y, -1.f));
    }

    // When an axis-aligned prop blocks horizontal movement on the same frame as a triangle-seam miss, a
    // downward AABB probe can be ambiguous. A normal-aware surface query must still recover floor contact.
    {
        SWalker walker;
        walker.feet = {0.f, 0.05f, 0.f};
        walker.vel = {5.f, -0.1f, 0.f};
        walker.onGround = false;
        walker.overlaps = [](const V3& feet, float /*height*/) {
            if (feet.y < 0.f) return true;                 // floor
            if (feet.x > 0.005f && feet.y < 0.1f) return true; // vertical obstacle side
            return false;
        };
        walker.surfaceBelow = [](float /*x*/, float /*z*/, float /*y*/) -> std::optional<float> { return 0.f; };
        walker.move(1.f / 60.f, 1.8f, false);
        assert(walker.onGround);
        assert(near(walker.feet.y, 0.f));
        assert(near(walker.vel.y, 0.f));
    }

    // A steep triangle can intersect the ground-probe ray above a valid floor. The filtered raycast must skip
    // that non-floor contact rather than treating it as the only hit; both triangles remain part of collision.
    {
        using hyprwalk::CCollision;
        using hyprwalk::SRayHit;
        CCollision collision;
        collision.addTriangle({-1.f, 0.2f, -1.f}, {1.f, 5.f, -1.f}, {0.f, 0.2f, 1.f}); // steep face
        collision.addTriangle({-10.f, 0.f, -10.f}, {0.f, 0.f, 10.f}, {10.f, 0.f, -10.f}); // floor
        collision.build();
        SRayHit hit;
        assert(collision.raycast({0.f, 2.f, 0.f}, {0.f, -1.f, 0.f}, 3.f, hit));
        assert(hit.t < 1.f && hit.normal.y < 0.5f);
        assert(collision.raycastUpward({0.f, 2.f, 0.f}, {0.f, -1.f, 0.f}, 3.f, 0.5f, hit));
        assert(near(hit.t, 2.f, 1e-3f));
        assert(hit.normal.y >= 0.5f);
    }

    // Small static props remain collidable but can be stepped onto from the ground without zeroing all horizontal
    // momentum. The same solver must not step through a tall obstacle.
    {
        using hyprwalk::CCollision;
        using hyprwalk::SAABB;
        CCollision collision;
        collision.addBox(SAABB{{-10.f, -0.2f, -10.f}, {10.f, 0.f, 10.f}}); // floor
        collision.addBox(SAABB{{0.6f, 0.f, -1.f}, {0.8f, 0.25f, 1.f}}); // low prop
        SWalker walker;
        walker.feet = {0.f, 0.f, 0.f};
        walker.vel = {3.f, 0.f, 0.f};
        walker.onGround = true;
        walker.overlaps = [&collision](const V3& feet, float height) {
            return collision.overlaps(SAABB{{feet.x - SWalker::RADIUS, feet.y, feet.z - SWalker::RADIUS},
                                             {feet.x + SWalker::RADIUS, feet.y + height, feet.z + SWalker::RADIUS}});
        };
        walker.move(0.2f, 1.8f, false);
        assert(walker.feet.x > 0.5f);
        assert(walker.feet.y >= 0.f && walker.feet.y <= SWalker::STEP_HEIGHT);
        assert(walker.onGround);
        assert(near(walker.vel.x, 3.f));

        CCollision tallCollision;
        tallCollision.addBox(SAABB{{-10.f, -0.2f, -10.f}, {10.f, 0.f, 10.f}});
        tallCollision.addBox(SAABB{{0.6f, 0.f, -1.f}, {0.8f, 2.f, 1.f}}); // too tall to step
        walker.feet = {0.f, 0.f, 0.f};
        walker.vel = {3.f, 0.f, 0.f};
        walker.onGround = true;
        walker.overlaps = [&tallCollision](const V3& feet, float height) {
            return tallCollision.overlaps(SAABB{{feet.x - SWalker::RADIUS, feet.y, feet.z - SWalker::RADIUS},
                                                 {feet.x + SWalker::RADIUS, feet.y + height, feet.z + SWalker::RADIUS}});
        };
        walker.move(0.2f, 1.8f, false);
        assert(walker.feet.x < 0.31f);
        assert(near(walker.vel.x, 0.f));
    }

    std::cout << "walker ground-snap and prop-collision checks passed\n";
    return 0;
}
