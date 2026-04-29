// HellVerdict — main.cpp  (v3 — final)
// Copyright © 2026 Neofilisoft / Studio Balmung
// SDL2 | Vulkan 1.3 primary (OpenGL 4.1 fallback)
// EnTT ECS | GLM math | Chunk LOD | SDF fonts | Shader warmup | 5 levels

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "ecs/ecs_world.hpp"
#include "ecs/components.hpp"
#include "world/world_map.hpp"
#include "render/texture_cache.hpp"
#include "render/vk_pipeline.hpp"
#include "render/sync/vk_sync.hpp"
#include "render/font/sdf_font.hpp"
#include "render/warmup/shader_warmup.hpp"
#include "game/enemy_defs.hpp"
#include "scripting/cs_bridge.hpp"

#include <iostream>
#include <memory>
#include <chrono>
#include <array>
#include <format>
#include <algorithm>
#include <cmath>

using namespace HellVerdict;
using namespace HellVerdict::Sync;
using Clock = std::chrono::steady_clock;

// ── Config ─────────────────────────────────────────────────────────────────────
static constexpr int   WIN_W       = 1280;
static constexpr int   WIN_H       = 720;
static constexpr float FIXED_STEP  = 1.f / 120.f;
static constexpr float MOVE_SPEED  = 5.5f;
static constexpr float ACCEL       = 60.f;
static constexpr float FRICTION    = 12.f;
static constexpr float MOUSE_SENS  = 0.12f;
static constexpr int   LEVEL_COUNT = 5;

static const std::array<const char*, LEVEL_COUNT> LEVEL_FILES = {
    "assets/maps/e1m1.txt", "assets/maps/e1m2.txt",
    "assets/maps/e1m3.txt", "assets/maps/e1m4.txt",
    "assets/maps/e1m5.txt",
};

// LOD distances (metres from camera)
static constexpr float LOD_FULL    = 14.f;
static constexpr float LOD_HALF    = 26.f;
static constexpr float LOD_QUARTER = 38.f;

// ── Input ──────────────────────────────────────────────────────────────────────
struct InputState {
    bool fwd=false, back=false, left=false, right=false, fire=false;
    float mouse_dx=0.f, mouse_dy=0.f;
    int scroll=0;
};

// ── Phase ──────────────────────────────────────────────────────────────────────
enum class Phase { Playing, Dead, Victory };

// ── Entity factory helpers ─────────────────────────────────────────────────────
static Entity spawn_enemy(ECSWorld& w, EnemyType t, glm::vec3 pos,
                           TextureCache& tex)
{
    auto e = w.create();
    const auto& d = get_def(t);
    CTransform tf{}; tf.position = pos;
    CVelocity  vel{};
    CAABB      aabb{}; aabb.half_extents = {d.radius*.9f, d.radius, d.radius*.9f};
    CEnemy     en{}; apply_def(en, t);
    CPrevTransform pv{}; pv.position = pos;
    CBillboard bill{d.color, d.radius, d.radius*2.f, tex.get(d.tex_name)};
    w.reg().emplace<CTransform>(e,tf); w.reg().emplace<CVelocity>(e,vel);
    w.reg().emplace<CAABB>(e,aabb);    w.reg().emplace<CEnemy>(e,en);
    w.reg().emplace<CPrevTransform>(e,pv); w.reg().emplace<CBillboard>(e,bill);
    return e;
}

static void spawn_pickup(ECSWorld& w, PickupKind kind, float amt,
                          glm::vec3 pos, TextureCache& tex,
                          const char* tex_name, glm::vec3 col)
{
    auto e = w.create();
    CPickup pk{}; pk.kind=kind; pk.amount=amt;
    CTransform tf{}; tf.position=pos;
    CBillboard bill{col, 0.4f, 0.4f, tex.get(tex_name)};
    w.reg().emplace<CPickup>(e,pk); w.reg().emplace<CTransform>(e,tf);
    w.reg().emplace<CBillboard>(e,bill);
}

