#pragma once
// HellVerdict — vk_pipeline.hpp
// Copyright © 2026 Neofilisoft / Studio Balmung
// Pipeline cache: loads pre-compiled SPIR-V, disk-backed VkPipelineCache,
// hot-reload in Debug, shader warmup integration.

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace HellVerdict {

// ── Push constants (128B — mobile minimum guaranteed) ─────────────────────────
struct alignas(16) PushConstants {
    float view[16]; // 64B  view matrix
    float proj[16]; // 64B  proj matrix  (proj[3][3] repurposed for pain_flash/alpha)
};
static_assert(sizeof(PushConstants) == 128);

// ── Pipeline IDs ──────────────────────────────────────────────────────────────
enum class PipelineId : uint32_t {
    World    = 0,  // opaque geometry, depth write, cull back
    WorldLOD,      // same pipeline, coarser draw calls
    Billboard,     // sprites, no cull, depth test
    HUD,           // 2D overlay, alpha blend, no depth
    Present,       // fullscreen blit (no vertex input)
    SdfText,       // SDF font, alpha blend, no depth
    COUNT
};

// ── VkPipelineManager ─────────────────────────────────────────────────────────
class VkPipelineManager {
public:
    VkPipelineManager()  = default;
    ~VkPipelineManager() { shutdown(); }

    // shader_dir:  "assets/shaders" — expects *.vert.spv / *.frag.spv
    // cache_path:  "pipeline.cache" — empty = no disk cache
    bool init(VkDevice device, VkFormat color_fmt, VkFormat depth_fmt,
              const std::string& shader_dir,
              const std::string& cache_path = "");

    void shutdown();

    VkPipeline       get(PipelineId id)  const;
    VkPipelineLayout layout()            const { return _layout; }
    VkPipelineCache  vk_cache()          const { return _vk_cache; }

    // Debug: check SPIR-V file mtimes, rebuild if changed
    void hot_reload_if_changed();

private:
    VkDevice         _device     = VK_NULL_HANDLE;
    VkPipelineCache  _vk_cache   = VK_NULL_HANDLE;
    VkPipelineLayout _layout     = VK_NULL_HANDLE;
    VkFormat         _color_fmt  = VK_FORMAT_UNDEFINED;
    VkFormat         _depth_fmt  = VK_FORMAT_UNDEFINED;
    std::string      _shader_dir;
    std::string      _cache_path;

    VkPipeline _pipes[(int)PipelineId::COUNT] = {};

    // Vertex attribute helpers per pipeline type
    bool _build_world_pipe   (bool cull_back, VkPipeline& out);
    bool _build_billboard_pipe(VkPipeline& out);
    bool _build_hud_pipe     (VkPipeline& out);
    bool _build_present_pipe (VkPipeline& out);
    bool _build_sdf_pipe     (VkPipeline& out);

    bool _create_pipe(VkShaderModule vert, VkShaderModule frag,
                      bool depth_write, bool alpha_blend,
                      bool cull_back,   bool depth_test,
                      int  vert_stride,
                      const VkVertexInputAttributeDescription* attrs,
                      int  attr_count,
                      VkPipeline& out);

    VkShaderModule _load_spv(const std::string& path);
    void _save_cache();
    void _load_cache();

    // Hot-reload tracking
    struct ShaderFile { std::string path; int64_t mtime = 0; };
    std::vector<ShaderFile> _tracked;
    int64_t _file_mtime(const std::string& p);
};

} // namespace HellVerdict
