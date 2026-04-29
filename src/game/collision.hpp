#pragma once
// HellVerdict — collision.hpp
// Copyright © 2026 Neofilisoft / Studio Balmung
// AABB collision: sweep test, wall-sliding, epsilon-guarded penetration

#include "types.hpp"
#include <vector>

namespace HellVerdict {

// ── Sweep result ─────────────────────────────────────────────────────────────
struct SweepHit {
    float  t;        // [0..1] first contact time along delta
    Vec3   normal;   // surface normal at contact
    bool   hit;
};

// ── Wall cell box (static) ────────────────────────────────────────────────────
struct WallBox {
    AABB box;
};

// ── Collision solver ─────────────────────────────────────────────────────────
class Collision {
public:
    // Sweep moving AABB along delta vs one static AABB.
    // Returns normalized contact time and normal; t=1 means no hit.
    static SweepHit sweep_aabb(const AABB& moving,
                               const Vec3&  delta,
                               const AABB&  obstacle);

    // Resolve moving AABB against a list of static walls with wall-sliding.
    // Modifies pos and velocity in-place; performs up to 3 solver iterations.
    static void slide_and_resolve(Vec3&              pos,
                                  Vec3&              vel,
                                  const Vec3&        half_extents,
                                  float              dt,
                                  const std::vector<WallBox>& walls);

    // Point-in-AABB (epsilon-expanded)
    static bool point_in_aabb(Vec3 pt, const AABB& box) {
        return pt.x >= box.min.x - EPSILON && pt.x <= box.max.x + EPSILON
            && pt.y >= box.min.y - EPSILON && pt.y <= box.max.y + EPSILON
            && pt.z >= box.min.z - EPSILON && pt.z <= box.max.z + EPSILON;
    }

    // Ray vs AABB (slab method). Returns t of first hit or -1.
    static float ray_vs_aabb(Vec3 origin, Vec3 dir_inv, const AABB& box);

    // Sphere vs AABB closest-point test (for melee / explosion range)
    static bool sphere_vs_aabb(Vec3 center, float radius, const AABB& box);
};

} // namespace HellVerdict
