// HellVerdict — hell_game.cpp
// Copyright © 2026 Neofilisoft / Studio Balmung

#include "hell_game.hpp"
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>

namespace HellVerdict {

HellGame::HellGame(const std::string& maps_dir)
    : _maps_dir(maps_dir)
{}

bool HellGame::load_level(const std::string& filename) {
    _current_map = _maps_dir + "/" + filename;
    if (!_map.load(_current_map)) return false;

    // Init pickups tracking
    _pickup_taken.assign(_map.spawns().size(), false);
    return true;
}

void HellGame::start_game() {
    _phase       = GamePhase::Playing;
    _score       = 0;
    _dead_timer  = 0.f;
    _accumulator = 0.f;

    _player.reset(_map.player_start());
    _enemies.clear();

    // Spawn entities from map
    for (const auto& sp : _map.spawns()) {
        switch (sp.type) {
        case CellType::Zombie:  _enemies.add(EnemyType::Zombie, sp.world_pos); break;
        case CellType::Imp:     _enemies.add(EnemyType::Imp,    sp.world_pos); break;
        case CellType::Demon:   _enemies.add(EnemyType::Demon,  sp.world_pos); break;
        default: break;
        }
    }

    _emit("hell_verdict:game_start",
          "{\"level\":\"" + _current_map + "\","
          "\"enemies\":" + std::to_string(_enemies.alive_count()) + "}");

    std::cout << "[HellGame] Level loaded, "
              << _enemies.alive_count() << " enemies\n";
}

// ── Main update ───────────────────────────────────────────────────────────────
void HellGame::update(float frame_dt) {
    // Cap dt to prevent spiral of death on lag spikes
    float dt = std::min(frame_dt, 0.05f);

    if (_phase == GamePhase::Dead) {
        _dead_timer -= dt;
        if (_dead_timer <= 0.f)
            _phase = GamePhase::MainMenu;
        return;
    }
    if (_phase != GamePhase::Playing) return;

    _accumulator += dt;
    while (_accumulator >= FIXED_STEP) {
        _fixed_step();
        _accumulator -= FIXED_STEP;
    }
    _render_alpha = _accumulator / FIXED_STEP;
}

void HellGame::_fixed_step() {
    _player.fixed_update(_input, _map, FIXED_STEP);
    _enemies.fixed_update(_player.state().pos, FIXED_STEP,
                          _map.wall_boxes(), _player.state().alive);

    _fire_player();

    // Enemy damage to player
    float enemy_dmg = _enemies.drain_player_damage();
    if (enemy_dmg > 0.f) {
        _player.take_damage(enemy_dmg);
        _emit("hell_verdict:player_hit",
              "{\"damage\":" + jfloat("damage", enemy_dmg) +
              ",\"health\":" + jfloat("health", _player.state().health) + "}");
    }

    _check_pickups();
    _check_exit();

    // Player death
    if (!_player.state().alive && _phase == GamePhase::Playing) {
        _phase      = GamePhase::Dead;
        _dead_timer = RESPAWN_TIME;
        _emit("hell_verdict:player_dead",
              "{\"score\":" + std::to_string(_score) + "}");
    }
}

void HellGame::_fire_player() {
    if (!_player.just_fired()) return;

    float damage = _player.fire_damage();
    float range  = _player.fire_range();
    Vec3  origin = _player.state().eye_pos();
    Vec3  dir    = _player.fire_dir();

    float dealt = _enemies.apply_fire(origin, dir, range, damage);
    if (dealt > 0.f) {
        _score += (int)(dealt * 10.f);
        _emit("hell_verdict:enemy_hit",
              "{\"damage\":" + jfloat("d", dealt) +
              ",\"score\":"  + std::to_string(_score) + "}");
    }
}

void HellGame::_check_pickups() {
    const auto& spawns = _map.spawns();
    const Vec3& ppos   = _player.state().pos;

    for (int i = 0; i < (int)spawns.size(); ++i) {
        if (_pickup_taken[i]) continue;
        const auto& sp = spawns[i];

        // Only pickup-type spawns
        if (sp.type != CellType::Health  &&
            sp.type != CellType::Ammo    &&
            sp.type != CellType::Shotgun) continue;

        Vec2 dp = { ppos.x - sp.world_pos.x, ppos.z - sp.world_pos.z };
        if (dp.len2() < 0.7f * 0.7f) {
            _pickup_taken[i] = true;
            switch (sp.type) {
            case CellType::Health:
                _player.heal(25.f);
                _emit("hell_verdict:pickup",
                      "{\"type\":\"health\",\"amount\":25}");
                break;
            case CellType::Ammo:
                _player.give_ammo(12);
                _emit("hell_verdict:pickup",
                      "{\"type\":\"ammo\",\"amount\":12}");
                break;
            case CellType::Shotgun:
                _player.give_shotgun();
                _emit("hell_verdict:pickup",
                      "{\"type\":\"shotgun\"}");
                break;
            default: break;
            }
        }
    }
}

void HellGame::_check_exit() {
    if (!_map.has_exit()) return;
    const Vec3& ppos = _player.state().pos;
    Vec3 eye = _player.state().eye_pos();
    if (Collision::point_in_aabb(eye, _map.exit_trigger())) {
        _phase = GamePhase::Victory;
        _emit("hell_verdict:victory",
              "{\"score\":" + std::to_string(_score) + "}");
    }
}

// ── JSON helpers ──────────────────────────────────────────────────────────────
std::string HellGame::jfloat(const std::string& k, float v) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << v;
    return ss.str();
}
std::string HellGame::jint(const std::string& k, int v) {
    return std::to_string(v);
}

} // namespace HellVerdict
