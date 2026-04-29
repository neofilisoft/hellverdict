#pragma once
// HellVerdict — doom_renderer.hpp
// Copyright © 2026 Neofilisoft / Studio Balmung
// OpenGL fallback renderer — uses Balmung::Shader + GL directly

#include "../game/types.hpp"
#include "../game/map_data.hpp"
#include "../game/player.hpp"
#include "../game/enemy.hpp"

// Balmung engine headers
#include "renderer/shader.hpp"   // Balmung::Shader

#include <cstdint>
#include <vector>

namespace HellVerdict {

class DoomRenderer {
public:
    DoomRenderer() = default;
    ~DoomRenderer();

    bool init(int w, int h);
    void shutdown();
    void resize(int w, int h);

    // Upload world mesh (call once on map load)
    void upload_map(const MapMeshData& mesh);

    // Per-frame render
    void begin_frame();
    void render_world(const PlayerState& player);
    void render_enemies(const std::vector<Enemy>& enemies,
                        const PlayerState& player);
    void render_hud(const PlayerState& player, int score, GamePhase phase);
    void end_frame();

    uint32_t scene_texture() const { return _scene_tex; }

private:
    int _w = 0, _h = 0;

    // FBO for off-screen render
    uint32_t _fbo       = 0;
    uint32_t _scene_tex = 0;
    uint32_t _depth_rbo = 0;

    // World geometry
    uint32_t _map_vao   = 0;
    uint32_t _map_vbo   = 0;
    uint32_t _map_ebo   = 0;
    int      _map_index_count = 0;

    // Billboard (enemy sprites)
    uint32_t _bill_vao  = 0;
    uint32_t _bill_vbo  = 0;

    // HUD quad
    uint32_t _hud_vao   = 0;
    uint32_t _hud_vbo   = 0;

    // Present quad (FBO → screen)
    uint32_t _present_vao = 0;
    uint32_t _present_vbo = 0;

    Balmung::Shader _world_shader;
    Balmung::Shader _bill_shader;
    Balmung::Shader _hud_shader;
    Balmung::Shader _present_shader;

    // Creates/recreates FBO at given size
    void _create_fbo(int w, int h);

    // Build a Mat4 from HellVerdict types → float[16] for Balmung::Shader
    static Mat4 _view_matrix  (const PlayerState& p);
    static Mat4 _proj_matrix  (float aspect);
    static Mat4 _billboard_mat(const Vec3& pos, const Vec3& cam_pos, float scale,
                               const Mat4& view);
};

} // namespace HellVerdict
