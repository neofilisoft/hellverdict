#pragma once
// HellVerdict — enemy.hpp
// Copyright © 2026 Neofilisoft / Studio Balmung
// Enemy types: Zombie, Imp, Demon — state machine AI

#include "types.hpp"
#include "collision.hpp"
#include <vector>
#include <cstdint>

namespace HellVerdict {

// ── Enemy types ───────────────────────────────────────────────────────────────
enum class EnemyType { Zombie, Imp, Demon };

struct EnemyDef {
    float max_health;
    float move_speed;     // m/s
    float attack_range;   // metres
    float attack_damage;
    float attack_cooldown;
    float aggro_range;    // how far they detect player
    Color3 color;
    float  radius;        // for billboard size
};

inline EnemyDef get_enemy_def(EnemyType type) {
    switch (type) {
    case EnemyType::Zombie: return { 60.f, 1.8f, 1.2f, 10.f, 1.2f, 12.f, COL_FLESH,   0.55f };
    case EnemyType::Imp:    return { 80.f, 2.8f, 8.0f, 15.f, 1.8f, 16.f, COL_ORANGE,  0.6f  };
    case EnemyType::Demon:  return {200.f, 4.0f, 1.4f, 30.f, 0.9f, 20.f, COL_BLOOD,   0.75f };
    default:                return { 60.f, 2.0f, 1.5f, 10.f, 1.0f, 14.f, COL_DARKGRAY,0.5f  };
    }
}

// ── Enemy state machine ───────────────────────────────────────────────────────
enum class EnemyAIState { Idle, Chase, Attack, Pain, Dead };

// ── Enemy instance ────────────────────────────────────────────────────────────
struct Enemy {
    uint32_t    id;
    EnemyType   type;
    EnemyAIState ai_state   = EnemyAIState::Idle;
    Vec3        pos         = {};
    Vec3        vel         = {};
    float       health      = 100.f;
    float       attack_cd   = 0.f;
    float       pain_timer  = 0.f;   // stun after taking hit
    bool        alive       = true;

    // Previous pos for interpolation
    Vec3        prev_pos    = {};

    AABB aabb() const {
        float r = get_enemy_def(type).radius;
        return AABB::from_center({pos.x, pos.y + r, pos.z}, {r, r, r});
    }

    Vec3 eye_pos() const {
        float r = get_enemy_def(type).radius;
        return {pos.x, pos.y + r * 1.4f, pos.z};
    }

    Color3 get_color() const {
        Color3 c = get_enemy_def(type).color;
        // Flash white on pain
        float t = pain_timer * 4.f;
        t = clampf(t, 0.f, 1.f);
        return { lerpf(c.r, 1.f, t), lerpf(c.g, 1.f, t), lerpf(c.b, 1.f, t) };
    }
};

// ── EnemyManager ─────────────────────────────────────────────────────────────
class EnemyManager {
public:
    EnemyManager() = default;

    void add(EnemyType type, Vec3 world_pos);
    void clear();

    // Called at FIXED_STEP
    void fixed_update(const Vec3& player_pos, float dt,
                      const std::vector<WallBox>& walls,
                      bool player_alive);

    // Hit all enemies within range of fire_origin along fire_dir.
    // Returns total damage dealt (summed if multiple hit).
    float apply_fire(Vec3 fire_origin, Vec3 fire_dir, float range, float damage);

    // True if any enemy is in melee range and attacks this tick
    float drain_player_damage(); // returns accumulated damage to deal to player

    const std::vector<Enemy>& enemies() const { return _enemies; }
    int  alive_count() const;
    bool all_dead()    const { return alive_count() == 0; }

private:
    std::vector<Enemy> _enemies;
    uint32_t           _next_id     = 1;
    float              _player_dmg  = 0.f;  // accumulated player damage

    void _update_ai(Enemy& e, const Vec3& player_pos,
                    const std::vector<WallBox>& walls, float dt);
    bool _has_los(const Vec3& from, const Vec3& to,
                  const std::vector<WallBox>& walls) const;
};

} // namespace HellVerdict
