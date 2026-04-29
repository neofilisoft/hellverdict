// HellVerdict — player.cpp
// Copyright © 2026 Neofilisoft / Studio Balmung

#include "player.hpp"
#include <cmath>
#include <algorithm>

namespace HellVerdict {

// ── PlayerState helpers ───────────────────────────────────────────────────────

Vec3 PlayerState::forward_xz() const {
    float yr = yaw * DEG2RAD;
    return Vec3{ std::sin(yr), 0.f, std::cos(yr) }.norm();
}

Vec3 PlayerState::forward() const {
    float yr = yaw   * DEG2RAD;
    float pr = pitch * DEG2RAD;
    float cp = std::cos(pr);
    return Vec3{ std::sin(yr)*cp, std::sin(pr), std::cos(yr)*cp }.norm();
}

Vec3 PlayerState::right() const {
    Vec3 fwd = forward_xz();
    return Vec3{ fwd.z, 0.f, -fwd.x };
}

// ── Player ───────────────────────────────────────────────────────────────────

Player::Player() { reset({}); }

void Player::reset(Vec3 start_pos) {
    _cur              = {};
    _cur.pos          = start_pos;
    _cur.health       = MAX_HEALTH;
    _cur.alive        = true;
    _cur.weapon.slot  = WeaponSlot::Pistol;
    _cur.weapon.ammo_shells = 0;
    _prev = _cur;
    _just_fired = false;
}

void Player::fixed_update(const Input& input, const MapData& map, float dt) {
    _prev        = _cur;
    _just_fired  = false;

    if (!_cur.alive) return;

    _apply_look    (input);
    _apply_movement(input, dt);
    _apply_weapon  (input, dt);

    // Decay pain flash
    _cur.pain_flash = std::max(0.f, _cur.pain_flash - dt * 2.5f);

    // Weapon bob
    bool moving = (std::abs(_cur.vel.x) + std::abs(_cur.vel.z)) > 0.5f;
    _update_bob(moving, dt);
}

void Player::_apply_look(const Input& in) {
    _cur.yaw   += in.mouse_dx * MOUSE_SENS;
    _cur.pitch -= in.mouse_dy * MOUSE_SENS;
    _cur.pitch  = clampf(_cur.pitch, -89.f, 89.f);

    // Wrap yaw
    while (_cur.yaw >  360.f) _cur.yaw -= 360.f;
    while (_cur.yaw < -360.f) _cur.yaw += 360.f;
}

void Player::_apply_movement(const Input& in, float dt) {
    // Desired velocity direction (XZ plane only — Doom-style, no vertical move)
    Vec3 fwd = _cur.forward_xz();
    Vec3 rgt = _cur.right();
    Vec3 wish{};
    if (in.forward) wish += fwd;
    if (in.back)    wish -= fwd;
    if (in.right)   wish += rgt;
    if (in.left)    wish -= rgt;

    if (wish.len2() > EPSILON2) wish = wish.norm();

    float wish_speed = MOVE_SPEED;

    // Ground acceleration (Quake-style): only adds velocity in wish direction
    float current_speed = dot(_cur.vel, wish);
    float add_speed     = wish_speed - current_speed;
    if (add_speed > 0.f) {
        float accel_speed = std::min(ACCEL * dt * wish_speed, add_speed);
        _cur.vel += wish * accel_speed;
    }

    // Friction (when not pressing movement keys or at all times on ground)
    if (wish.len2() < EPSILON2) {
        float speed = _cur.vel.len();
        if (speed > EPSILON) {
            float drop = speed * FRICTION * dt;
            float new_speed = std::max(0.f, speed - drop);
            _cur.vel *= (new_speed / speed);
        }
    }

    // Zero out Y velocity (no gravity in Doom-style)
    _cur.vel.y = 0.f;

    // Collision-aware move
    Collision::slide_and_resolve(_cur.pos, _cur.vel,
                                  PlayerState::HALF, dt,
                                  map.wall_boxes());
}

void Player::_apply_weapon(const Input& in, float dt) {
    auto& wp = _cur.weapon;

    // Weapon switch via scroll
    if (in.scroll > 0) {
        int s = ((int)wp.slot + 1) % (int)WeaponSlot::COUNT;
        if ((WeaponSlot)s == WeaponSlot::Shotgun && !wp.has_shotgun) s = 0;
        wp.slot = (WeaponSlot)s;
    } else if (in.scroll < 0) {
        int s = ((int)wp.slot - 1 + (int)WeaponSlot::COUNT) % (int)WeaponSlot::COUNT;
        if ((WeaponSlot)s == WeaponSlot::Shotgun && !wp.has_shotgun) s--;
        if (s < 0) s = (int)WeaponSlot::COUNT - 1;
        wp.slot = (WeaponSlot)s;
    }

    // Cooldown
    wp.cooldown = std::max(0.f, wp.cooldown - dt);

    // Fire
    if (in.fire && wp.cooldown <= 0.f) {
        bool can_fire = false;

        switch (wp.slot) {
        case WeaponSlot::Fist:
            can_fire    = true;
            wp.cooldown = 0.5f;
            break;
        case WeaponSlot::Pistol:
            can_fire    = true;
            wp.cooldown = 0.35f;
            break;
        case WeaponSlot::Shotgun:
            if (wp.ammo_shells > 0) {
                can_fire = true;
                wp.ammo_shells--;
                wp.cooldown = 0.85f;
            }
            break;
        default: break;
        }

        if (can_fire) {
            _just_fired = true;
            _fire_dir   = _cur.forward();
        }
    }
}

void Player::_update_bob(bool moving, float dt) {
    auto& wp = _cur.weapon;
    if (moving) {
        wp.bob_phase += dt * 8.5f;
    } else {
        // Decay toward 0 smoothly
        wp.bob_phase += dt * 2.f;
    }
    float amp = moving ? 0.035f : 0.008f;
    wp.bob_offset = std::sin(wp.bob_phase) * amp;
}

// ── Damage / pickups ──────────────────────────────────────────────────────────

void Player::take_damage(float dmg) {
    if (!_cur.alive) return;
    _cur.health -= dmg;
    _cur.pain_flash = 0.45f;
    if (_cur.health <= 0.f) {
        _cur.health = 0.f;
        _cur.alive  = false;
    }
}

void Player::heal(float hp) {
    _cur.health = std::min(_cur.health + hp, MAX_HEALTH);
}

void Player::give_ammo(int shells) {
    _cur.weapon.ammo_shells += shells;
}

void Player::give_shotgun() {
    _cur.weapon.has_shotgun = true;
    _cur.weapon.slot        = WeaponSlot::Shotgun;
    _cur.weapon.ammo_shells += 8;
}

void Player::give_weapon(WeaponSlot slot) {
    _cur.weapon.slot = slot;
}

// ── Fire stats ────────────────────────────────────────────────────────────────

float Player::fire_damage() const {
    if (!_just_fired) return 0.f;
    switch (_cur.weapon.slot) {
    case WeaponSlot::Fist:    return 20.f;
    case WeaponSlot::Pistol:  return 12.f;
    case WeaponSlot::Shotgun: return 7.f * 7.f; // 7 pellets × 7 dmg
    default:                  return 0.f;
    }
}

float Player::fire_range() const {
    switch (_cur.weapon.slot) {
    case WeaponSlot::Fist:    return 1.5f;
    case WeaponSlot::Pistol:  return 50.f;
    case WeaponSlot::Shotgun: return 14.f;
    default:                  return 0.f;
    }
}

// ── Interpolation ─────────────────────────────────────────────────────────────

PlayerState Player::interpolate(float alpha) const {
    PlayerState s = _cur;
    s.pos   = lerp(_prev.pos,   _cur.pos,   alpha);
    s.yaw   = lerpf(_prev.yaw,  _cur.yaw,   alpha);
    s.pitch = lerpf(_prev.pitch,_cur.pitch,  alpha);
    return s;
}

} // namespace HellVerdict
