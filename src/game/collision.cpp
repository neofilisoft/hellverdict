// HellVerdict — collision.cpp
// Copyright © 2026 Neofilisoft / Studio Balmung

#include "collision.hpp"
#include <cmath>
#include <limits>

namespace HellVerdict {

// ── Sweep AABB vs AABB ───────────────────────────────────────────────────────
//
// Uses the Minkowski sum: expand obstacle by moving's half-extents,
// then cast a ray from moving's center along delta.
// This is the standard separating-axis sweep for AABBs.
//
SweepHit Collision::sweep_aabb(const AABB& moving,
                                const Vec3&  delta,
                                const AABB&  obstacle)
{
    SweepHit result{ 1.f, {}, false };

    // Expanded obstacle (Minkowski sum)
    Vec3 mhalf = moving.half();
    AABB expanded {
        { obstacle.min.x - mhalf.x,
          obstacle.min.y - mhalf.y,
          obstacle.min.z - mhalf.z },
        { obstacle.max.x + mhalf.x,
          obstacle.max.y + mhalf.y,
          obstacle.max.z + mhalf.z }
    };

    Vec3 origin = moving.center();

    // If already overlapping (within epsilon), push out gently
    if (expanded.overlaps(AABB::from_center(origin, {EPSILON,EPSILON,EPSILON}))) {
        // Already penetrating — no sweep, caller handles push-out
        result.hit    = true;
        result.t      = 0.f;
        result.normal = {}; // Caller determines push direction
        return result;
    }

    // Slab intersection
    float t_enter = 0.f;
    float t_exit  = 1.f;
    Vec3  hit_normal{};

    auto process_axis = [&](float origin_v, float delta_v,
                             float box_min, float box_max,
                             Vec3 neg_normal, Vec3 pos_normal) -> bool
    {
        if (std::abs(delta_v) < EPSILON) {
            // Stationary on this axis — must already be in slab
            return (origin_v >= box_min - EPSILON && origin_v <= box_max + EPSILON);
        }
        float inv = 1.f / delta_v;
        float t0  = (box_min - origin_v) * inv;
        float t1  = (box_max - origin_v) * inv;
        if (t0 > t1) std::swap(t0, t1);

        if (t0 > t_enter) {
            t_enter    = t0;
            hit_normal = (delta_v < 0) ? pos_normal : neg_normal;
        }
        t_exit = std::min(t_exit, t1);
        return true;
    };

    if (!process_axis(origin.x, delta.x,
                      expanded.min.x, expanded.max.x,
                      {-1,0,0}, {1,0,0}))
        return result;

    if (!process_axis(origin.y, delta.y,
                      expanded.min.y, expanded.max.y,
                      {0,-1,0}, {0,1,0}))
        return result;

    if (!process_axis(origin.z, delta.z,
                      expanded.min.z, expanded.max.z,
                      {0,0,-1}, {0,0,1}))
        return result;

    if (t_enter < t_exit && t_enter >= 0.f && t_enter < 1.f) {
        result.hit    = true;
        result.t      = t_enter;
        result.normal = hit_normal;
    }
    return result;
}

// ── Wall sliding resolver ─────────────────────────────────────────────────────
//
// Up to 3 iterations: move as far as possible, project velocity against
// the collision normal (slide), repeat for remaining movement.
//
void Collision::slide_and_resolve(Vec3&              pos,
                                   Vec3&              vel,
                                   const Vec3&        half,
                                   float              dt,
                                   const std::vector<WallBox>& walls)
{
    constexpr int MAX_ITERS = 3;
    Vec3 remaining = vel * dt;

    for (int iter = 0; iter < MAX_ITERS; ++iter) {
        if (remaining.len2() < EPSILON2) break;

        float best_t = 1.f;
        Vec3  best_normal{};
        AABB  mover = AABB::from_center(pos, half);

        for (const auto& w : walls) {
            SweepHit hit = sweep_aabb(mover, remaining, w.box);
            if (hit.hit && hit.t < best_t) {
                best_t      = hit.t;
                best_normal = hit.normal;
            }
        }

        // Move to contact point (back off by epsilon to avoid sinking)
        float safe_t = std::max(0.f, best_t - EPSILON);
        pos += remaining * safe_t;

        if (best_t >= 1.f) {
            // No collision — consumed all movement
            break;
        }

        // Project remaining movement onto slide plane (perpendicular to normal)
        Vec3 leftover = remaining * (1.f - safe_t);
        float along   = dot(leftover, best_normal);
        remaining     = leftover - best_normal * along;

        // Also zero out the velocity component into the wall
        float vel_into = dot(vel, best_normal);
        if (vel_into < 0.f)
            vel -= best_normal * vel_into;
    }
}

// ── Ray vs AABB (slab test) ───────────────────────────────────────────────────
float Collision::ray_vs_aabb(Vec3 origin, Vec3 dir_inv, const AABB& box)
{
    float t1 = (box.min.x - origin.x) * dir_inv.x;
    float t2 = (box.max.x - origin.x) * dir_inv.x;
    float t3 = (box.min.y - origin.y) * dir_inv.y;
    float t4 = (box.max.y - origin.y) * dir_inv.y;
    float t5 = (box.min.z - origin.z) * dir_inv.z;
    float t6 = (box.max.z - origin.z) * dir_inv.z;

    float tmin = std::max({std::min(t1,t2), std::min(t3,t4), std::min(t5,t6)});
    float tmax = std::min({std::max(t1,t2), std::max(t3,t4), std::max(t5,t6)});

    if (tmax < 0.f || tmin > tmax) return -1.f;
    return (tmin < 0.f) ? tmax : tmin;
}

// ── Sphere vs AABB ─────────────────────────────────────────────────────────
bool Collision::sphere_vs_aabb(Vec3 center, float radius, const AABB& box)
{
    // Closest point on box to sphere center
    Vec3 closest {
        clampf(center.x, box.min.x, box.max.x),
        clampf(center.y, box.min.y, box.max.y),
        clampf(center.z, box.min.z, box.max.z)
    };
    Vec3 diff = center - closest;
    return diff.len2() < radius * radius;
}

} // namespace HellVerdict