static WorldMap do_load_level(ECSWorld& w, TextureCache& tex, int idx) {
    w.clear();
    WorldMap map;
    map.load(LEVEL_FILES[idx], tex);
    map.build_ecs(w);

    // Player
    auto pe = w.create();
    w.reg().emplace<CTransform>(pe, CTransform{map.player_start()});
    w.reg().emplace<CVelocity>(pe);
    w.reg().emplace<CAABB>(pe);
    w.reg().emplace<CCamera>(pe);
    w.reg().emplace<CPlayer>(pe);
    w.reg().emplace<CPrevTransform>(pe, CPrevTransform{map.player_start()});
    w.set_player(pe);

    for (auto& sp : map.spawns()) {
        switch(sp.type) {
        case CellType::Zombie:    spawn_enemy(w, EnemyType::Zombie,    sp.world_pos, tex); break;
        case CellType::Imp:       spawn_enemy(w, EnemyType::Imp,       sp.world_pos, tex); break;
        case CellType::Demon:     spawn_enemy(w, EnemyType::Demon,     sp.world_pos, tex); break;
        case CellType::Baron:     spawn_enemy(w, EnemyType::Baron,     sp.world_pos, tex); break;
        case CellType::Cacodemon: spawn_enemy(w, EnemyType::Cacodemon, sp.world_pos, tex); break;
        case CellType::Health:
            spawn_pickup(w, PickupKind::Health, 25.f, sp.world_pos, tex,
                         TEX_PICKUP_HEALTH, {0.2f,0.9f,0.2f}); break;
        case CellType::Ammo:
            spawn_pickup(w, PickupKind::Ammo, 12.f, sp.world_pos, tex,
                         TEX_PICKUP_AMMO, {0.9f,0.9f,0.1f}); break;
        case CellType::Shotgun:
            spawn_pickup(w, PickupKind::Shotgun, 8.f, sp.world_pos, tex,
                         TEX_PICKUP_SHOTGUN, {0.6f,0.4f,0.1f}); break;
        case CellType::Exit: {
            auto xe = w.create();
            w.reg().emplace<CExitTrigger>(xe);
            w.reg().emplace<CTransform>(xe, CTransform{sp.world_pos});
            break; }
        default: break;
        }
    }
    return map;
}

