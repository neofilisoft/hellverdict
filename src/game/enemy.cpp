// HellVerdict — enemy.cpp
// Copyright © 2026 Neofilisoft / Studio Balmung

#include "enemy.hpp"
#include "collision.hpp"
#include <cmath>
#include <limits>

namespace HellVerdict {

void EnemyManager::add(EnemyType type, Vec3 world_pos) {
    Enemy e;
    e.id       = _next_id++;
    e.type     = type;
    e.pos      = world_pos;
    e.prev_pos = world_pos;
    e.health   = get_enemy_def(type).max_health;
    e.alive    = true;
    _enemies.push_back(e);
}

void EnemyManager::clear() {
    _enemies.clear();
    _next_id    = 1;
    _player_dmg = 0.f;
}

int EnemyManager::alive_count() const {
    int n = 0;
    for (auto& e : _enemies) if (e.alive) ++n;
    return n;
}

// ── Line-of-sight (ray vs wall boxes) ────────────────────────────────────────
bool EnemyManager::_has_los(const Vec3& from, const Vec3& to,
                              const std::vector<WallBox>& walls) const
{
    Vec3 delta  = to - from;
    float dist  = delta.len();
    if (dist < EPSILON) return true;

    Vec3 dir    = delta * (1.f / dist);
    // Safe reciprocal with epsilon guard
    Vec3 dir_inv{
        std::abs(dir.x) > EPSILON ? 1.f / dir.x : 1e9f,
        std::abs(dir.y) > EPSILON ? 1.f / dir.y : 1e9f,
        std::abs(dir.z) > EPSILON ? 1.f / dir.z : 1e9f
    };

    for (const auto& w : walls) {
        float t = Collision::ray_vs_aabb(from, dir_inv, w.box);
        if (t >= 0.f && t < dist - EPSILON) return false;
    }
    return true;
}

// ── Per-enemy AI update ───────────────────────────────────────────────────────
void EnemyManager::_update_ai(Enemy& e, const Vec3& player_pos,
                               const std::vector<WallBox>& walls, float dt)
{
    if (!e.alive) return;

    const EnemyDef& def = get_enemy_def(e.type);
    e.attack_cd = std::max(0.f, e.attack_cd - dt);
    e.pain_timer = std::max(0.f, e.pain_timer - dt);

    // Pain stun
    if (e.pain_timer > 0.f) {
        e.ai_state = EnemyAIState::Pain;
        e.vel = {};
        return;
    }

    Vec3 to_player = player_pos - e.pos;
    to_player.y    = 0.f;  // only XZ distance
    float dist_xz  = to_player.len();

    switch (e.ai_state) {

    case EnemyAIState::Idle:
        e.vel = {};
        if (dist_xz < def.aggro_range && _has_los(e.eye_pos(), player_pos, walls)) {
            e.ai_state = EnemyAIState::Chase;
        }
        break;

    case EnemyAIState::Chase: {
        if (dist_xz <= def.attack_range + EPSILON) {
            e.ai_state = EnemyAIState::Attack;
            break;
        }
        // Move toward player
        Vec3 dir = (dist_xz > EPSILON) ? to_player * (1.f / dist_xz) : Vec3{};
        e.vel = dir * def.move_speed;

        // Slide against walls
        Collision::slide_and_resolve(e.pos, e.vel,
            {def.radius*0.9f, def.radius, def.radius*0.9f}, dt, walls);

        // Re-eval aggro (can lose sight)
        if (dist_xz > def.aggro_range * 1.5f || !_has_los(e.eye_pos(), player_pos, walls))
            e.ai_state = EnemyAIState::Idle;
        break;
    }

    case EnemyAIState::Attack:
        e.vel = {};
        if (dist_xz > def.attack_range * 1.1f) {
            e.ai_state = EnemyAIState::Chase;
            break;
        }
        if (e.attack_cd <= 0.f) {
            _player_dmg += def.attack_damage;
            e.attack_cd  = def.attack_cooldown;
        }
        break;

    case EnemyAIState::Pain:
        // Handled above
        e.ai_state = EnemyAIState::Chase;
        break;

    case EnemyAIState::Dead:
        e.vel = {};
        break;
    }
}

void EnemyManager::fixed_update(const Vec3& player_pos, float dt,
                                 const std::vector<WallBox>& walls,
                                 bool player_alive)
{
    _player_dmg = 0.f;

    for (auto& e : _enemies) {
        if (!e.alive) continue;
        e.prev_pos = e.pos;
        if (player_alive)
            _update_ai(e, player_pos, walls, dt);
    }
}

// ── Apply weapon fire ─────────────────────────────────────────────────────────
float EnemyManager::apply_fire(Vec3 fire_origin, Vec3 fire_dir,
                                float range, float total_damage)
{
    float dealt = 0.f;
    Vec3 dir_inv{
        std::abs(fire_dir.x) > EPSILON ? 1.f / fire_dir.x : 1e9f,
        std::abs(fire_dir.y) > EPSILON ? 1.f / fire_dir.y : 1e9f,
        std::abs(fire_dir.z) > EPSILON ? 1.f / fire_dir.z : 1e9f
    };

    float closest_t = range;
    Enemy* hit_enemy = nullptr;

    for (auto& e : _enemies) {
        if (!e.alive) continue;
        float t = Collision::ray_vs_aabb(fire_origin, dir_inv, e.aabb());
        if (t >= 0.f && t < closest_t) {
            closest_t = t;
            hit_enemy = &e;
        }
    }

    if (hit_enemy) {
        hit_enemy->health -= total_damage;
        hit_enemy->pain_timer = 0.25f;
        dealt = total_damage;

        if (hit_enemy->health <= 0.f) {
            hit_enemy->health   = 0.f;
            hit_enemy->alive    = false;
            hit_enemy->ai_state = EnemyAIState::Dead;
        }
    }
    return dealt;
}

float EnemyManager::drain_player_damage() {
    float d     = _player_dmg;
    _player_dmg = 0.f;
    return d;
}

} // namespace HellVerdict
