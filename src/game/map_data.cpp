// HellVerdict — map_data.cpp
// Copyright © 2026 Neofilisoft / Studio Balmung

#include "map_data.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

namespace HellVerdict {

// ── Load ──────────────────────────────────────────────────────────────────────
bool MapData::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << "[Map] Cannot open: " << path << "\n";
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(f)), {});
    parse(text);
    return true;
}

void MapData::parse(const std::string& text) {
    _grid.clear();
    _spawns.clear();
    _wall_boxes.clear();
    _mesh.clear();
    _has_exit = false;

    std::istringstream ss(text);
    std::string line;
    std::vector<std::string> lines;

    while (std::getline(ss, line)) {
        // Strip carriage returns
        while (!line.empty() && (line.back()=='\r' || line.back()=='\n'))
            line.pop_back();
        lines.push_back(line);
    }

    // Determine grid size
    _height = (int)lines.size();
    _width  = 0;
    for (auto& l : lines) _width = std::max(_width, (int)l.size());

    _grid.resize(_width * _height, CellType::Void);

    for (int z = 0; z < _height; ++z) {
        const auto& row = lines[z];
        for (int x = 0; x < _width; ++x) {
            char c = (x < (int)row.size()) ? row[x] : ' ';
            CellType ct = CellType::Void;

            switch (c) {
            case '#': ct = CellType::Wall;    break;
            case '.': ct = CellType::Floor;   break;
            case 'P': ct = CellType::Player;  break;
            case 'Z': ct = CellType::Zombie;  break;
            case 'I': ct = CellType::Imp;     break;
            case 'D': ct = CellType::Demon;   break;
            case 'H': ct = CellType::Health;  break;
            case 'A': ct = CellType::Ammo;    break;
            case 'S': ct = CellType::Shotgun; break;
            case 'X': ct = CellType::Exit;    break;
            default:  ct = CellType::Void;    break;
            }

            _grid[z * _width + x] = ct;

            Vec3 world_center = tile_to_world(x, z);

            if (ct == CellType::Player) {
                _player_start = world_center;
                _grid[z * _width + x] = CellType::Floor;
            } else if (ct == CellType::Exit) {
                _has_exit = true;
                _exit_trigger = AABB::from_center(world_center, {0.5f, 2.0f, 0.5f});
                _grid[z * _width + x] = CellType::Floor;
            } else if (ct != CellType::Wall && ct != CellType::Floor && ct != CellType::Void) {
                // Entity spawn
                EntitySpawn sp;
                sp.type      = ct;
                sp.tile_pos  = {(float)x, (float)z};
                sp.world_pos = world_center;
                _spawns.push_back(sp);
                _grid[z * _width + x] = CellType::Floor;
            }
        }
    }

    _build_geometry();
}

// ── Geometry ──────────────────────────────────────────────────────────────────

CellType MapData::cell(int x, int z) const {
    if (x < 0 || x >= _width || z < 0 || z >= _height) return CellType::Void;
    return _grid[z * _width + x];
}

Vec3 MapData::tile_to_world(int x, int z) const {
    return { (x + 0.5f) * TILE_SIZE, 0.f, (z + 0.5f) * TILE_SIZE };
}

void MapData::_add_quad(const Vec3& p0, const Vec3& p1,
                         const Vec3& p2, const Vec3& p3,
                         const Vec3& normal, Color3 col)
{
    auto base = (uint32_t)_mesh.vertices.size();
    float uvs[4][2] = {{0,0},{1,0},{1,1},{0,1}};
    Vec3  pts[4]    = {p0, p1, p2, p3};

    for (int i = 0; i < 4; ++i) {
        _mesh.vertices.push_back({
            pts[i].x, pts[i].y, pts[i].z,
            normal.x, normal.y, normal.z,
            col.r, col.g, col.b
        });
    }
    // Two triangles: 0,1,2 and 0,2,3
    _mesh.indices.insert(_mesh.indices.end(),
        { base+0, base+1, base+2, base+0, base+2, base+3 });
}

// Doom-style sector color based on grid position (slight variation)
static Color3 wall_color_for(int x, int z) {
    // Slight variation to break monotony — Doom had different sector brightnesses
    float h = 0.18f + 0.04f * (float)((x ^ z) & 3) * 0.25f;
    return {h, h, h};
}

void MapData::_build_geometry() {
    const float T = TILE_SIZE;
    const float CY = CEIL_Y;
    const float FY = FLOOR_Y;

    for (int z = 0; z < _height; ++z) {
        for (int x = 0; x < _width; ++x) {
            CellType ct = cell(x, z);
            float wx = (float)x * T;
            float wz = (float)z * T;

            if (ct == CellType::Wall) {
                // Add wall AABB for collision
                Vec3 wmin = { wx,       FY, wz       };
                Vec3 wmax = { wx + T,   CY, wz + T   };
                _wall_boxes.push_back({ AABB{wmin, wmax} });

                Color3 wc = wall_color_for(x, z);

                // Emit faces only adjacent to non-solid (exposed faces only)
                // -Z face (north wall face)
                if (!is_solid(cell(x, z-1))) {
                    Vec3 n = {0,0,-1};
                    _add_quad({wx,   FY, wz}, {wx+T, FY, wz},
                              {wx+T, CY, wz}, {wx,   CY, wz}, n, wc);
                }
                // +Z face (south wall face)
                if (!is_solid(cell(x, z+1))) {
                    Vec3 n = {0,0,1};
                    _add_quad({wx+T, FY, wz+T}, {wx,   FY, wz+T},
                              {wx,   CY, wz+T}, {wx+T, CY, wz+T}, n, wc);
                }
                // -X face (west wall face)
                if (!is_solid(cell(x-1, z))) {
                    Vec3 n = {-1,0,0};
                    _add_quad({wx,   FY, wz+T}, {wx,   FY, wz  },
                              {wx,   CY, wz  }, {wx,   CY, wz+T}, n, wc);
                }
                // +X face (east wall face)
                if (!is_solid(cell(x+1, z))) {
                    Vec3 n = {1,0,0};
                    _add_quad({wx+T, FY, wz  }, {wx+T, FY, wz+T},
                              {wx+T, CY, wz+T}, {wx+T, CY, wz  }, n, wc);
                }

            } else if (ct != CellType::Void) {
                // Floor quad
                Color3 fc = {0.12f, 0.11f, 0.10f};
                Vec3   fn = {0,1,0};
                _add_quad({wx,   FY, wz+T}, {wx+T, FY, wz+T},
                          {wx+T, FY, wz  }, {wx,   FY, wz  }, fn, fc);

                // Ceiling quad
                Color3 cc = {0.07f, 0.07f, 0.08f};
                Vec3   cn = {0,-1,0};
                _add_quad({wx,   CY, wz  }, {wx+T, CY, wz  },
                          {wx+T, CY, wz+T}, {wx,   CY, wz+T}, cn, cc);
            }
        }
    }

    std::cout << "[Map] Geometry: " << _mesh.vertices.size() << " verts, "
              << _mesh.indices.size()/3 << " tris, "
              << _wall_boxes.size() << " wall boxes, "
              << _spawns.size() << " spawns\n";
}

} // namespace HellVerdict
