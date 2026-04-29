#pragma once
// HellVerdict — player.hpp
// Copyright © 2026 Neofilisoft / Studio Balmung
// First-person player: movement, look, weapon slots, health

#include "types.hpp"
#include "collision.hpp"
#include "map_data.hpp"
#include <array>

namespace HellVerdict {

// ── Weapon slot ───────────────────────────────────────────────────────────────
enum class WeaponSlot { Fist = 0, Pistol, Shotgun, COUNT };

struct WeaponState {
    WeaponSlot slot        = WeaponSlot::Pistol;
    float      cooldown    = 0.f;   // time until next shot allowed
    int        ammo_shells = 0;     // shotgun ammo
    bool       has_shotgun = false;

    // Bob animation
    float bob_phase  = 0.f;
    float bob_offset = 0.f;        // vertical offset for render
};

// ── Player state (POD — interpolatable for render) ────────────────────────────
struct PlayerState {
    Vec3  pos      = {};            // feet position (Y = 0 at floor)
    Vec3  vel      = {};            // velocity (m/s)
    float yaw      = 0.f;          // degrees, 0 = +Z
    float pitch    = 0.f;          // degrees, clamped -89..89
    float health   = 100.f;
    float pain_flash = 0.f;        // red vignette timer
    bool  alive    = true;

    WeaponState weapon{};

    // Eye height above feet
    static constexpr float EYE_HEIGHT = 1.65f;
    Vec3  eye_pos() const { return { pos.x, pos.y + EYE_HEIGHT, pos.z }; }

    // Player AABB (feet-centered, slightly narrower than a tile)
    static constexpr Vec3  HALF = {0.35f, 0.9f, 0.35f};
    AABB aabb() const { return AABB::from_center({pos.x, pos.y + HALF.y, pos.z}, HALF); }

    // View vectors
    Vec3 forward_xz() const;   // horizontal forward
    Vec3 forward()    const;   // full 3D forward
    Vec3 right()      const;
};

// ── Player controller ─────────────────────────────────────────────────────────
class Player {
public:
    static constexpr float MOVE_SPEED   = 5.5f;   // m/s
    static constexpr float MOUSE_SENS   = 0.12f;  // deg per pixel
    static constexpr float ACCEL        = 60.f;   // m/s² ground acceleration
    static constexpr float FRICTION     = 12.f;   // velocity damping
    static constexpr float MAX_HEALTH   = 100.f;

    // Input flags (set by main loop each frame)
    struct Input {
        bool forward = false, back = false, left = false, right = false;
        bool fire    = false;
        float mouse_dx = 0.f, mouse_dy = 0.f;
        int   scroll = 0;
    };

    Player();
    void reset(Vec3 start_pos);

    // Called at FIXED_STEP (120 Hz)
    void fixed_update(const Input& input, const MapData& map, float dt);

    // Apply damage (from enemies, events)
    void take_damage(float dmg);
    void heal(float hp);
    void give_ammo(int shells);
    void give_shotgun();
    void give_weapon(WeaponSlot slot);

    // Getters
    const PlayerState& state()         const { return _cur; }
    const PlayerState& prev_state()    const { return _prev; }

    // True for one fixed tick after firing
    bool  just_fired() const { return _just_fired; }
    float fire_damage() const;   // Damage dealt this frame (0 if not fired)
    Vec3  fire_dir()   const { return _fire_dir; }
    float fire_range() const;    // Effective range of current weapon

    // Interpolated state for render (alpha in [0,1])
    PlayerState interpolate(float alpha) const;

private:
    PlayerState _cur, _prev;
    bool        _just_fired = false;
    Vec3        _fire_dir   = {};

    void _apply_movement(const Input& in, float dt);
    void _apply_look    (const Input& in);
    void _apply_weapon  (const Input& in, float dt);
    void _update_bob    (bool moving, float dt);
};

} // namespace HellVerdict
