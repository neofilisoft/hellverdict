#pragma once
// HellVerdict — texture_cache.hpp
// Copyright © 2026 Neofilisoft / Studio Balmung
// Loads wall.png / ground.png / ceil.png / enemy_*.png from assets/textures/
// into Vulkan VkImage + VkImageView (or GL texture for fallback).
// File names are moddable — just replace the PNG/JPG with the same name.

#include <string>
#include <unordered_map>
#include <cstdint>
#include <vector>

// Forward-declare Vulkan types to avoid pulling in full vulkan.h here
#if __has_include(<vulkan/vulkan.h>)
#  include <vulkan/vulkan.h>
#  define HV_TEX_VK 1
#else
#  define HV_TEX_VK 0
using VkDevice         = void*;
using VkPhysicalDevice = void*;
using VkCommandPool    = void*;
using VkQueue          = void*;
using VkImage          = uint64_t;
using VkImageView      = uint64_t;
using VkSampler        = uint64_t;
using VkDeviceMemory   = uint64_t;
using VkDescriptorSet  = uint64_t;
#endif

namespace HellVerdict {

// ── Slot names (moddable) ─────────────────────────────────────────────────────
// These filenames are fixed contracts — modders replace PNG/JPG with same name.
static constexpr const char* TEX_WALL        = "wall";
static constexpr const char* TEX_WALL2       = "wall2";
static constexpr const char* TEX_GROUND      = "ground";
static constexpr const char* TEX_CEIL        = "ceil";
static constexpr const char* TEX_ENEMY_ZOMBIE= "enemy_zombie";
static constexpr const char* TEX_ENEMY_IMP   = "enemy_imp";
static constexpr const char* TEX_ENEMY_DEMON = "enemy_demon";
static constexpr const char* TEX_ENEMY_BARON = "enemy_baron";
static constexpr const char* TEX_PICKUP_HEALTH = "pickup_health";
static constexpr const char* TEX_PICKUP_AMMO   = "pickup_ammo";
static constexpr const char* TEX_PICKUP_SHOTGUN= "pickup_shotgun";
static constexpr const char* TEX_EXIT        = "exit_sign";

// ── GPU texture handle ────────────────────────────────────────────────────────
struct GpuTexture {
    uint32_t id      = 0;      // slot index (used as array index in shader)
    int      width   = 0;
    int      height  = 0;
    bool     valid   = false;

#if HV_TEX_VK
    VkImage        vk_image  = VK_NULL_HANDLE;
    VkImageView    vk_view   = VK_NULL_HANDLE;
    VkDeviceMemory vk_mem    = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
#else
    uint32_t gl_handle = 0;    // OpenGL texture ID
#endif
};

// ── TextureCache ──────────────────────────────────────────────────────────────
class TextureCache {
public:
    TextureCache() = default;
    ~TextureCache() { shutdown(); }

    // Call once after Vulkan/GL device init.
    // tex_dir: e.g. "assets/textures"
    // Extensions tried in order: .png, .jpg, .jpeg
#if HV_TEX_VK
    bool init_vk(VkDevice device, VkPhysicalDevice phys,
                 VkCommandPool cmd_pool, VkQueue queue,
                 const std::string& tex_dir);
#endif
    bool init_gl(const std::string& tex_dir);

    void shutdown();

    // Load or retrieve a named texture (slot name, not filename)
    // Returns fallback checkerboard if file not found — never fails.
    uint32_t get(const std::string& name);

    const GpuTexture& texture(uint32_t id) const;
    int               count()              const { return (int)_textures.size(); }

    // Pre-warm all known slots (called at map load)
    void preload_all();

#if HV_TEX_VK
    // Vulkan descriptor set for the whole texture array (bindless)
    VkDescriptorSet  descriptor_set() const { return _array_descriptor; }
#endif

private:
    std::string _tex_dir;
    std::unordered_map<std::string, uint32_t> _name_to_id;
    std::vector<GpuTexture>                   _textures;

    bool _use_vk = false;

#if HV_TEX_VK
    VkDevice         _device   = VK_NULL_HANDLE;
    VkPhysicalDevice _phys     = VK_NULL_HANDLE;
    VkCommandPool    _cmd_pool = VK_NULL_HANDLE;
    VkQueue          _queue    = VK_NULL_HANDLE;
    VkSampler        _sampler  = VK_NULL_HANDLE;
    VkDescriptorSet  _array_descriptor = VK_NULL_HANDLE;
#endif

    // stb_image load + upload helpers
    bool _load_file(const std::string& name);
    bool _upload_vk(GpuTexture& tex, const uint8_t* pixels, int w, int h);
    bool _upload_gl(GpuTexture& tex, const uint8_t* pixels, int w, int h);
    bool _make_checkerboard(GpuTexture& tex);

    uint32_t _find_memory_type(uint32_t filter, uint32_t props) const;
};

} // namespace HellVerdict
