#pragma once
// HellVerdict — vk_frame_renderer.hpp
// Copyright © 2026 Neofilisoft / Studio Balmung
//
// The single authoritative Vulkan render loop.
// Owns: device, swapchain, FrameSyncManager, VkPipelineManager,
//       TextureCache, SdfFontRenderer, ShaderWarmup.
//
// Call order each frame:
//   begin_frame()          → acquire swapchain image, start command buffer
//   render_world(chunks)   → world geometry (chunk LOD)
//   render_billboards()    → enemies + pickups
//   render_hud()           → 2D health/ammo bars + crosshair
//   render_text()          → SDF font pass (no depth test)
//   end_frame()            → submit, present

#include "sync/vk_sync.hpp"
#include "vk_pipeline.hpp"
#include "texture_cache.hpp"
#include "font/sdf_font.hpp"
#include "warmup/shader_warmup.hpp"
#include "../ecs/ecs_world.hpp"
#include "../world/world_map.hpp"

#include <vulkan/vulkan.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <cstdint>

namespace HellVerdict {

// ── Dynamic buffer (host-coherent, mapped persistently) ──────────────────────
struct DynBuffer {
    VkBuffer       buf  = VK_NULL_HANDLE;
    VkDeviceMemory mem  = VK_NULL_HANDLE;
    void*          mapped = nullptr;
    VkDeviceSize   capacity = 0;
};

// Tiny POD for camera data (render-side, interpolated)
struct PlayerState_GL {
    glm::vec3 eye_pos;
    glm::vec3 forward;
    float     fov_deg;
    float     aspect;
    float     near_z = 0.05f;
    float     far_z  = 200.f;
    float     pain_flash = 0.f;

    glm::mat4 view() const {
        return glm::lookAt(eye_pos, eye_pos + forward, {0,1,0});
    }
    glm::mat4 proj() const {
        return glm::perspective(glm::radians(fov_deg), aspect, near_z, far_z);
    }
};

// ── VkFrameRenderer ──────────────────────────────────────────────────────────
class VkFrameRenderer {
public:
    VkFrameRenderer() = default;
    ~VkFrameRenderer() { shutdown(); }

    // Init everything. Returns false if Vulkan unavailable → fall back to GL.
    bool init(SDL_Window* window, int w, int h);
    void shutdown();

    // Resize (called on SDL_WINDOWEVENT_RESIZED)
    void resize(int w, int h);

    // Level load: upload map mesh, preload textures, run shader warmup
    void load_level(int level_index, const WorldMap& map);

    // ── Per-frame API ─────────────────────────────────────────────────────────
    bool begin_frame();   // returns false if swapchain needs rebuild
    void render_world (const ECSWorld& world, const PlayerState_GL& cam);
    void render_billboards(const ECSWorld& world, const PlayerState_GL& cam);
    void render_hud   (const CPlayer& player, int score);
    void render_text  (SdfFontRenderer& font);
    void end_frame();

    // Access for SdfFontRenderer
    VkCommandBuffer current_cb() const {
        return _sync.frame(_sync.cur_frame()).cmd_buf;
    }
    bool is_valid() const { return _initialized; }

private:
    // ── Vulkan core objects ───────────────────────────────────────────────────
    VkInstance              _instance       = VK_NULL_HANDLE;
    VkPhysicalDevice        _phys_dev       = VK_NULL_HANDLE;
    VkDevice                _device         = VK_NULL_HANDLE;
    VkSurfaceKHR            _surface        = VK_NULL_HANDLE;
    VkQueue                 _gfx_queue      = VK_NULL_HANDLE;
    uint32_t                _gfx_family     = 0;

    // ── Swapchain ─────────────────────────────────────────────────────────────
    VkSwapchainKHR          _swapchain      = VK_NULL_HANDLE;
    VkFormat                _sc_format      = VK_FORMAT_UNDEFINED;
    VkExtent2D              _sc_extent      = {};
    std::vector<VkImage>    _sc_images;
    std::vector<VkImageView>_sc_views;
    uint32_t                _cur_image_idx  = 0;

