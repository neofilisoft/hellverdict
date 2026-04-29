#pragma once
// HellVerdict — world_map.hpp
// Copyright © 2026 Neofilisoft / Studio Balmung
// Chunk-based world: 8×8 tile chunks, 3-level LOD (Full/Half/Quarter),
// automatic LOD selection by distance from camera.
// Geometry emitted per-chunk into a merged device-local VBO.

#include "../ecs/components.hpp"
#include "../ecs/ecs_world.hpp"
#include "../render/texture_cache.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace HellVerdict {

// ── Cell types (ASCII legend in .txt maps) ────────────────────────────────────
enum class CellType : uint8_t {
    Void=0, Wall='#', Floor='.', Player='P',
    Zombie='Z', Imp='I', Demon='D', Baron='B', Cacodemon='C',
    Health='H', Ammo='A', Shotgun='S', Exit='X',
    Wall2='W'   // alternate wall texture
};

inline bool cell_solid(CellType c) {
    return c == CellType::Wall || c == CellType::Wall2;
}

// ── Per-tile vertex layout ─────────────────────────────────────────────────────
struct WorldVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    uint32_t  tex_slot;   // index into texture array
};

// ── Chunk LOD mesh ─────────────────────────────────────────────────────────────
struct ChunkMesh {
    std::vector<WorldVertex> verts_full;     // 1:1 tiles
    std::vector<uint32_t>    idx_full;
    std::vector<WorldVertex> verts_half;     // 2×2 merged quads
    std::vector<uint32_t>    idx_half;
    std::vector<WorldVertex> verts_quarter;  // 4×4 merged quads
    std::vector<uint32_t>    idx_quarter;
};

// ── Spawn record ───────────────────────────────────────────────────────────────
struct SpawnRecord {
    CellType  type;
    glm::vec3 world_pos;
};

// ── WorldMap ──────────────────────────────────────────────────────────────────
class WorldMap {
public:
    static constexpr float TILE  = 1.f;
    static constexpr float WALL_H = 3.f;
    static constexpr int   CHUNK  = 8;    // tiles per chunk side

    bool load(const std::string& path, TextureCache& tex);
    void build_ecs(ECSWorld& world) const; // populate CChunk entities + spawns

    int width()  const { return _w; }
    int height() const { return _h; }

    CellType cell(int x, int z) const;
    glm::vec3 tile_center(int x, int z) const {
        return {(x+0.5f)*TILE, 0.f, (z+0.5f)*TILE};
    }

    const std::vector<SpawnRecord>&    spawns()    const { return _spawns; }
    const std::vector<Systems::WallBox>& wall_boxes() const { return _wall_boxes; }

    glm::vec3 player_start() const { return _player_start; }
    bool      has_exit()     const { return _has_exit; }
    glm::vec3 exit_pos()     const { return _exit_pos; }

    // Per-chunk LOD meshes (indexed by chunk_idx = cx + cz*_cw)
    const std::vector<ChunkMesh>& chunk_meshes() const { return _chunks; }
    int chunk_width()  const { return _cw; }
    int chunk_height() const { return _ch; }

private:
    int _w=0, _h=0, _cw=0, _ch=0;
    glm::vec3 _player_start{};
    bool      _has_exit = false;
    glm::vec3 _exit_pos{};

    std::vector<CellType>         _grid;
    std::vector<SpawnRecord>      _spawns;
    std::vector<Systems::WallBox> _wall_boxes;
    std::vector<ChunkMesh>        _chunks;

    // Texture slot IDs (filled at load time from TextureCache)
    uint32_t _tex_wall=0, _tex_wall2=0, _tex_floor=0, _tex_ceil=0;

    void _build_chunk(int cx, int cz, ChunkMesh& out);
    void _add_quad(std::vector<WorldVertex>& verts, std::vector<uint32_t>& idx,
                   glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
                   glm::vec3 normal, uint32_t tex, bool flip_uv = false);
};

} // namespace HellVerdict
