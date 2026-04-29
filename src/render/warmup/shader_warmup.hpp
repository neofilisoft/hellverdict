#pragma once
// HellVerdict — shader_warmup.hpp
// Copyright © 2026 Neofilisoft / Studio Balmung
//
// Shader Warmup System
// ====================
// Problem:  Vulkan drivers compile pipelines lazily — the FIRST time a pipeline
//           is used in a real draw call, the driver does final machine-code
//           compilation. This causes a visible frame stutter ("shader stutter")
//           that appears exactly when the player first encounters a new enemy
//           type or enters a new room with different surface materials.
//
// Solution: At level load (before the player can move), we issue one invisible
//           draw call for every pipeline + vertex format combination that might
//           appear in this level. This forces the driver to finish compiling
//           all shader variants during the loading screen, not during gameplay.
//
// Bonus:    We also serialize/deserialize the VkPipelineCache to disk.
//           On second run, the cache contains pre-compiled shader binaries for
//           this GPU — skipping both the SPIR-V→IR and IR→machine-code steps.
//           Cold start: full compile (~200ms). Warm start: cache hit (<5ms).
//
// Implementation:
//   1. WarmupPlan: list of (pipeline_id, vertex_format) pairs per level
//   2. WarmupExecutor: records a 1×1 scissored, depth-masked draw into an
//      offscreen scratch image during the loading screen
//   3. All draws use a 0-area scissor → zero pixels shaded → GPU compiles
//      the shader but produces no visible output and costs ~0 GPU time
//   4. A VkFence waits for completion before the loading screen ends

#include "../render/vk_pipeline.hpp"
#include <vulkan/vulkan.h>
#include <string>
#include <vector>

namespace HellVerdict {

// ── Warmup variant descriptor ─────────────────────────────────────────────────
// Uniquely identifies a GPU pipeline state we need to warm up.
struct WarmupVariant {
    PipelineId  pipeline;
    uint32_t    texture_slot;     // which texture array slot (affects descriptor)
    bool        alpha_blend;
    const char* debug_name;       // for logging only
};

// ── Per-level warmup plan ─────────────────────────────────────────────────────
// Defines which variants to warm up when loading a specific level.
// Levels 3-5 have more enemy types → more variants.
struct LevelWarmupPlan {
    int                        level_index;  // 0-based
    std::vector<WarmupVariant> variants;
};

// Hardcoded plans for all 5 levels
// (in practice generated from level metadata)
std::vector<LevelWarmupPlan> build_warmup_plans();

// ── ShaderWarmup ──────────────────────────────────────────────────────────────
class ShaderWarmup {
public:
    ShaderWarmup() = default;
    ~ShaderWarmup() { shutdown(); }

    bool init(VkDevice device, VkPhysicalDevice phys,
              VkQueue queue, uint32_t queue_family,
              VkPipelineCache pipeline_cache,
              const std::string& cache_path);

    // Execute warmup for the given level.
    // Blocks until all variants are compiled (called during loading screen).
    // progress_cb: called 0..100 for loading bar (optional)
    void execute(int level_index,
                 const VkPipelineManager& pm,
                 std::function<void(int progress)> progress_cb = nullptr);

    // Save pipeline cache to disk (call after warmup or on clean exit)
    void save_cache();

    void shutdown();

private:
    VkDevice         _device      = VK_NULL_HANDLE;
    VkPhysicalDevice _phys        = VK_NULL_HANDLE;
    VkQueue          _queue       = VK_NULL_HANDLE;
    uint32_t         _queue_family = 0;
    VkPipelineCache  _vk_cache    = VK_NULL_HANDLE;
    std::string      _cache_path;

    // Scratch resources for dummy draws
    VkCommandPool    _cmd_pool    = VK_NULL_HANDLE;
    VkCommandBuffer  _cmd_buf     = VK_NULL_HANDLE;
    VkFence          _fence       = VK_NULL_HANDLE;

    // 1×1 scratch image (target for dummy draws)
    VkImage          _scratch_img = VK_NULL_HANDLE;
    VkImageView      _scratch_view = VK_NULL_HANDLE;
    VkDeviceMemory   _scratch_mem = VK_NULL_HANDLE;

    bool _create_scratch_image();
    void _destroy_scratch_image();

    // Issue a single "null" draw for a variant
    // 0-area scissor → 0 pixels drawn → driver compiles shader, no visual output
    void _warmup_variant(VkCommandBuffer cb,
                          const WarmupVariant& v,
                          const VkPipelineManager& pm);

    uint32_t _find_memory_type(uint32_t filter, uint32_t props) const;
};

} // namespace HellVerdict
