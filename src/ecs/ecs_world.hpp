#pragma once
// HellVerdict — ecs_world.hpp
// Copyright © 2026 Neofilisoft / Studio Balmung
// Thin wrapper around entt::registry.
// Systems are free functions that operate on the registry.

#include "components.hpp"
#include <entt/entt.hpp>
#include <functional>

namespace HellVerdict {

// ── Registry alias ────────────────────────────────────────────────────────────
using Registry = entt::registry;
using Entity   = entt::entity;
constexpr Entity NULL_ENTITY = entt::null;

// ── ECSWorld: owns the registry + player entity reference ─────────────────────
class ECSWorld {
public:
    ECSWorld();

    Registry& reg() { return _reg; }
    const Registry& reg() const { return _reg; }

    // Convenience: create and destroy entities
    Entity create()           { return _reg.create(); }
    void   destroy(Entity e)  { if (_reg.valid(e)) _reg.destroy(e); }

    // Player entity (created at spawn, stays valid for level lifetime)
    Entity player_entity() const { return _player; }
    void   set_player(Entity e)  { _player = e; }

    // Clear everything (between levels)
    void clear();

private:
    Registry _reg;
    Entity   _player = NULL_ENTITY;
};

// ─────────────────────────────────────────────────────────────────────────────
// Systems (called by HellGame at fixed step)
// ─────────────────────────────────────────────────────────────────────────────

namespace Systems {

// Save previous-tick transforms for render interpolation
void snapshot_transforms(ECSWorld& world);

// Apply velocity to transforms (with collision against wall boxes)
// walls: flat array of AABB min/max from MapChunkSystem
struct WallBox { glm::vec3 min, max; };
void integrate_movement(ECSWorld& world, float dt,
                        const std::vector<WallBox>& walls);

// Enemy AI state machine
void update_ai(ECSWorld& world, float dt, Entity player,
               const std::vector<WallBox>& walls);

// Pickup collection
// Returns health+ammo changes for the player via callback
using PickupCallback = std::function<void(PickupKind, float)>;
void collect_pickups(ECSWorld& world, Entity player, PickupCallback cb);

// Exit trigger check — returns true if player reached exit
bool check_exit(ECSWorld& world, Entity player);

// LOD + frustum cull: assigns LODLevel to every CChunk and CEnemy
void update_lod(ECSWorld& world, const glm::mat4& view_proj,
                float near_full, float near_half, float near_quarter);

} // namespace Systems

} // namespace HellVerdict