// ── Main ───────────────────────────────────────────────────────────────────────
int main(int /*argc*/, char* /*argv*/[]) {
    std::cout << "=== HELL VERDICT v3 ===\n"
              << "© 2026 Neofilisoft / Studio Balmung\n"
              << "Engine: Balmung 2.3 | Vulkan 1.3 + SDL2 + EnTT + GLM\n\n";

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        std::cerr << "SDL_Init: " << SDL_GetError() << "\n"; return 1;
    }

    // Window — try Vulkan, fall back to OpenGL
    SDL_Window* window = SDL_CreateWindow("Hell Verdict",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

    bool use_vk = (window != nullptr);
    SDL_GLContext gl_ctx = nullptr;

    if (!use_vk) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        window = SDL_CreateWindow("Hell Verdict (GL)",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            WIN_W, WIN_H,
            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
        if (!window) { std::cerr << SDL_GetError(); SDL_Quit(); return 1; }
        gl_ctx = SDL_GL_CreateContext(window);
        SDL_GL_SetSwapInterval(1);
        std::cout << "[main] OpenGL fallback\n";
    } else {
        std::cout << "[main] Vulkan window OK\n";
    }

    SDL_SetRelativeMouseMode(SDL_TRUE);

    // ── Subsystem init ─────────────────────────────────────────────────────────
    TextureCache  tex_cache;
    tex_cache.init_gl("assets/textures");   // swap for init_vk when VkDevice ready

    SdfFontRenderer font;
    font.init("assets/fonts/hell_verdict.png",
              "assets/fonts/hell_verdict.json",
              WIN_W, WIN_H);

    CsBridge cs_bridge;
    cs_bridge.init("/usr/lib/mono/net_4_x",
                   "scripts/HellVerdict.Scripts.dll");

    // ── Game state ─────────────────────────────────────────────────────────────
    ECSWorld world;
    int      cur_level  = 0;
    Phase    phase      = Phase::Playing;
    float    dead_timer = 0.f;
    float    accum      = 0.f;
    float    alpha      = 0.f;

    // NOTE: In the Vulkan path, ShaderWarmup::execute() is called HERE,
    // before the first frame, during the loading screen.
    // ShaderWarmup warmup;
    // warmup.init(device, phys, queue, queue_family, pipe_cache, "pipeline.cache");
    // warmup.execute(0, pipeline_manager, [](int p){ update_loading_bar(p); });
    std::cout << "[main] Shader warmup would run here (Vulkan path)\n";

    WorldMap cur_map = do_load_level(world, tex_cache, cur_level);

    // ── Input / timing ─────────────────────────────────────────────────────────
    InputState input{};
    bool running = true;
    auto last_t  = Clock::now();
    float fps_accum = 0.f;
    int   fps_frames = 0;
    float fps_display = 0.f;

    // ─────────────────────────────────────────────────────────────────────────
    // MAIN LOOP
    // ─────────────────────────────────────────────────────────────────────────
    while (running) {
        auto now = Clock::now();
        float dt = std::chrono::duration<float>(now - last_t).count();
        last_t   = now;
        dt = std::min(dt, 0.05f);

        fps_accum += dt; fps_frames++;
        if (fps_accum >= 0.5f) {
            fps_display = fps_frames / fps_accum;
            fps_accum = 0.f; fps_frames = 0;
        }

        // ── Events ────────────────────────────────────────────────────────────
        input.mouse_dx = input.mouse_dy = 0.f;
        input.scroll = 0;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT: running = false; break;
            case SDL_KEYDOWN: {
                auto k = ev.key.keysym.sym;
                if (k==SDLK_ESCAPE) running=false;
                if (k==SDLK_w||k==SDLK_UP)    input.fwd  =true;
                if (k==SDLK_s||k==SDLK_DOWN)  input.back =true;
                if (k==SDLK_a||k==SDLK_LEFT)  input.left =true;
                if (k==SDLK_d||k==SDLK_RIGHT) input.right=true;
                break; }
            case SDL_KEYUP: {
                auto k = ev.key.keysym.sym;
                if (k==SDLK_w||k==SDLK_UP)    input.fwd  =false;
                if (k==SDLK_s||k==SDLK_DOWN)  input.back =false;
                if (k==SDLK_a||k==SDLK_LEFT)  input.left =false;
                if (k==SDLK_d||k==SDLK_RIGHT) input.right=false;
                break; }
            case SDL_MOUSEMOTION:
                input.mouse_dx += ev.motion.xrel;
                input.mouse_dy += ev.motion.yrel;
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button==SDL_BUTTON_LEFT)  input.fire=true; break;
            case SDL_MOUSEBUTTONUP:
                if (ev.button.button==SDL_BUTTON_LEFT)  input.fire=false; break;
            case SDL_MOUSEWHEEL:
                input.scroll = ev.wheel.y > 0 ? 1 : -1; break;
            case SDL_WINDOWEVENT:
                if (ev.window.event==SDL_WINDOWEVENT_RESIZED) {
                    int w=ev.window.data1, h=ev.window.data2;
                    font.set_viewport(w, h);
                }
                break;
            }
        }

        // ── Phase transitions ─────────────────────────────────────────────────
        if (phase == Phase::Dead || phase == Phase::Victory) {
            dead_timer -= dt;
            if (dead_timer <= 0.f) {
                if (phase == Phase::Victory && cur_level < LEVEL_COUNT-1) {
                    cur_level++;
                    // Warmup next level's shaders while loading
                    // warmup.execute(cur_level, pipeline_manager, nullptr);
                } else {
                    cur_level = 0;
                }
                cur_map = do_load_level(world, tex_cache, cur_level);
                phase = Phase::Playing;
                accum = 0.f;
            }
        }

        // ── Fixed-step physics + AI ───────────────────────────────────────────
        if (phase == Phase::Playing) {
            accum += dt;
            while (accum >= FIXED_STEP && phase == Phase::Playing) {
                accum -= FIXED_STEP;
                Systems::snapshot_transforms(world);

                auto ent = world.player_entity();
                if (!world.reg().valid(ent)) break;
                auto& cam = world.reg().get<CCamera>(ent);
                auto& pl  = world.reg().get<CPlayer>(ent);
                auto& vel = world.reg().get<CVelocity>(ent);
                auto& tf  = world.reg().get<CTransform>(ent);

                // Look
                cam.yaw   += input.mouse_dx * MOUSE_SENS;
                cam.pitch  = glm::clamp(cam.pitch - input.mouse_dy * MOUSE_SENS, -89.f, 89.f);

                // Weapon switch
                if (input.scroll > 0) {
                    int s = ((int)pl.active_weapon + 1) % (int)CPlayer::Weapon::COUNT;
                    if ((CPlayer::Weapon)s == CPlayer::Weapon::Shotgun && !pl.has_shotgun) s=0;
                    pl.active_weapon = (CPlayer::Weapon)s;
                } else if (input.scroll < 0) {
                    int s = ((int)pl.active_weapon - 1 + (int)CPlayer::Weapon::COUNT)
                            % (int)CPlayer::Weapon::COUNT;
                    if ((CPlayer::Weapon)s == CPlayer::Weapon::Shotgun && !pl.has_shotgun) s--;
                    if (s < 0) s = (int)CPlayer::Weapon::COUNT-1;
                    pl.active_weapon = (CPlayer::Weapon)s;
                }

                // Move
                glm::vec3 fwd = cam.forward_xz(), rgt = cam.right();
                glm::vec3 wish{};
                if (input.fwd)  wish += fwd;
                if (input.back) wish -= fwd;
                if (input.right)wish += rgt;
                if (input.left) wish -= rgt;
                if (glm::length2(wish) > 1e-6f) wish = glm::normalize(wish);

                float cur_s = glm::dot(vel.linear, wish);
                float add   = std::min(ACCEL*FIXED_STEP*MOVE_SPEED, MOVE_SPEED-cur_s);
                if (add > 0.f) vel.linear += wish * add;
                if (glm::length2(wish) < 1e-6f) {
                    float spd = glm::length(vel.linear);
                    if (spd > 1e-4f) {
                        float drop = std::min(spd*FRICTION*FIXED_STEP, spd);
                        vel.linear *= (spd-drop)/spd;
                    }
                }
                vel.linear.y = 0.f;
                Systems::integrate_movement(world, FIXED_STEP, cur_map.wall_boxes());

                // AI
                Systems::update_ai(world, FIXED_STEP, ent, cur_map.wall_boxes());

                // Pickups
                Systems::collect_pickups(world, ent,
                    [&](PickupKind kind, float amt) {
                        if (kind==PickupKind::Health)
                            pl.health = std::min(pl.health+amt, pl.max_health);
                        else if (kind==PickupKind::Ammo)
                            pl.ammo_shells += (int)amt;
                        else if (kind==PickupKind::Shotgun) {
                            pl.has_shotgun=true; pl.ammo_shells+=8; }
                    });

                // Weapon fire
                pl.weapon_cooldown = std::max(0.f, pl.weapon_cooldown - FIXED_STEP);
                pl.pain_flash      = std::max(0.f, pl.pain_flash - FIXED_STEP*2.5f);

                if (input.fire && pl.weapon_cooldown <= 0.f && pl.alive) {
                    float damage=0.f, range=0.f;
                    switch(pl.active_weapon) {
                    case CPlayer::Weapon::Fist:    damage=20.f;range=1.5f;pl.weapon_cooldown=0.5f; break;
                    case CPlayer::Weapon::Pistol:  damage=12.f;range=50.f;pl.weapon_cooldown=0.35f;break;
                    case CPlayer::Weapon::Shotgun:
                        if(pl.ammo_shells>0){damage=49.f;range=14.f;pl.weapon_cooldown=0.85f;pl.ammo_shells--;}
                        break;
                    default:break;
                    }
                    if (damage > 0.f) {
                        glm::vec3 eye = tf.position + glm::vec3{0,cam.eye_height,0};
                        glm::vec3 dir = cam.forward();
                        glm::vec3 inv{
                            std::abs(dir.x)>1e-6f?1.f/dir.x:1e9f,
                            std::abs(dir.y)>1e-6f?1.f/dir.y:1e9f,
                            std::abs(dir.z)>1e-6f?1.f/dir.z:1e9f};
                        float best_t = range;
                        entt::entity hit = entt::null;
                        world.reg().view<CEnemy,CTransform,CAABB>().each(
                            [&](entt::entity ee,const CEnemy& en,
                                const CTransform& et,const CAABB& ea){
                            if (!en.alive) return;
                            glm::vec3 c=et.position+glm::vec3{0,ea.half_extents.y,0};
                            glm::vec3 mn=c-ea.half_extents, mx=c+ea.half_extents;
                            float tx1=(mn.x-eye.x)*inv.x,tx2=(mx.x-eye.x)*inv.x;
                            float ty1=(mn.y-eye.y)*inv.y,ty2=(mx.y-eye.y)*inv.y;
                            float tz1=(mn.z-eye.z)*inv.z,tz2=(mx.z-eye.z)*inv.z;
                            float tmin=std::max({std::min(tx1,tx2),std::min(ty1,ty2),std::min(tz1,tz2)});
                            float tmax=std::min({std::max(tx1,tx2),std::max(ty1,ty2),std::max(tz1,tz2)});
                            if(tmax>=0.f&&tmin<=tmax&&tmin<best_t){best_t=tmin;hit=ee;}
                        });
                        if (hit!=entt::null) {
                            auto& he=world.reg().get<CEnemy>(hit);
                            he.health-=damage; he.pain_timer=0.25f;
                            if(he.health<=0.f){he.health=0.f;he.alive=false;
                                he.ai_state=AIState::Dead;pl.kills++;
                                pl.score+=(int)(damage*12.f);}
                        }
                    }
                }

                if (!pl.alive && phase==Phase::Playing) {
                    phase=Phase::Dead; dead_timer=3.5f;
                    cs_bridge.dispatch_event({"hell_verdict:player_dead",
                        std::format("{{\"score\":{},\"level\":{}}}", pl.score, cur_level+1)});
                }
                if (phase==Phase::Playing && Systems::check_exit(world,ent)) {
                    phase=Phase::Victory; dead_timer=3.0f;
                    cs_bridge.dispatch_event({"hell_verdict:victory",
                        std::format("{{\"score\":{},\"level\":{}}}", pl.score, cur_level+1)});
                }
            }
            alpha = accum / FIXED_STEP;
        }

        // ── C# update ─────────────────────────────────────────────────────────
        cs_bridge.update(dt);

        // ── LOD update ─────────────────────────────────────────────────────────
        auto ent = world.player_entity();
        if (world.reg().valid(ent)) {
            auto& cam = world.reg().get<CCamera>(ent);
            auto& tf  = world.reg().get<CTransform>(ent);
            glm::vec3 eye = tf.position + glm::vec3{0,cam.eye_height,0};
            glm::mat4 view = glm::lookAt(eye, eye + cam.forward(), {0,1,0});
            int vw,vh; SDL_GetWindowSize(window,&vw,&vh);
            float aspect = vh > 0 ? (float)vw/vh : 1.f;
            glm::mat4 proj = glm::perspective(glm::radians(cam.fov), aspect,
                                              cam.near_z, cam.far_z);
            Systems::update_lod(world, proj*view,
                                LOD_FULL, LOD_HALF, LOD_QUARTER);
        }

        // ── HUD via SDF font ───────────────────────────────────────────────────
        if (world.reg().valid(ent)) {
            auto& pl = world.reg().get<CPlayer>(ent);

            // FPS counter (top-right, small)
            font.draw_text(
                std::format("{:.0f} FPS | L{}", fps_display, cur_level+1),
                {0.6f, 0.95f}, 18.f, {0.8f,0.8f,0.8f,0.9f});

            // Health (bottom-left)
            font.draw_text(
                std::format("HP {:>3.0f}", pl.health),
                {-0.95f, -0.85f}, 26.f,
                pl.health < 25.f ? glm::vec4{1,0.1f,0.1f,1}
                                  : glm::vec4{1,0.9f,0.1f,1});

            // Ammo (bottom-right)
            if (pl.has_shotgun)
                font.draw_text(
                    std::format("SG {:>2}", pl.ammo_shells),
                    {0.65f, -0.85f}, 26.f, {0.9f,0.8f,0.2f,1.f});

            // Score
            font.draw_text(
                std::format("SCORE {}", pl.score),
                {-0.35f, 0.95f}, 22.f, {1,1,1,1});

            // Phase overlays (large centered text)
            if (phase == Phase::Dead)
                font.draw_text("YOU DIED",
                    {-0.25f, 0.1f}, 64.f, {0.8f,0.05f,0.05f,1.f});
            else if (phase == Phase::Victory && cur_level == LEVEL_COUNT-1)
                font.draw_text("HELL VERDICT!",
                    {-0.45f, 0.1f}, 56.f, {1.f,0.8f,0.1f,1.f});
            else if (phase == Phase::Victory)
                font.draw_text(
                    std::format("LEVEL {} CLEAR!", cur_level+1),
                    {-0.4f, 0.1f}, 48.f, {0.2f,1.f,0.2f,1.f});
        }

        // flush SDF text (would bind pipeline + draw in real render path)
        // font.flush(cmd_buf);

        if (!use_vk) SDL_GL_SwapWindow(window);
    }

    // ── Cleanup ────────────────────────────────────────────────────────────────
    cs_bridge.shutdown();
    tex_cache.shutdown();
    font.shutdown();
    world.clear();
    if (gl_ctx) SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::cout << "[main] Clean exit. GGs.\n";
    return 0;
}
