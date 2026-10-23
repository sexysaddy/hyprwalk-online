#include "walker.hpp"

#include <algorithm>
#include <cmath>

namespace hyprwalk {

    void SWalker::moveAxis(int axis, float d, bool& blocked, float height, bool fly) {
        blocked = false;
        if (d == 0.f)
            return;

        // small steps so nothing is tunneled through
        const int   steps = std::max(1, (int)std::ceil(std::abs(d) / 0.1f));
        const float step  = d / steps;
        for (int i = 0; i < steps; ++i) {
            V3 p = feet;
            (&p.x)[axis] += step;
            if (!overlaps(p, height)) {
                feet = p;
                continue;
            }

            // walk up small ledges (stairs)
            if (axis != 1 && onGround && !fly) {
                V3 up = p;
                up.y += STEP_HEIGHT;
                V3 upFrom = feet;
                upFrom.y += STEP_HEIGHT;
                if (!overlaps(upFrom, height) && !overlaps(up, height)) {
                    feet       = up;
                    bool       dummy     = false;
                    const bool wasGround = onGround;
                    moveAxis(1, -STEP_HEIGHT, dummy, height, fly); // settle on top of it
                    onGround = wasGround;
                    continue;
                }
            }

            // binary search the contact point for a snug stop
            float lo = 0.f, hi = 1.f;
            for (int k = 0; k < 8; ++k) {
                const float mid = (lo + hi) * 0.5f;
                V3          q   = feet;
                (&q.x)[axis] += step * mid;
                if (overlaps(q, height))
                    hi = mid;
                else
                    lo = mid;
            }
            (&feet.x)[axis] += step * lo;
            blocked = true;
            return;
        }
    }

    void SWalker::move(float dt, float height, bool fly) {
        const bool  wasGround = onGround && !fly;
        const float y0        = feet.y;
        bool        blocked   = false;
        bool        horizontalBlocked = false;
        moveAxis(0, vel.x * dt, blocked, height, fly);
        if (blocked) {
            vel.x = 0;
            horizontalBlocked = true;
        }
        moveAxis(2, vel.z * dt, blocked, height, fly);
        if (blocked) {
            vel.z = 0;
            horizontalBlocked = true;
        }
        const float vy = vel.y;
        moveAxis(1, vy * dt, blocked, height, fly);
        if (blocked) {
            onGround = vy < 0;
            vel.y    = 0;
        } else if (!fly) {
            onGround = false;
            // snap down onto a lower stair or slope, up to STEP_HEIGHT, instead of falling off every step
            if (wasGround && vy <= 0) {
                const V3 at   = feet;
                bool     down = false;
                moveAxis(1, -STEP_HEIGHT, down, height, fly);
                if (down)
                    onGround = true, vel.y = 0;
                else
                    feet = at;
            }
            // A triangulated floor can have seams where the horizontal sweep misses a floor contact for one
            // frame. Without this small probe, wasGround is false on the next frame too, so the player can
            // unexpectedly lose the bhop chain. Only probe while descending and only by a short distance;
            // real jumps and falls keep their vertical velocity and are not snapped from farther away.
            // A downward probe beside a wall can collide with the wall's side and look like a floor contact.
            // Skip it for this frame after a horizontal collision; regular vertical collision still lands on top.
            if (!onGround && vy <= 0.f && vy >= -1.5f && !horizontalBlocked) {
                const V3 beforeProbe = feet;
                bool snapped = false;
                moveAxis(1, -GROUND_SNAP, snapped, height, fly);
                if (snapped) {
                    onGround = true;
                    vel.y = 0.f;
                } else {
                    // moveAxis advances through clear space; a probe must not turn into extra downward velocity.
                    feet = beforeProbe;
                }
            }

            // If a horizontal collision happened on the same frame as a floor-seam miss, the AABB-only downward
            // probe above is intentionally skipped: a prop's vertical side could be mistaken for a floor. When the
            // caller can raycast the actual surface, use its upward-facing hit normal to recover grounded state
            // safely even in that case. Only snap down a short distance; never lift the player onto a ledge.
            if (!onGround && vy <= 0.f && surfaceBelow) {
                if (const auto groundY = surfaceBelow(feet.x, feet.z, feet.y)) {
                    const float delta = feet.y - *groundY;
                    if (delta >= -0.02f && delta <= GROUND_SNAP) {
                        V3 groundedFeet = feet;
                        groundedFeet.y = *groundY;
                        if (!overlaps || !overlaps(groundedFeet, height)) {
                            feet = groundedFeet;
                            onGround = true;
                            vel.y = 0.f;
                        }
                    }
                }
            }
        }

        // visible height: an exact critically damped step toward the feet moving on at the climb rate
        if (!(std::abs(feet.y - seenY) <= 1.f)) // first time, or put somewhere else
            seenY = feet.y, seenV = climb = 0;
        const bool  walking = onGround && !fly;
        // landing: reset the climb (a fall isn't a slope; the view would sink) and keep LAND_SEEN of the fall speed
        if (walking && !wasGround)
            climb = 0, seenV *= LAND_SEEN;
        const float rdt     = std::max(dt, 1e-4f);
        climb               = walking ? climb + ((feet.y - y0) / rdt - climb) * (1.f - std::exp(-dt * CLIMB_K)) : vel.y;
        const float lead = walking ? SEEN_LEAD * climb : climb, w = walking ? SEEN_W : SEEN_AIR_W, e = std::exp(-w * dt);
        const float x0 = seenY - (feet.y - lead * dt), v0 = seenV - lead, k = v0 + w * x0;
        seenY = feet.y + std::clamp((x0 + k * dt) * e, -STEP_HEIGHT, STEP_HEIGHT);
        seenV = lead + (v0 - w * k * dt) * e;
    }

    std::optional<float> groundUnder(const CCollision& col, float x, float z, float y) {
        // Skip non-floor hits rather than checking only the nearest collision. A nearly vertical/sloped triangle
        // can cross this ray before the floor; rejecting that first normal must not hide the valid floor below.
        if (SRayHit hit; col.raycastUpward({x, y + 0.6f, z}, {0, -1, 0}, 1.4f, 0.5f, hit))
            return y + 0.6f - hit.t;
        return std::nullopt;
    }
}
