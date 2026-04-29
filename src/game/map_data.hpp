#pragma once
// HellVerdict — map_data.hpp
// Copyright © 2026 Neofilisoft / Studio Balmung
// ASCII grid map: loads sectors, builds collision geometry, generates render mesh

#include "types.hpp"
#include "collision.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace HellVerdict {

// ── Cell legend ───────────────────────────────────────────────────────────────
enum class CellType : uint8_t {
    Void   = 0,  // Outside map
    Wall   = '#',
    Floor  = '.',
    Player = 'P',
    Zombie = 'Z',
    Imp    = 'I',
    Demon  = 'D',
    Health = 'H',
    Ammo   = 'A',
    Shotgun= 'S',
    Exit   = 'X',
};

inline bool is_solid(CellType c)  { return c == CellType::Wall; }
inline bool is_walkable(CellType c) {
    return c != CellType::Wall && c != CellType::Void;
}

// ── Sector colors (Doom-style) ───────────────────────────────────────────────
struct Sector {
    Color3 wall_color   = COL_DARKGRAY;
    Color3 floor_color  = {0.12f, 0.12f, 0.12f};
    Color3 ceil_color   = {0.08f, 0.08f, 0.08f};
    float  ceil_height  = 3.0f;
    float  floor_height = 0.0f;
};

// ── Spawn record ───────────────────────────────────────────────────────────────
struct EntitySpawn {
    CellType type;
    Vec2     tile_pos;  // tile coordinates
    Vec3     world_pos; // world center
};

// ── Render vertex (packed for GPU upload) ─────────────────────────────────────
struct MapVertex {
    float px, py, pz;  // position
    float nx, ny, nz;  // normal
    float cr, cg, cb;  // color (no textures in v1)
};

struct MapMeshData {
    std::vector<MapVertex> vertices;
    std::vector<uint32_t>  indices;
    void clear() { vertices.clear(); indices.clear(); }
};

// ── Map ─────────────────────────────────────────────────────────────────────
class MapData {
public:
    static constexpr float TILE_SIZE   = 1.0f;
    static constexpr float WALL_HEIGHT = 3.0f;
    static constexpr float CEIL_Y      = WALL_HEIGHT;
    static constexpr float FLOOR_Y     = 0.0f;

    // Load from ASCII file.  Returns false on failure.
    bool load(const std::string& path);

    // Parse from string (for testing / embedded maps)
    void parse(const std::string& text);

    int width()  const { return _width; }
    int height() const { return _height; }

    CellType cell(int x, int z) const;
    Vec3     tile_to_world(int x, int z) const;  // center of tile

    // Player start position (world space)
    Vec3 player_start() const { return _player_start; }

    // Entity spawn list (enemies, pickups, exit)
    const std::vector<EntitySpawn>& spawns() const { return _spawns; }

    // Collision geometry (static walls)
    const std::vector<WallBox>& wall_boxes() const { return _wall_boxes; }

    // Render geometry (uploaded to GPU at map load)
    const MapMeshData& mesh() const { return _mesh; }

    // Exit trigger AABB
    const AABB& exit_trigger() const { return _exit_trigger; }
    bool        has_exit()     const { return _has_exit; }

private:
    int   _width  = 0;
    int   _height = 0;
    Vec3  _player_start{};
    bool  _has_exit = false;
    AABB  _exit_trigger{};

    std::vector<CellType>    _grid;
    std::vector<EntitySpawn> _spawns;
    std::vector<WallBox>     _wall_boxes;
    MapMeshData              _mesh;

    void _build_geometry();
    void _add_quad(const Vec3& p0, const Vec3& p1,
                   const Vec3& p2, const Vec3& p3,
                   const Vec3& normal, Color3 color);
};

} // namespace HellVerdict
