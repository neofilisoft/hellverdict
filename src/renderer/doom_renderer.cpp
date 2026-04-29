// HellVerdict — doom_renderer.cpp
// Copyright © 2026 Neofilisoft / Studio Balmung
// OpenGL 4.1 renderer: world mesh, billboards, HUD, FBO present

#if __has_include(<glad/glad.h>)
#  include <glad/glad.h>
#  define HV_HAS_GL 1
#else
#  define HV_HAS_GL 0
#endif

#include "doom_renderer.hpp"
#include "../game/hell_game.hpp"
#include <cmath>
#include <iostream>
#include <vector>
#include <cstring>

namespace HellVerdict {

// ─────────────────────────────────────────────────────────────────────────────
// GLSL sources
// ─────────────────────────────────────────────────────────────────────────────

static const char* WORLD_VERT = R"(
#version 410 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNorm;
layout(location=2) in vec3 aColor;

uniform mat4 uView;
uniform mat4 uProj;

out vec3 vColor;
out vec3 vNorm;
out float vFog;

void main(){
    vec4 viewPos = uView * vec4(aPos, 1.0);
    gl_Position  = uProj * viewPos;
    vColor = aColor;
    vNorm  = aNorm;
    // Fog: starts at 12m, fully fogged at 26m  (Doom-feel)
    float dist = length(viewPos.xyz);
    vFog = clamp((dist - 12.0) / 14.0, 0.0, 1.0);
}
)";

static const char* WORLD_FRAG = R"(
#version 410 core
in vec3 vColor;
in vec3 vNorm;
in float vFog;

uniform vec3 uSunDir;
uniform vec3 uFogColor;
uniform float uPainFlash;   // 0..1 red vignette strength

out vec4 FragColor;

void main(){
    float ambient = 0.40;
    float diff    = max(dot(vNorm, normalize(uSunDir)), 0.0) * 0.60;
    vec3  lit     = vColor * (ambient + diff);
    vec3  final   = mix(lit, uFogColor, vFog);
    // Pain flash: blend toward red
    final = mix(final, vec3(0.6, 0.0, 0.0), uPainFlash * 0.35);
    FragColor = vec4(final, 1.0);
}
)";

// Billboards: world-space quad aligned to camera
static const char* BILL_VERT = R"(
#version 410 core
layout(location=0) in vec3 aPos;    // pre-computed world position
layout(location=1) in vec3 aColor;

uniform mat4 uView;
uniform mat4 uProj;

out vec3 vColor;
out vec2 vUV;

void main(){
    gl_Position = uProj * uView * vec4(aPos, 1.0);
    vColor = aColor;
    // UVs baked via vertex index trick
    vUV = vec2(0.0);
}
)";

static const char* BILL_FRAG = R"(
#version 410 core
in vec3 vColor;
in vec2 vUV;
out vec4 FragColor;

void main(){
    // Solid colored billboard (no texture in v1)
    // Slight silhouette darkening at edges via screen-space trick
    FragColor = vec4(vColor, 1.0);
}
)";

// HUD overlay (2D colored quads)
static const char* HUD_VERT = R"(
#version 410 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec3 aColor;
out vec3 vColor;
void main(){
    gl_Position = vec4(aPos, 0.0, 1.0);
    vColor = aColor;
}
)";

static const char* HUD_FRAG = R"(
#version 410 core
in vec3 vColor;
out vec4 FragColor;
uniform float uAlpha;
void main(){
    FragColor = vec4(vColor, uAlpha);
}
)";

// FBO blit
static const char* PRESENT_VERT = R"(
#version 410 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main(){ gl_Position = vec4(aPos, 0.0, 1.0); vUV = aUV; }
)";

static const char* PRESENT_FRAG = R"(
#version 410 core
in vec2 vUV;
uniform sampler2D uTex;
out vec4 FragColor;
void main(){ FragColor = texture(uTex, vUV); }
)";

// ─────────────────────────────────────────────────────────────────────────────

DoomRenderer::~DoomRenderer() { shutdown(); }

