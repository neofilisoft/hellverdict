#pragma once
// HellVerdict — vk_renderer.hpp
// Copyright © 2026 Neofilisoft / Studio Balmung
// Vulkan 1.3 render backend: primary path for PC + Mobile
// Falls back to doom_renderer (OpenGL 4.1) if Vulkan init fails

#include "../game/types.hpp"
#include "../game/map_data.hpp"
#include "../game/player.hpp"
#include "../game/enemy.hpp"
#include "../game/hell_game.hpp"

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

// Detect Vulkan availability
#if __has_include(<vulkan/vulkan.h>)
#  include <vulkan/vulkan.h>
#  define HV_HAS_VK 1
#else
#  define HV_HAS_VK 0
#endif

namespace HellVerdict {

// ── Vulkan feature caps (queried at init) ─────────────────────────────────────
struct VkCaps {
    bool dynamic_rendering = false;    // VK_KHR_dynamic_rendering
    bool descriptor_indexing = false;
    uint32_t max_push_constants = 0;
    std::string device_name;
    bool is_mobile = false;            // Detected via device type (VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU on mobile)
};

// ── Per-frame uniform (push constant block) ───────────────────────────────────
// Fits in 128-byte minimum push constant range (mobile-safe)
struct alignas(16) FrameUniforms {
    float view[16];       // 64 bytes
    float proj[16];       // 64 bytes
    // Total: 128 bytes exactly — fits worst-case mobile GPUs
};

static_assert(sizeof(FrameUniforms) == 128, "FrameUniforms must be 128 bytes");

// ── VkRenderer ───────────────────────────────────────────────────────────────
class VkRenderer {
public:
    VkRenderer();
    ~VkRenderer();

    // Returns true if Vulkan is available and init succeeded.
    // Call get_fallback_needed() to decide if GL fallback should be used.
    bool init(void* window_handle, int w, int h,
              bool prefer_mobile_tier = false);

    void shutdown();
    void resize(int w, int h);

    bool is_valid()            const { return _initialized; }
    bool fallback_needed()     const { return _fallback_needed; }
    const VkCaps& caps()       const { return _caps; }

    // ── Scene upload ─────────────────────────────────────────────────────────
    void upload_map(const MapMeshData& mesh);

    // ── Per-frame render ─────────────────────────────────────────────────────
    void begin_frame();
    void render_world  (const PlayerState& player);
    void render_enemies(const std::vector<Enemy>& enemies, const PlayerState& player);
    void render_hud    (const PlayerState& player, int score, GamePhase phase);
    void end_frame();

    // Handle to engine scene texture (for Avalonia editor viewport blitting)
    uint64_t scene_texture_handle() const { return _scene_image_handle; }

private:
#if HV_HAS_VK
    // ── Vulkan objects ────────────────────────────────────────────────────────
    VkInstance               _instance       = VK_NULL_HANDLE;
    VkPhysicalDevice         _phys_device    = VK_NULL_HANDLE;
    VkDevice                 _device         = VK_NULL_HANDLE;
    VkQueue                  _gfx_queue      = VK_NULL_HANDLE;
    VkSurfaceKHR             _surface        = VK_NULL_HANDLE;
    VkSwapchainKHR           _swapchain      = VK_NULL_HANDLE;
    VkRenderPass             _render_pass    = VK_NULL_HANDLE;
    VkPipelineLayout         _pipe_layout    = VK_NULL_HANDLE;
    VkPipeline               _world_pipe     = VK_NULL_HANDLE;
    VkPipeline               _bill_pipe      = VK_NULL_HANDLE;
    VkPipeline               _hud_pipe       = VK_NULL_HANDLE;
    VkCommandPool            _cmd_pool       = VK_NULL_HANDLE;
    VkDescriptorPool         _desc_pool      = VK_NULL_HANDLE;
    VkDescriptorSetLayout    _desc_layout    = VK_NULL_HANDLE;

    // Per-frame sync (double-buffered)
    static constexpr int FRAMES_IN_FLIGHT = 2;
    VkCommandBuffer  _cmd_bufs[FRAMES_IN_FLIGHT] = {};
    VkSemaphore      _img_avail[FRAMES_IN_FLIGHT] = {};
    VkSemaphore      _render_done[FRAMES_IN_FLIGHT] = {};
    VkFence          _frame_fences[FRAMES_IN_FLIGHT] = {};
    int              _frame_idx = 0;

    // Swapchain images
    std::vector<VkImage>     _sc_images;
    std::vector<VkImageView> _sc_views;
    std::vector<VkFramebuffer> _framebuffers;
    VkFormat                 _sc_format = VK_FORMAT_UNDEFINED;
    VkExtent2D               _sc_extent = {};

    // Scene render image (off-screen target, read back to C# editor)
    VkImage                  _scene_image    = VK_NULL_HANDLE;
    VkDeviceMemory           _scene_mem      = VK_NULL_HANDLE;
    VkImageView              _scene_view     = VK_NULL_HANDLE;
    uint64_t                 _scene_image_handle = 0;

    // Map vertex/index buffers
    VkBuffer                 _map_vbuf       = VK_NULL_HANDLE;
    VkDeviceMemory           _map_vmem       = VK_NULL_HANDLE;
    VkBuffer                 _map_ibuf       = VK_NULL_HANDLE;
    VkDeviceMemory           _map_imem       = VK_NULL_HANDLE;
    uint32_t                 _map_idx_count  = 0;

    // Billboard dynamic buffer (per-frame)
    VkBuffer                 _bill_buf[FRAMES_IN_FLIGHT] = {};
    VkDeviceMemory           _bill_mem[FRAMES_IN_FLIGHT] = {};
    static constexpr size_t  BILL_BUF_SIZE = 256 * 6 * 6 * sizeof(float);

    // HUD dynamic buffer
    VkBuffer                 _hud_buf[FRAMES_IN_FLIGHT] = {};
    VkDeviceMemory           _hud_mem[FRAMES_IN_FLIGHT] = {};
    static constexpr size_t  HUD_BUF_SIZE  = 512 * 5 * sizeof(float);

    // ── Internal helpers ──────────────────────────────────────────────────────
    bool _create_instance();
    bool _select_physical_device();
    bool _create_device();
    bool _create_swapchain(int w, int h);
    bool _create_render_pass();
    bool _create_pipelines();
    bool _create_sync_objects();
    bool _create_dynamic_buffers();
    bool _create_framebuffers();
    void _destroy_swapchain();

    uint32_t _find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags props) const;
    bool     _create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags props,
                             VkBuffer& buf, VkDeviceMemory& mem);
    void     _upload_to_device_local(VkBuffer dst, const void* data, VkDeviceSize size);

    // SPIR-V shaders (compiled from GLSL at build time via glslc)
    // Paths relative to executable: assets/shaders/
    VkShaderModule _load_spv(const std::string& path);
#endif // HV_HAS_VK

    bool      _initialized     = false;
    bool      _fallback_needed = false;
    VkCaps    _caps;
    int       _w = 0, _h = 0;

    // Current frame state (used during render)
    uint32_t  _cur_image_idx = 0;
};

} // namespace HellVerdict
