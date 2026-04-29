// HellVerdict — world_map.cpp
// Copyright © 2026 Neofilisoft / Studio Balmung

#include "world_map.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>

namespace HellVerdict {

// ── Load ──────────────────────────────────────────────────────────────────────
bool WorldMap::load(const std::string& path, TextureCache& tex) {
    std::ifstream f(path);
    if (!f) { std::cerr << "[Map] Cannot open: " << path << "\n"; return false; }

    // Pre-fetch texture slots
    _tex_wall  = tex.get(TEX_WALL);
    _tex_wall2 = tex.get(TEX_WALL2);
    _tex_floor = tex.get(TEX_GROUND);
    _tex_ceil  = tex.get(TEX_CEIL);

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back()=='\r'||line.back()=='\n'))
            line.pop_back();
        lines.push_back(line);
    }

    _h = (int)lines.size();
    _w = 0;
    for (auto& l : lines) _w = std::max(_w, (int)l.size());

    _grid.assign(_w * _h, CellType::Void);

    for (int z = 0; z < _h; ++z) {
        const auto& row = lines[z];
        for (int x = 0; x < _w; ++x) {
            char c = x < (int)row.size() ? row[x] : ' ';
            CellType ct = CellType::Void;
            switch (c) {
            case '#': ct=CellType::Wall;      break;
            case 'W': ct=CellType::Wall2;     break;
            case '.': ct=CellType::Floor;     break;
            case 'P': ct=CellType::Player;    break;
            case 'Z': ct=CellType::Zombie;    break;
            case 'I': ct=CellType::Imp;       break;
            case 'D': ct=CellType::Demon;     break;
            case 'B': ct=CellType::Baron;     break;
            case 'C': ct=CellType::Cacodemon; break;
            case 'H': ct=CellType::Health;    break;
            case 'A': ct=CellType::Ammo;      break;
            case 'S': ct=CellType::Shotgun;   break;
            case 'X': ct=CellType::Exit;      break;
            }
            _grid[z*_w+x] = ct;
            glm::vec3 wpos = tile_center(x, z);

            if (ct == CellType::Player) {
                _player_start = wpos;
                _grid[z*_w+x] = CellType::Floor;
            } else if (ct == CellType::Exit) {
                _has_exit = true; _exit_pos = wpos;
                _grid[z*_w+x] = CellType::Floor;
            } else if (ct != CellType::Wall && ct != CellType::Wall2 &&
                       ct != CellType::Floor && ct != CellType::Void) {
                _spawns.push_back({ct, wpos});
                _grid[z*_w+x] = CellType::Floor;
            }
        }
    }

    // Build wall collision boxes
    for (int z=0;z<_h;++z) for (int x=0;x<_w;++x) {
        auto ct = cell(x,z);
        if (cell_solid(ct)) {
            _wall_boxes.push_back({
                {x*TILE, 0.f, z*TILE},
                {(x+1)*TILE, WALL_H, (z+1)*TILE}
            });
        }
    }

    // Build chunk meshes
    _cw = (_w + CHUNK-1) / CHUNK;
    _ch = (_h + CHUNK-1) / CHUNK;
    _chunks.resize(_cw * _ch);
    for (int cz=0;cz<_ch;++cz)
        for (int cx=0;cx<_cw;++cx)
            _build_chunk(cx, cz, _chunks[cz*_cw+cx]);

    std::cout << "[Map] " << path << " — " << _w << "x" << _h
              << " tiles, " << _cw << "x" << _ch << " chunks, "
              << _wall_boxes.size() << " walls, "
              << _spawns.size() << " spawns\n";
    return true;
}

CellType WorldMap::cell(int x, int z) const {
    if (x<0||x>=_w||z<0||z>=_h) return CellType::Void;
    return _grid[z*_w+x];
}

// ── ECS chunk entities ─────────────────────────────────────────────────────────
void WorldMap::build_ecs(ECSWorld& world) const {
    auto& reg = world.reg();
    for (int cz=0;cz<_ch;++cz) for (int cx=0;cx<_cw;++cx) {
        auto e = world.create();
        CChunk ch;
        ch.coord   = {cx, cz};
        ch.aabb_min= {cx*CHUNK*TILE, 0.f, cz*CHUNK*TILE};
        ch.aabb_max= {(cx+1)*CHUNK*TILE, WALL_H, (cz+1)*CHUNK*TILE};
        ch.dirty   = true;
        reg.emplace<CChunk>(e, ch);
    }
}