bool DoomRenderer::init(int w, int h) {
#if HV_HAS_GL
    _world_shader   = Balmung::Shader(WORLD_VERT,   WORLD_FRAG);
    _bill_shader    = Balmung::Shader(BILL_VERT,    BILL_FRAG);
    _hud_shader     = Balmung::Shader(HUD_VERT,     HUD_FRAG);
    _present_shader = Balmung::Shader(PRESENT_VERT, PRESENT_FRAG);

    if (!_world_shader.valid()   || !_present_shader.valid() ||
        !_bill_shader.valid()    || !_hud_shader.valid()) {
        std::cerr << "[DoomRenderer] Shader compile failed\n";
        return false;
    }

    // Billboard VAO (6 verts per billboard, re-uploaded each frame as dynamic)
    glGenVertexArrays(1, &_bill_vao);
    glGenBuffers(1, &_bill_vbo);
    glBindVertexArray(_bill_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _bill_vbo);
    // Allocate for up to 256 billboards × 6 verts × (3+3) floats
    glBufferData(GL_ARRAY_BUFFER, 256 * 6 * 6 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
    glBindVertexArray(0);

    // HUD VAO (dynamic)
    glGenVertexArrays(1, &_hud_vao);
    glGenBuffers(1, &_hud_vbo);
    glBindVertexArray(_hud_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _hud_vbo);
    glBufferData(GL_ARRAY_BUFFER, 512 * 5 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5*sizeof(float), (void*)(2*sizeof(float)));
    glBindVertexArray(0);

    // Present quad
    const float quad[] = {
        -1,-1, 0,0,  1,-1, 1,0,  1,1, 1,1,
        -1,-1, 0,0,  1,1,  1,1, -1,1, 0,1
    };
    glGenVertexArrays(1, &_present_vao);
    glGenBuffers(1, &_present_vbo);
    glBindVertexArray(_present_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _present_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    _create_fbo(w, h);
    _w = w; _h = h;
    std::cout << "[DoomRenderer] Init " << w << "x" << h << "\n";
    return true;
#else
    std::cout << "[DoomRenderer] GL not available — headless\n";
    return true;
#endif
}

void DoomRenderer::_create_fbo(int w, int h) {
#if HV_HAS_GL
    if (_fbo) {
        glDeleteFramebuffers(1, &_fbo);
        glDeleteTextures(1, &_scene_tex);
        glDeleteRenderbuffers(1, &_depth_rbo);
    }
    glGenFramebuffers(1, &_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo);

    glGenTextures(1, &_scene_tex);
    glBindTexture(GL_TEXTURE_2D, _scene_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _scene_tex, 0);

    glGenRenderbuffers(1, &_depth_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, _depth_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, _depth_rbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "[DoomRenderer] FBO incomplete\n";

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
#endif
}

void DoomRenderer::resize(int w, int h) {
#if HV_HAS_GL
    if (w == _w && h == _h) return;
    _w = w; _h = h;
    glViewport(0, 0, w, h);
    _create_fbo(w, h);
#endif
}

void DoomRenderer::shutdown() {
#if HV_HAS_GL
    if (_map_vao)     { glDeleteVertexArrays(1, &_map_vao); glDeleteBuffers(1,&_map_vbo); glDeleteBuffers(1,&_map_ebo); _map_vao=0; }
    if (_bill_vao)    { glDeleteVertexArrays(1, &_bill_vao); glDeleteBuffers(1,&_bill_vbo); _bill_vao=0; }
    if (_hud_vao)     { glDeleteVertexArrays(1, &_hud_vao);  glDeleteBuffers(1,&_hud_vbo);  _hud_vao=0;  }
    if (_present_vao) { glDeleteVertexArrays(1, &_present_vao); glDeleteBuffers(1,&_present_vbo); _present_vao=0; }
    if (_fbo)         { glDeleteFramebuffers(1,&_fbo); glDeleteTextures(1,&_scene_tex); glDeleteRenderbuffers(1,&_depth_rbo); _fbo=0; }
#endif
}

// ── Upload world mesh ─────────────────────────────────────────────────────────
void DoomRenderer::upload_map(const MapMeshData& mesh) {
#if HV_HAS_GL
    if (_map_vao) {
        glDeleteVertexArrays(1, &_map_vao);
        glDeleteBuffers(1, &_map_vbo);
        glDeleteBuffers(1, &_map_ebo);
    }
    _map_index_count = (int)mesh.indices.size();

    glGenVertexArrays(1, &_map_vao);
    glGenBuffers(1, &_map_vbo);
    glGenBuffers(1, &_map_ebo);

    glBindVertexArray(_map_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _map_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 mesh.vertices.size() * sizeof(MapVertex),
                 mesh.vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _map_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 mesh.indices.size() * sizeof(uint32_t),
                 mesh.indices.data(), GL_STATIC_DRAW);

    constexpr int S = sizeof(MapVertex);
    // pos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, S, (void*)offsetof(MapVertex, px));
    glEnableVertexAttribArray(0);
    // normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, S, (void*)offsetof(MapVertex, nx));
    glEnableVertexAttribArray(1);
    // color
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, S, (void*)offsetof(MapVertex, cr));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    std::cout << "[DoomRenderer] Map uploaded: "
              << mesh.vertices.size() << " verts, "
              << _map_index_count/3 << " tris\n";
#endif
}

// ── Math helpers ──────────────────────────────────────────────────────────────
Mat4 DoomRenderer::_view_matrix(const PlayerState& p) {
    Vec3 eye    = p.eye_pos();
    Vec3 center = eye + p.forward();
    Vec3 up     = {0,1,0};
    return mat4_look_at(eye, center, up);
}

Mat4 DoomRenderer::_proj_matrix(float aspect) {
    return mat4_perspective(70.f * DEG2RAD, aspect, 0.05f, 200.f);
}

// Helper: push a colored quad (2 triangles, 6 verts) to a float buffer
// Each vertex: x y z  cr cg cb
static void push_billboard(std::vector<float>& buf,
                            Vec3 center, Vec3 right_axis, Vec3 up_axis,
                            float half_w, float half_h, Color3 col)
{
    Vec3 corners[4] = {
        center + right_axis * (-half_w) + up_axis * 0.f,
        center + right_axis * ( half_w) + up_axis * 0.f,
        center + right_axis * ( half_w) + up_axis * (half_h * 2.f),
        center + right_axis * (-half_w) + up_axis * (half_h * 2.f),
    };
    int order[6] = {0,1,2, 0,2,3};
    for (int i : order) {
        buf.push_back(corners[i].x); buf.push_back(corners[i].y); buf.push_back(corners[i].z);
        buf.push_back(col.r);        buf.push_back(col.g);        buf.push_back(col.b);
    }
}

// ── Render ────────────────────────────────────────────────────────────────────
void DoomRenderer::begin_frame() {
#if HV_HAS_GL
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo ? _fbo : 0);
    glViewport(0, 0, _w, _h);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    // Doom-style dark background
    glClearColor(0.02f, 0.01f, 0.01f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#endif
}

void DoomRenderer::render_world(const PlayerState& player) {
#if HV_HAS_GL
    if (!_map_vao || !_map_index_count) return;

    float aspect = _h > 0 ? (float)_w / (float)_h : 1.f;
    Mat4 view = _view_matrix(player);
    Mat4 proj = _proj_matrix(aspect);

    // Convert HellVerdict::Mat4 → Balmung::Mat4 (same layout, just wrap)
    Balmung::Mat4 bv, bp;
    std::memcpy(bv.m, view.m, 64);
    std::memcpy(bp.m, proj.m, 64);

    _world_shader.bind();
    _world_shader.set_mat4("uView", bv);
    _world_shader.set_mat4("uProj", bp);
    _world_shader.set_vec3("uSunDir", {0.4f, 0.8f, 0.3f});
    _world_shader.set_vec3("uFogColor", {0.02f, 0.01f, 0.01f});
    _world_shader.set_float("uPainFlash", player.pain_flash);

    glBindVertexArray(_map_vao);
    glDrawElements(GL_TRIANGLES, _map_index_count, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    _world_shader.unbind();
#endif
}

void DoomRenderer::render_enemies(const std::vector<Enemy>& enemies,
                                   const PlayerState& player)
{
#if HV_HAS_GL
    if (enemies.empty() || !_bill_vao) return;

    float aspect = _h > 0 ? (float)_w / (float)_h : 1.f;
    Mat4 view = _view_matrix(player);
    Mat4 proj = _proj_matrix(aspect);

    Balmung::Mat4 bv, bp;
    std::memcpy(bv.m, view.m, 64);
    std::memcpy(bp.m, proj.m, 64);

    // Camera right axis (world-space) — for billboard facing
    Vec3 cam_right = {view.m[0], view.m[4], view.m[8]};
    Vec3 cam_up    = {0.f, 1.f, 0.f};   // keep upright like Doom sprites

    std::vector<float> bill_buf;
    bill_buf.reserve(enemies.size() * 6 * 6);

    for (const auto& e : enemies) {
        if (!e.alive) {
            // Dead — flat on ground, dark
            Color3 dead_col = {0.2f, 0.05f, 0.05f};
            Vec3 flat_center = {e.pos.x, e.pos.y + 0.05f, e.pos.z};
            // Horizontal quad on floor
            Vec3 right_axis = {1,0,0};
            Vec3 fwd_axis   = {0,0,1};
            float r = get_enemy_def(e.type).radius * 0.7f;
            Vec3 c[4] = {
                flat_center + right_axis*(-r) + fwd_axis*(-r),
                flat_center + right_axis*( r) + fwd_axis*(-r),
                flat_center + right_axis*( r) + fwd_axis*( r),
                flat_center + right_axis*(-r) + fwd_axis*( r),
            };
            int ord[6] = {0,1,2,0,2,3};
            for (int i : ord) {
                bill_buf.push_back(c[i].x); bill_buf.push_back(c[i].y); bill_buf.push_back(c[i].z);
                bill_buf.push_back(dead_col.r); bill_buf.push_back(dead_col.g); bill_buf.push_back(dead_col.b);
            }
            continue;
        }
        float r = get_enemy_def(e.type).radius;
        push_billboard(bill_buf,
                       {e.pos.x, e.pos.y, e.pos.z},
                       cam_right, cam_up,
                       r, r,
                       e.get_color());
    }

    if (bill_buf.empty()) return;

    glBindBuffer(GL_ARRAY_BUFFER, _bill_vbo);
    // Grow buffer if needed
    glBufferData(GL_ARRAY_BUFFER,
                 bill_buf.size() * sizeof(float),
                 bill_buf.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Billboards need alpha-test style render — disable cull face
    glDisable(GL_CULL_FACE);

    _bill_shader.bind();
    _bill_shader.set_mat4("uView", bv);
    _bill_shader.set_mat4("uProj", bp);

    glBindVertexArray(_bill_vao);
    glDrawArrays(GL_TRIANGLES, 0, (int)(bill_buf.size() / 6));
    glBindVertexArray(0);
    _bill_shader.unbind();

    glEnable(GL_CULL_FACE);
#endif
}

// ── HUD ───────────────────────────────────────────────────────────────────────
// Pushes a colored NDC rect into a float buffer (2 tris, 5 floats/vert)
static void push_hud_rect(std::vector<float>& buf,
                           float x0, float y0, float x1, float y1,
                           Color3 col)
{
    auto v = [&](float x, float y) {
        buf.push_back(x); buf.push_back(y);
        buf.push_back(col.r); buf.push_back(col.g); buf.push_back(col.b);
    };
    v(x0,y0); v(x1,y0); v(x1,y1);
    v(x0,y0); v(x1,y1); v(x0,y1);
}

void DoomRenderer::render_hud(const PlayerState& player, int score, GamePhase phase) {
#if HV_HAS_GL
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    std::vector<float> hud;
    hud.reserve(256);

    // ── Health bar ────────────────────────────────────────────────────────────
    // Background (dark red)
    push_hud_rect(hud, -0.95f, -0.97f, -0.05f, -0.90f, {0.25f,0.0f,0.0f});
    // Foreground (bright red) scaled to health %
    float hp_frac = clampf(player.health / 100.f, 0.f, 1.f);
    float hp_x1   = -0.95f + 0.90f * hp_frac;
    push_hud_rect(hud, -0.95f, -0.97f, hp_x1,   -0.90f, COL_RED);

    // ── Ammo bar (shotgun shells) ──────────────────────────────────────────────
    if (player.weapon.has_shotgun) {
        push_hud_rect(hud, 0.05f, -0.97f, 0.95f, -0.90f, {0.1f,0.1f,0.0f});
        float ammo_frac = clampf(player.weapon.ammo_shells / 24.f, 0.f, 1.f);
        float ax1       = 0.05f + 0.90f * ammo_frac;
        push_hud_rect(hud, 0.05f, -0.97f, ax1,   -0.90f, COL_YELLOW);
    }

    // ── Weapon slot indicator ─────────────────────────────────────────────────
    // Three small squares near bottom center
    for (int i = 0; i < 3; ++i) {
        float bx = -0.06f + (float)i * 0.07f;
        bool active = (int)player.weapon.slot == i;
        bool avail  = (i < 2) || player.weapon.has_shotgun;
        Color3 c = active ? COL_YELLOW : (avail ? COL_GRAY : COL_DARKGRAY);
        push_hud_rect(hud, bx, -0.88f, bx+0.05f, -0.84f, c);
    }

    // ── Pain flash vignette ───────────────────────────────────────────────────
    if (player.pain_flash > 0.f) {
        // Four red edge quads
        float alpha = player.pain_flash * 0.55f;
        Color3 rc = {0.6f,0.0f,0.0f};
        push_hud_rect(hud, -1.f, -1.f, -0.75f, 1.f, rc);
        push_hud_rect(hud,  0.75f,-1.f,  1.f,  1.f, rc);
        push_hud_rect(hud, -1.f,  0.75f, 1.f,  1.f, rc);
        push_hud_rect(hud, -1.f, -1.f,   1.f, -0.75f, rc);
        (void)alpha;  // used as uAlpha uniform
    }

    // ── Crosshair ─────────────────────────────────────────────────────────────
    push_hud_rect(hud, -0.005f, -0.025f, 0.005f, 0.025f, COL_WHITE);
    push_hud_rect(hud, -0.025f, -0.005f, 0.025f, 0.005f, COL_WHITE);

    // ── Game-over / victory overlay ───────────────────────────────────────────
    if (phase == GamePhase::Dead) {
        push_hud_rect(hud, -1.f, -1.f, 1.f, 1.f, {0.3f,0.0f,0.0f});
    } else if (phase == GamePhase::Victory) {
        push_hud_rect(hud, -1.f, -1.f, 1.f, 1.f, {0.0f,0.15f,0.0f});
    }

    if (hud.empty()) goto done;

    glBindBuffer(GL_ARRAY_BUFFER, _hud_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 hud.size() * sizeof(float),
                 hud.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    _hud_shader.bind();
    _hud_shader.set_float("uAlpha",
        player.pain_flash > 0.f ? player.pain_flash * 0.55f : 1.0f);

    glBindVertexArray(_hud_vao);
    glDrawArrays(GL_TRIANGLES, 0, (int)(hud.size() / 5));
    glBindVertexArray(0);
    _hud_shader.unbind();

done:
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
#endif
}

void DoomRenderer::end_frame() {
#if HV_HAS_GL
    // Blit FBO to default framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, _w, _h);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT);

    _present_shader.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _scene_tex);
    _present_shader.set_int("uTex", 0);

    glBindVertexArray(_present_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_2D, 0);
    _present_shader.unbind();
    glEnable(GL_DEPTH_TEST);
#endif
}

} // namespace HellVerdict
