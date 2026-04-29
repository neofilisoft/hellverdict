// HellVerdict — ecs_world.cpp
// Copyright © 2026 Neofilisoft / Studio Balmung

#include "ecs_world.hpp"
#include <glm/gtx/norm.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace HellVerdict {

// ─────────────────────────────────────────────────────────────────────────────
// ECSWorld
// ─────────────────────────────────────────────────────────────────────────────
ECSWorld::ECSWorld() = default;

void ECSWorld::clear() {
    _reg.clear();
    _player = NULL_ENTITY;
}

// ─────────────────────────────────────────────────────────────────────────────
// Systems
// ─────────────────────────────────────────────────────────────────────────────

namespace Systems {

static constexpr float EPSILON  = 1e-3f;
static constexpr float EPSILON2 = EPSILON * EPSILON;

// ── Sweep AABB (Minkowski) ────────────────────────────────────────────────────
struct SweepResult { float t = 1.f; glm::vec3 normal{0.f}; bool hit = false; };

static SweepResult sweep_aabb(glm::vec3 center, glm::vec3 half,
                               glm::vec3 delta,
                               glm::vec3 obs_min, glm::vec3 obs_max)
{
    SweepResult r;
    // Expanded obstacle (Minkowski)
    glm::vec3 emin = obs_min - half;
    glm::vec3 emax = obs_max + half;

    // Already inside?
    if (center.x > emin.x && center.x < emax.x &&
        center.y > emin.y && center.y < emax.y &&
        center.z > emin.z && center.z < emax.z) {
        r.hit = true; r.t = 0.f; return r;
    }

    float t_enter = 0.f, t_exit = 1.f;
    glm::vec3 hit_n{};

    auto axis = [&](float o, float d, float bmin, float bmax,
                    glm::vec3 neg_n, glm::vec3 pos_n) -> bool {
        if (std::abs(d) < EPSILON)
            return (o >= bmin - EPSILON && o <= bmax + EPSILON);
        float inv = 1.f / d;
        float t0  = (bmin - o) * inv;
        float t1  = (bmax - o) * inv;
        if (t0 > t1) std::swap(t0, t1);
        if (t0 > t_enter) { t_enter = t0; hit_n = (d < 0) ? pos_n : neg_n; }
        t_exit = std::min(t_exit, t1);
        return true;
    };

    if (!axis(center.x, delta.x, emin.x, emax.x, {-1,0,0}, {1,0,0})) return r;
    if (!axis(center.y, delta.y, emin.y, emax.y, {0,-1,0}, {0,1,0})) return r;
    if (!axis(center.z, delta.z, emin.z, emax.z, {0,0,-1}, {0,0,1})) return r;

    if (t_enter < t_exit && t_enter >= 0.f && t_enter < 1.f) {
        r.hit = true; r.t = t_enter; r.normal = hit_n;
    }
    return r;
}

static void slide_resolve(glm::vec3& pos, glm::vec3& vel,
                           glm::vec3 half, float dt,
                           const std::vector<WallBox>& walls)
{
    glm::vec3 remaining = vel * dt;
    for (int iter = 0; iter < 3; ++iter) {
        if (glm::length2(remaining) < EPSILON2) break;
        float best_t = 1.f; glm::vec3 best_n{};

        for (auto& w : walls) {
            auto r = sweep_aabb(pos, half, remaining, w.min, w.max);
            if (r.hit && r.t < best_t) { best_t = r.t; best_n = r.normal; }
        }
        float safe_t = std::max(0.f, best_t - EPSILON);
        pos += remaining * safe_t;
        if (best_t >= 1.f) break;

        glm::vec3 leftover = remaining * (1.f - safe_t);
        leftover -= best_n * glm::dot(leftover, best_n);
        remaining = leftover;

        float into = glm::dot(vel, best_n);
        if (into < 0.f) vel -= best_n * into;
    }
}

// ── snapshot_transforms ───────────────────────────────────────────────────────
void snapshot_transforms(ECSWorld& world) {
    auto& reg = world.reg();
    auto view = reg.view<CTransform, CPrevTransform>();
    view.each([](const CTransform& t, CPrevTransform& p) {
        p.position = t.position;
    });
    // Player camera snapshot
    auto cview = reg.view<CCamera, CPrevTransform>();
    cview.each([](const CCamera& cam, CPrevTransform& p) {
        p.yaw   = cam.yaw;
        p.pitch = cam.pitch;
    });
}

// ── integrate_movement ────────────────────────────────────────────────────────
void integrate_movement(ECSWorld& world, float dt,
                        const std::vector<WallBox>& walls)
{
    auto& reg = world.reg();
    auto view = reg.view<CTransform, CVelocity, CAABB>();
    view.each([&](CTransform& t, CVelocity& v, const CAABB& aabb) {
        glm::vec3 center = t.position + glm::vec3{0, aabb.half_extents.y, 0};
        slide_resolve(center, v.linear, aabb.half_extents, dt, walls);
        t.position = center - glm::vec3{0, aabb.half_extents.y, 0};
        v.linear.y = 0.f;  // no gravity (Doom-style)
    });
}

// ── update_ai ─────────────────────────────────────────────────────────────────
static bool has_los(glm::vec3 from, glm::vec3 to,
                    const std::vector<WallBox>& walls)
{
    glm::vec3 delta = to - from;
    float dist = glm::length(delta);
    if (dist < EPSILON) return true;
    glm::vec3 dir = delta / dist;
    glm::vec3 inv {
        std::abs(dir.x) > EPSILON ? 1.f/dir.x : 1e9f,
        std::abs(dir.y) > EPSILON ? 1.f/dir.y : 1e9f,
        std::abs(dir.z) > EPSILON ? 1.f/dir.z : 1e9f
    };
    for (auto& w : walls) {
        float tx1=(w.min.x-from.x)*inv.x, tx2=(w.max.x-from.x)*inv.x;
        float ty1=(w.min.y-from.y)*inv.y, ty2=(w.max.y-from.y)*inv.y;
        float tz1=(w.min.z-from.z)*inv.z, tz2=(w.max.z-from.z)*inv.z;
        float tmin=std::max({std::min(tx1,tx2),std::min(ty1,ty2),std::min(tz1,tz2)});
        float tmax=std::min({std::max(tx1,tx2),std::max(ty1,ty2),std::max(tz1,tz2)});
        if (tmax>=0.f && tmin<=tmax && tmin<dist-EPSILON) return false;
    }
    return true;
}

void update_ai(ECSWorld& world, float dt, Entity player_ent,
               const std::vector<WallBox>& walls)
{
    auto& reg = world.reg();
    if (!reg.valid(player_ent)) return;

    auto& ptf  = reg.get<CTransform>(player_ent);
    auto& pply = reg.get<CPlayer>(player_ent);
    glm::vec3 player_pos = ptf.position;
    bool player_alive    = pply.alive;

    float& player_pain = pply.pain_flash;  // accumulate enemy damage here

    auto view = reg.view<CEnemy, CTransform, CVelocity>();
    view.each([&](CEnemy& e, CTransform& t, CVelocity& v) {
        if (!e.alive) { v.linear = {0,0,0}; return; }

        e.attack_cd  = std::max(0.f, e.attack_cd  - dt);
        e.pain_timer = std::max(0.f, e.pain_timer - dt);

        if (e.pain_timer > 0.f) { e.ai_state = AIState::Pain; v.linear={}; return; }

        glm::vec3 to_p   = player_pos - t.position;
        to_p.y           = 0.f;
        float dist_xz    = glm::length(to_p);
        glm::vec3 eye    = t.position + glm::vec3{0, e.radius*1.4f, 0};

        switch (e.ai_state) {
        case AIState::Idle:
            v.linear = {};
            if (dist_xz < e.aggro_range && has_los(eye, player_pos, walls))
                e.ai_state = AIState::Chase;
            break;

        case AIState::Chase:
            if (dist_xz <= e.attack_range + EPSILON) {
                e.ai_state = AIState::Attack; break;
            }
            if (dist_xz > EPSILON)
                v.linear = glm::normalize(to_p) * e.move_speed;
            else
                v.linear = {};
            if (dist_xz > e.aggro_range*1.5f ||
                !has_los(eye, player_pos, walls))
                e.ai_state = AIState::Idle;
            break;

        case AIState::Attack:
            v.linear = {};
            if (dist_xz > e.attack_range*1.1f) {
                e.ai_state = AIState::Chase; break;
            }
            if (e.attack_cd <= 0.f && player_alive) {
                pply.health -= e.attack_damage;
                pply.pain_flash = 0.45f;
                if (pply.health <= 0.f) { pply.health=0.f; pply.alive=false; }
                e.attack_cd = e.attack_cooldown_def;
            }
            break;

        case AIState::Pain:
            e.ai_state = AIState::Chase; break;

        case AIState::Dead:
            v.linear = {}; break;
        }
    });
}

// ── collect_pickups ───────────────────────────────────────────────────────────
void collect_pickups(ECSWorld& world, Entity player_ent, PickupCallback cb) {
    auto& reg = world.reg();
    if (!reg.valid(player_ent)) return;
    auto& ptf  = reg.get<CTransform>(player_ent);

    auto view = reg.view<CPickup, CTransform>();
    view.each([&](CPickup& pk, const CTransform& t) {
        if (pk.taken) return;
        glm::vec2 dp = {ptf.position.x - t.position.x,
                        ptf.position.z - t.position.z};
        if (glm::length2(dp) < 0.7f*0.7f) {
            pk.taken = true;
            if (cb) cb(pk.kind, pk.amount);
        }
        // Animate bob
        pk.bob_phase += 0.016f * 3.f;  // approx, exact dt not passed here
    });
}

// ── check_exit ────────────────────────────────────────────────────────────────
bool check_exit(ECSWorld& world, Entity player_ent) {
    auto& reg = world.reg();
    if (!reg.valid(player_ent)) return false;
    auto& ptf = reg.get<CTransform>(player_ent);

    bool triggered = false;
    auto view = reg.view<CExitTrigger, CTransform>();
    view.each([&](const CExitTrigger& ex, const CTransform& t) {
        glm::vec3 d = glm::abs(ptf.position - t.position);
        if (d.x < ex.half.x && d.y < ex.half.y && d.z < ex.half.z)
            triggered = true;
    });
    return triggered;
}

// ── update_lod ────────────────────────────────────────────────────────────────
// Frustum planes extraction from view-proj matrix (Gribb/Hartmann method)
struct Frustum {
    glm::vec4 planes[6]; // left,right,bottom,top,near,far
};

static Frustum extract_frustum(const glm::mat4& vp) {
    Frustum f;
    // Row-major access (GLM is column-major)
    auto row = [&](int r) {
        return glm::vec4{vp[0][r], vp[1][r], vp[2][r], vp[3][r]};
    };
    glm::vec4 r0=row(0),r1=row(1),r2=row(2),r3=row(3);
    f.planes[0] = r3 + r0; // left
    f.planes[1] = r3 - r0; // right
    f.planes[2] = r3 + r1; // bottom
    f.planes[3] = r3 - r1; // top
    f.planes[4] = r3 + r2; // near
    f.planes[5] = r3 - r2; // far
    // Normalize
    for (auto& p : f.planes) p /= glm::length(glm::vec3(p));
    return f;
}

static bool aabb_in_frustum(const Frustum& f, glm::vec3 mn, glm::vec3 mx) {
    for (auto& p : f.planes) {
        glm::vec3 n{p};
        glm::vec3 pos_v{
            p.x >= 0 ? mx.x : mn.x,
            p.y >= 0 ? mx.y : mn.y,
            p.z >= 0 ? mx.z : mn.z
        };
        if (glm::dot(n, pos_v) + p.w < -EPSILON) return false;
    }
    return true;
}

void update_lod(ECSWorld& world, const glm::mat4& view_proj,
                float near_full, float near_half, float near_quarter)
{
    Frustum frustum = extract_frustum(view_proj);
    auto& reg = world.reg();

    // Chunks
    reg.view<CChunk>().each([&](CChunk& chunk) {
        if (!aabb_in_frustum(frustum, chunk.aabb_min, chunk.aabb_max)) {
            chunk.lod = LODLevel::Culled; return;
        }
        // Distance from near plane to chunk center
        glm::vec3 center = (chunk.aabb_min + chunk.aabb_max) * 0.5f;
        float dist = glm::length(center); // rough proxy — refine with player pos
        chunk.lod = dist < near_full   ? LODLevel::Full
                  : dist < near_half   ? LODLevel::Half
                  : dist < near_quarter? LODLevel::Quarter
                  :                      LODLevel::Culled;
    });

    // Enemies
    reg.view<CEnemy, CTransform, CAABB>().each(
        [&](CEnemy& e, const CTransform& t, const CAABB& aabb) {
        if (!e.alive) return;
        glm::vec3 mn = aabb.min(t.position + glm::vec3{0,aabb.half_extents.y,0});
        glm::vec3 mx = aabb.max(t.position + glm::vec3{0,aabb.half_extents.y,0});
        // Enemies just get culled if outside frustum; no LOD tiers (billboard is always 1 quad)
        (void)mn; (void)mx;
    });
}

} // namespace Systems
} // namespace HellVerdict