// ── Quad helper ───────────────────────────────────────────────────────────────
void WorldMap::_add_quad(std::vector<WorldVertex>& verts,
                          std::vector<uint32_t>& idx,
                          glm::vec3 p0, glm::vec3 p1,
                          glm::vec3 p2, glm::vec3 p3,
                          glm::vec3 normal, uint32_t tex, bool flip_uv)
{
    uint32_t base = (uint32_t)verts.size();
    glm::vec2 uvs[4] = flip_uv
        ? std::array<glm::vec2,4>{{{0,1},{1,1},{1,0},{0,0}}}
        : std::array<glm::vec2,4>{{{0,0},{1,0},{1,1},{0,1}}};
    glm::vec3 pts[4] = {p0,p1,p2,p3};
    for (int i=0;i<4;++i)
        verts.push_back({pts[i], normal, uvs[i], tex});
    idx.insert(idx.end(), {base,base+1,base+2, base,base+2,base+3});
}

// ── Chunk geometry builder ────────────────────────────────────────────────────
// Full LOD: 1:1 tiles  Half LOD: 2×2 merged  Quarter LOD: 4×4 merged
void WorldMap::_build_chunk(int cx, int cz, ChunkMesh& out) {
    const float T = TILE;
    const float WH = WALL_H;

    auto emit = [&](int x, int z,
                    std::vector<WorldVertex>& verts,
                    std::vector<uint32_t>& idx,
                    float tile_scale = 1.f) {
        CellType ct = cell(x, z);
        float wx = x * T; float wz = z * T;
        float ts = T * tile_scale;  // merged tile size for lower LODs

        if (cell_solid(ct)) {
            uint32_t wtex = (ct==CellType::Wall2) ? _tex_wall2 : _tex_wall;
            // Only exposed faces
            auto emit_face = [&](int nx, int nz,
                                  glm::vec3 p0, glm::vec3 p1,
                                  glm::vec3 p2, glm::vec3 p3,
                                  glm::vec3 norm) {
                if (!cell_solid(cell(x+nx, z+nz)))
                    _add_quad(verts, idx, p0,p1,p2,p3, norm, wtex);
            };
            emit_face( 0,-1,
                {wx,0,wz},{wx+ts,0,wz},{wx+ts,WH,wz},{wx,WH,wz},{0,0,-1});
            emit_face( 0, 1,
                {wx+ts,0,wz+ts},{wx,0,wz+ts},{wx,WH,wz+ts},{wx+ts,WH,wz+ts},{0,0,1});
            emit_face(-1, 0,
                {wx,0,wz+ts},{wx,0,wz},{wx,WH,wz},{wx,WH,wz+ts},{-1,0,0});
            emit_face( 1, 0,
                {wx+ts,0,wz},{wx+ts,0,wz+ts},{wx+ts,WH,wz+ts},{wx+ts,WH,wz},{1,0,0});
        } else if (ct != CellType::Void) {
            // Floor
            _add_quad(verts, idx,
                {wx,0,wz+ts},{wx+ts,0,wz+ts},{wx+ts,0,wz},{wx,0,wz},
                {0,1,0}, _tex_floor, true);
            // Ceiling
            _add_quad(verts, idx,
                {wx,WH,wz},{wx+ts,WH,wz},{wx+ts,WH,wz+ts},{wx,WH,wz+ts},
                {0,-1,0}, _tex_ceil, false);
        }
    };

    int x0=cx*CHUNK, z0=cz*CHUNK;
    int x1=std::min(x0+CHUNK,_w), z1=std::min(z0+CHUNK,_h);

    // Full LOD — every tile
    for (int z=z0;z<z1;++z)
        for (int x=x0;x<x1;++x)
            emit(x, z, out.verts_full, out.idx_full, 1.f);

    // Half LOD — every 2nd tile, merged
    for (int z=z0;z<z1;z+=2)
        for (int x=x0;x<x1;x+=2)
            emit(x, z, out.verts_half, out.idx_half, 2.f);

    // Quarter LOD — every 4th tile, merged
    for (int z=z0;z<z1;z+=4)
        for (int x=x0;x<x1;x+=4)
            emit(x, z, out.verts_quarter, out.idx_quarter, 4.f);
}

} // namespace HellVerdict
