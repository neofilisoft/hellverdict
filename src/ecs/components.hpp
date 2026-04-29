#pragma once
// HellVerdict — components.hpp
// Copyright © 2026 Neofilisoft / Studio Balmung
// All ECS components for EnTT registry.
// Math via GLM (column-major, matches Vulkan/SPIR-V layout).

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/norm.hpp>

#include <cstdint>
#include <string>
#include <array>

namespace HellVerdict {

// ─────────────────────────────────────────────────────────────────────────────
// Transform
// ─────────────────────────────────────────────────────────────────────────────
struct CTransform {
    glm::vec3 position {0.f};
    glm::vec3 rotation {0.f};   // Euler degrees XYZ
    glm::vec3 scale    {1.f};

    glm::mat4 matrix() const {
        glm::mat4 m = glm::mat4(1.f);
        m = glm::translate(m, position);
        m = glm::rotate(m, glm::radians(rotation.y), {0,1,0});
        m = glm::rotate(m, glm::radians(rotation.x), {1,0,0});
        m = glm::rotate(m, glm::radians(rotation.z), {0,0,1});
        m = glm::scale(m, scale);
        return m;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Physics / Velocity
// ─────────────────────────────────────────────────────────────────────────────
struct CVelocity {
    glm::vec3 linear  {0.f};
    float     yaw_vel  = 0.f;  // angular velocity for smooth turn
};

// AABB (axis-aligned bounding box) for collision
struct CAABB {
    glm::vec3 half_extents {0.35f, 0.9f, 0.35f};

    glm::vec3 min(const glm::vec3& center) const { return center - half_extents; }
    glm::vec3 max(const glm::vec3& center) const { return center + half_extents; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Camera / Player
// ─────────────────────────────────────────────────────────────────────────────
struct CCamera {
    float yaw       = 0.f;     // degrees
    float pitch     = 0.f;     // degrees, clamped ±89
    float fov       = 70.f;    // degrees
    float near_z    = 0.05f;
    float far_z     = 200.f;
    float eye_height = 1.65f;

    glm::vec3 forward() const {
        float yr = glm::radians(yaw);
        float pr = glm::radians(pitch);
        return glm::normalize(glm::vec3{
            std::sin(yr) * std::cos(pr),
            std::sin(pr),
            std::cos(yr) * std::cos(pr)
        });
    }
    glm::vec3 forward_xz() const {
        float yr = glm::radians(yaw);
        return glm::normalize(glm::vec3{std::sin(yr), 0.f, std::cos(yr)});
    }
    glm::vec3 right() const {
        glm::vec3 fwd = forward_xz();
        return glm::vec3{fwd.z, 0.f, -fwd.x};
    }
};

struct CPlayer {
    float health     = 100.f;
    float max_health = 100.f;
    float pain_flash = 0.f;   // 0..1 red vignette
    bool  alive      = true;

    // Weapon
    enum class Weapon : uint8_t { Fist=0, Pistol, Shotgun, COUNT };
    Weapon  active_weapon   = Weapon::Pistol;
    bool    has_shotgun     = false;
    int     ammo_shells     = 0;
    float   weapon_cooldown = 0.f;
    float   bob_phase       = 0.f;
    float   bob_offset      = 0.f;

    // Score / kills (carried across level transitions)
    int score  = 0;
    int kills  = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Enemy AI
// ─────────────────────────────────────────────────────────────────────────────
enum class EnemyType : uint8_t { Zombie=0, Imp, Demon, Baron, Cacodemon };

enum class AIState : uint8_t { Idle, Chase, Attack, Pain, Dead };

struct CEnemy {
    EnemyType type      = EnemyType::Zombie;
    AIState   ai_state  = AIState::Idle;
    float     health    = 60.f;
    float     attack_cd = 0.f;
    float     pain_timer = 0.f;
    bool      alive     = true;

    // Cached def values (set at spawn)
    float move_speed    = 2.f;
    float attack_range  = 1.5f;
    float attack_damage = 10.f;
    float attack_cooldown_def = 1.f;
    float aggro_range   = 14.f;
    float radius        = 0.55f;

    glm::vec3 color     = {0.75f, 0.55f, 0.45f};
};

// ─────────────────────────────────────────────────────────────────────────────
// Renderable
// ─────────────────────────────────────────────────────────────────────────────

// LOD level (computed per-frame by visibility system)
enum class LODLevel : uint8_t { Full=0, Half, Quarter, Culled };

struct CRenderable {
    uint32_t  mesh_id    = 0;       // index into GPU mesh table
    uint32_t  texture_id = 0;       // index into TextureCache
    LODLevel  lod        = LODLevel::Full;
    bool      cast_shadow = true;
    glm::vec3 tint       = {1,1,1}; // color modulate
};

// Billboard sprite (enemies, pickups) — no mesh, just a colored quad
struct CBillboard {
    glm::vec3 color  = {1,1,1};
    float     width  = 1.f;
    float     height = 1.f;
    uint32_t  texture_id = 0;   // 0 = solid color
};

// ─────────────────────────────────────────────────────────────────────────────
// Pickups
// ─────────────────────────────────────────────────────────────────────────────
enum class PickupKind : uint8_t { Health, Ammo, Shotgun };

struct CPickup {
    PickupKind kind   = PickupKind::Health;
    float      amount = 25.f;
    bool       taken  = false;
    float      bob_phase = 0.f;   // animated float
};

// ─────────────────────────────────────────────────────────────────────────────
// Level exit trigger
// ─────────────────────────────────────────────────────────────────────────────
struct CExitTrigger {
    glm::vec3 half = {0.5f, 2.f, 0.5f};
};

// ─────────────────────────────────────────────────────────────────────────────
// Chunk (world partitioning for LOD/frustum culling)
// ─────────────────────────────────────────────────────────────────────────────
struct CChunk {
    glm::ivec2 coord;           // chunk index (each chunk = 8×8 tiles)
    glm::vec3  aabb_min;
    glm::vec3  aabb_max;
    uint32_t   vbo_offset = 0;  // offset into the merged world VBO
    uint32_t   index_count = 0;
    LODLevel   lod = LODLevel::Full;
    bool       dirty = true;    // needs GPU upload
};

// ─────────────────────────────────────────────────────────────────────────────
// Previous-tick snapshot (for render interpolation)
// ─────────────────────────────────────────────────────────────────────────────
struct CPrevTransform {
    glm::vec3 position {0.f};
    float     yaw       = 0.f;
    float     pitch     = 0.f;
};

} // namespace HellVerdict
