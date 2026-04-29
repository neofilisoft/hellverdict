#pragma once
// HellVerdict — hell_game.hpp
// Copyright © 2026 Neofilisoft / Studio Balmung
// Top-level game: fixed-step update, render state, event dispatch to C# runtime

#include "player.hpp"
#include "enemy.hpp"
#include "map_data.hpp"
#include <string>
#include <functional>

namespace HellVerdict {

// ── Game state machine ────────────────────────────────────────────────────────
enum class GamePhase { MainMenu, Playing, Dead, Victory, LoadingLevel };
using MapPhase = GamePhase;  // alias used in member field declaration

// ── Events dispatched to C# scripting runtime ─────────────────────────────────
struct GameEvent {
    std::string name;
    std::string json;  // payload — simple hand-crafted JSON strings
};

// ── HellGame ──────────────────────────────────────────────────────────────────
class HellGame {
public:
    static constexpr float FIXED_STEP   = 1.f / 120.f;  // 120 Hz physics
    static constexpr float RESPAWN_TIME = 3.5f;          // dead → menu delay

    explicit HellGame(const std::string& maps_dir = "assets/maps");

    // Call once after window/renderer init
    bool load_level(const std::string& filename);
    void start_game();

    // ── Frame pump ────────────────────────────────────────────────────────────
    // Called every render frame with the raw frame dt
    void update(float frame_dt);

    // Input passthrough to player
    void apply_input(const Player::Input& in) { _input = in; }

    // ── Rendering data getters ────────────────────────────────────────────────
    // Interpolated player state for render
    PlayerState render_player(float alpha) const { return _player.interpolate(alpha); }
    const MapData&       map()     const { return _map; }
    const EnemyManager&  enemies() const { return _enemies; }
    GamePhase            phase()   const { return _phase; }
    int                  score()   const { return _score; }
    float                render_alpha() const { return _render_alpha; }

    // ── C# event callback (set by main.cpp) ───────────────────────────────────
    // Called once per event so the Mono bridge can dispatch it
    std::function<void(const GameEvent&)> on_event;

private:
    std::string  _maps_dir;
    std::string  _current_map;
    MapPhase     _phase         = GamePhase::MainMenu;
    float        _accumulator   = 0.f;
    float        _render_alpha  = 0.f;
    float        _dead_timer    = 0.f;
    int          _score         = 0;

    MapData      _map;
    Player       _player;
    EnemyManager _enemies;
    Player::Input _input;

    // Pickup tracking (index → collected)
    std::vector<bool> _pickup_taken;

    void _fixed_step();
    void _check_pickups();
    void _check_exit();
    void _fire_player();

    void _emit(const std::string& name, const std::string& json) {
        if (on_event) on_event({ name, json });
    }

    // Simple JSON helpers
    static std::string jfloat(const std::string& k, float v);
    static std::string jint  (const std::string& k, int v);
};

} // namespace HellVerdict
