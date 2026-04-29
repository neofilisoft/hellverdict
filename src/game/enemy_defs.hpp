#pragma once
// HellVerdict — enemy_defs.hpp
// Copyright © 2026 Neofilisoft / Studio Balmung
// Stat tables for all enemy types. Scaled for 5-level difficulty ramp.

#include "../ecs/components.hpp"

namespace HellVerdict {

struct EnemyDef {
    float health;
    float move_speed;
    float attack_range;
    float attack_damage;
    float attack_cooldown;
    float aggro_range;
    float radius;
    glm::vec3 color;
    const char* tex_name;
};

inline const EnemyDef& get_def(EnemyType t) {
    static const EnemyDef defs[] = {
        // Zombie
        { 60.f,  1.8f, 1.2f, 10.f, 1.2f, 12.f, 0.5f,
          {0.75f,0.55f,0.45f}, "enemy_zombie" },
        // Imp
        { 80.f,  2.8f, 8.0f, 15.f, 1.6f, 16.f, 0.55f,
          {0.9f,0.45f,0.1f},   "enemy_imp"    },
        // Demon
        { 200.f, 4.0f, 1.4f, 30.f, 0.85f,20.f, 0.7f,
          {0.55f,0.05f,0.05f}, "enemy_demon"  },
        // Baron of Hell
        { 400.f, 3.5f, 10.f, 40.f, 1.2f, 24.f, 0.85f,
          {0.2f,0.6f,0.2f},    "enemy_baron"  },
        // Cacodemon
        { 300.f, 2.0f, 12.f, 35.f, 1.4f, 22.f, 0.9f,
          {0.7f,0.1f,0.6f},    "enemy_imp"    },  // reuses imp tex until modder replaces
    };
    return defs[(int)t];
}

// Populate CEnemy from def table
inline void apply_def(CEnemy& e, EnemyType type) {
    const auto& d     = get_def(type);
    e.type            = type;
    e.health          = d.health;
    e.move_speed      = d.move_speed;
    e.attack_range    = d.attack_range;
    e.attack_damage   = d.attack_damage;
    e.attack_cooldown_def = d.attack_cooldown;
    e.aggro_range     = d.aggro_range;
    e.radius          = d.radius;
    e.color           = d.color;
    e.alive           = true;
    e.ai_state        = AIState::Idle;
}

} // namespace HellVerdict