    // ── Depth image ───────────────────────────────────────────────────────────
    VkImage                 _depth_img      = VK_NULL_HANDLE;
    VkImageView             _depth_view     = VK_NULL_HANDLE;
    VkDeviceMemory          _depth_mem      = VK_NULL_HANDLE;
    VkFormat                _depth_fmt      = VK_FORMAT_D32_SFLOAT;

    // ── Descriptor pool + layout (texture array) ──────────────────────────────
    VkDescriptorPool        _desc_pool      = VK_NULL_HANDLE;
    VkDescriptorSetLayout   _desc_layout    = VK_NULL_HANDLE;
    VkDescriptorSet         _desc_set       = VK_NULL_HANDLE;

    // ── Per-frame geometry buffers (double-buffered) ──────────────────────────
    DynBuffer _bill_buf[Sync::FRAMES_IN_FLIGHT];  // billboard vertices
    DynBuffer _hud_buf [Sync::FRAMES_IN_FLIGHT];  // HUD vertices
    static constexpr VkDeviceSize BILL_BUF_SZ = 512 * 6 * 36;  // 512 sprites × 6 verts × 36B
    static constexpr VkDeviceSize HUD_BUF_SZ  = 1024 * 6 * 20; // 1024 quads × 6 verts × 20B

    // ── World (static, uploaded once per level) ───────────────────────────────
    struct ChunkGPU {
        VkBuffer       vbo_full    = VK_NULL_HANDLE;
        VkDeviceMemory vbo_full_m  = VK_NULL_HANDLE;
        VkBuffer       ibo_full    = VK_NULL_HANDLE;
        VkDeviceMemory ibo_full_m  = VK_NULL_HANDLE;
        VkBuffer       vbo_half    = VK_NULL_HANDLE;
        VkDeviceMemory vbo_half_m  = VK_NULL_HANDLE;
        VkBuffer       ibo_half    = VK_NULL_HANDLE;
        VkDeviceMemory ibo_half_m  = VK_NULL_HANDLE;
        VkBuffer       vbo_qtr     = VK_NULL_HANDLE;
        VkDeviceMemory vbo_qtr_m   = VK_NULL_HANDLE;
        VkBuffer       ibo_qtr     = VK_NULL_HANDLE;
        VkDeviceMemory ibo_qtr_m   = VK_NULL_HANDLE;
        uint32_t       idx_full    = 0;
        uint32_t       idx_half    = 0;
        uint32_t       idx_qtr     = 0;
    };
    std::vector<ChunkGPU> _chunks;

    // ── Subsystems ────────────────────────────────────────────────────────────
    Sync::FrameSyncManager  _sync;
    VkPipelineManager       _pipelines;
    TextureCache            _tex;
    ShaderWarmup            _warmup;

    bool _initialized = false;
    int  _win_w = 0, _win_h = 0;

    // ── Init helpers ──────────────────────────────────────────────────────────
    bool _create_instance(SDL_Window* window);
    bool _select_gpu();
    bool _create_device();
    bool _create_swapchain(int w, int h);
    bool _create_depth_image(int w, int h);
    bool _create_descriptor_pool();
    bool _create_dyn_buffers();
    void _destroy_swapchain();
    void _destroy_depth_image();
    void _destroy_chunks();
    void _destroy_dyn_buffers();

    // Upload a host buffer to a device-local GPU buffer
    bool _upload_buffer(const void* data, VkDeviceSize size,
                        VkBufferUsageFlags usage,
                        VkBuffer& out_buf, VkDeviceMemory& out_mem);
    bool _create_dyn_buffer(VkDeviceSize size, DynBuffer& out);
    uint32_t _find_memory_type(uint32_t filter, VkMemoryPropertyFlags props) const;

    // Build billboard vertex data into mapped buffer this frame
    uint32_t _build_billboard_verts(const ECSWorld& world,
                                     const glm::mat4& view,
                                     void* dst, VkDeviceSize max_size);
};

} // namespace HellVerdict
