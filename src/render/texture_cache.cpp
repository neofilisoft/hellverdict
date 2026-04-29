// HellVerdict — texture_cache.cpp
// Copyright © 2026 Neofilisoft / Studio Balmung
// Uses stb_image (header-only) for PNG/JPG decoding.
// Uploads to Vulkan device-local image or GL texture.

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include "texture_cache.hpp"
#include <iostream>
#include <array>
#include <cstring>

#if HV_TEX_VK
#include <vulkan/vulkan.h>
#endif

#if __has_include(<glad/glad.h>)
#include <glad/glad.h>
#define HV_TEX_GL 1
#else
#define HV_TEX_GL 0
#endif

namespace HellVerdict {

// ── Known texture slots ───────────────────────────────────────────────────────
static const std::array<const char*, 12> ALL_SLOTS = {
    TEX_WALL, TEX_WALL2, TEX_GROUND, TEX_CEIL,
    TEX_ENEMY_ZOMBIE, TEX_ENEMY_IMP, TEX_ENEMY_DEMON, TEX_ENEMY_BARON,
    TEX_PICKUP_HEALTH, TEX_PICKUP_AMMO, TEX_PICKUP_SHOTGUN,
    TEX_EXIT
};

// ── Init ─────────────────────────────────────────────────────────────────────
#if HV_TEX_VK
bool TextureCache::init_vk(VkDevice device, VkPhysicalDevice phys,
                            VkCommandPool cmd_pool, VkQueue queue,
                            const std::string& tex_dir)
{
    _device   = device;
    _phys     = phys;
    _cmd_pool = cmd_pool;
    _queue    = queue;
    _tex_dir  = tex_dir;
    _use_vk   = true;

    // Create sampler (trilinear, anisotropy 4x for sharp 1080p look)
    VkSamplerCreateInfo sci{};
    sci.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter               = VK_FILTER_NEAREST;   // Doom-style crisp pixels
    sci.minFilter               = VK_FILTER_LINEAR;
    sci.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.anisotropyEnable        = VK_TRUE;
    sci.maxAnisotropy           = 4.f;
    sci.minLod                  = 0.f;
    sci.maxLod                  = VK_LOD_CLAMP_NONE;
    vkCreateSampler(_device, &sci, nullptr, &_sampler);

    stbi_set_flip_vertically_on_load(1);
    preload_all();
    return true;
}
#endif

bool TextureCache::init_gl(const std::string& tex_dir) {
    _tex_dir = tex_dir;
    _use_vk  = false;
    stbi_set_flip_vertically_on_load(1);
    preload_all();
    return true;
}

// ── Preload ───────────────────────────────────────────────────────────────────
void TextureCache::preload_all() {
    for (auto slot : ALL_SLOTS) get(slot);
}

// ── Get / Load ────────────────────────────────────────────────────────────────
uint32_t TextureCache::get(const std::string& name) {
    auto it = _name_to_id.find(name);
    if (it != _name_to_id.end()) return it->second;
    _load_file(name);
    it = _name_to_id.find(name);
    return (it != _name_to_id.end()) ? it->second : 0;
}

const GpuTexture& TextureCache::texture(uint32_t id) const {
    static GpuTexture fallback{};
    if (id >= _textures.size()) return fallback;
    return _textures[id];
}

bool TextureCache::_load_file(const std::string& name) {
    // Try .png then .jpg then .jpeg
    static const char* exts[] = {".png", ".jpg", ".jpeg", nullptr};
    std::string found;
    for (int i = 0; exts[i]; ++i) {
        std::string path = _tex_dir + "/" + name + exts[i];
        FILE* f = fopen(path.c_str(), "rb");
        if (f) { fclose(f); found = path; break; }
    }

    GpuTexture tex;
    tex.id = (uint32_t)_textures.size();

    if (!found.empty()) {
        int w, h, ch;
        uint8_t* px = stbi_load(found.c_str(), &w, &h, &ch, 4);
        if (px) {
            tex.width  = w;
            tex.height = h;
            bool ok = _use_vk ? _upload_vk(tex, px, w, h)
                               : _upload_gl(tex, px, w, h);
            stbi_image_free(px);
            if (ok) {
                tex.valid = true;
                std::cout << "[Tex] Loaded: " << found
                          << " (" << w << "x" << h << ")\n";
            } else {
                _make_checkerboard(tex);
            }
        } else {
            std::cerr << "[Tex] stbi_load failed: " << found << "\n";
            _make_checkerboard(tex);
        }
    } else {
        std::cout << "[Tex] Not found: " << name << " — using checkerboard\n";
        _make_checkerboard(tex);
    }

    _name_to_id[name] = tex.id;
    _textures.push_back(tex);
    return tex.valid;
}

// ── OpenGL upload ─────────────────────────────────────────────────────────────
bool TextureCache::_upload_gl(GpuTexture& tex, const uint8_t* px, int w, int h) {
#if HV_TEX_GL
    uint32_t id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    tex.gl_handle = id;
    return true;
#else
    return false;
#endif
}

// ── Vulkan upload ─────────────────────────────────────────────────────────────
bool TextureCache::_upload_vk(GpuTexture& tex, const uint8_t* px, int w, int h) {
#if HV_TEX_VK
    VkDeviceSize sz = w * h * 4;

    // Staging buffer
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size  = sz;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer stg_buf; VkDeviceMemory stg_mem;
    vkCreateBuffer(_device, &bci, nullptr, &stg_buf);
    VkMemoryRequirements mr{}; vkGetBufferMemoryRequirements(_device, stg_buf, &mr);

    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = _find_memory_type(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(_device, &mai, nullptr, &stg_mem);
    vkBindBufferMemory(_device, stg_buf, stg_mem, 0);

    void* mapped; vkMapMemory(_device, stg_mem, 0, sz, 0, &mapped);
    std::memcpy(mapped, px, sz); vkUnmapMemory(_device, stg_mem);

    // Device-local image (SRGB for correct gamma on 1080p display)
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = VK_FORMAT_R8G8B8A8_SRGB;
    ici.extent        = {(uint32_t)w, (uint32_t)h, 1};
    ici.mipLevels     = (uint32_t)std::floor(std::log2(std::max(w,h))) + 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                        VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(_device, &ici, nullptr, &tex.vk_image);

    VkMemoryRequirements imr{}; vkGetImageMemoryRequirements(_device, tex.vk_image, &imr);
    VkMemoryAllocateInfo iai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    iai.allocationSize  = imr.size;
    iai.memoryTypeIndex = _find_memory_type(imr.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(_device, &iai, nullptr, &tex.vk_mem);
    vkBindImageMemory(_device, tex.vk_image, tex.vk_mem, 0);

    // Single-use CB: transition → copy → generate mips → shader-read
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = _cmd_pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cb; vkAllocateCommandBuffers(_device, &cai, &cb);

    VkCommandBufferBeginInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &cbi);

    // Transition UNDEFINED → TRANSFER_DST
    auto barrier = [&](VkImageLayout old_l, VkImageLayout new_l,
                        VkAccessFlags src_a, VkAccessFlags dst_a,
                        VkPipelineStageFlags src_s, VkPipelineStageFlags dst_s,
                        uint32_t base_mip=0, uint32_t levels=1) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout=old_l; b.newLayout=new_l;
        b.srcAccessMask=src_a; b.dstAccessMask=dst_a;
        b.image=tex.vk_image;
        b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,base_mip,levels,0,1};
        vkCmdPipelineBarrier(cb,src_s,dst_s,0,0,nullptr,0,nullptr,1,&b);
    };

    barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, ici.mipLevels);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {(uint32_t)w, (uint32_t)h, 1};
    vkCmdCopyBufferToImage(cb, stg_buf, tex.vk_image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Generate mip chain via blit (high-quality box filter)
    int mip_w = w, mip_h = h;
    for (uint32_t i = 1; i < ici.mipLevels; ++i) {
        barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                i-1, 1);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i-1, 0, 1};
        blit.srcOffsets[1]  = {mip_w, mip_h, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
        blit.dstOffsets[1]  = {std::max(1,mip_w/2), std::max(1,mip_h/2), 1};
        vkCmdBlitImage(cb,
            tex.vk_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            tex.vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);
        barrier(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, i-1, 1);
        mip_w = std::max(1, mip_w/2);
        mip_h = std::max(1, mip_h/2);
    }
    barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            ici.mipLevels-1, 1);

    vkEndCommandBuffer(cb);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount=1; si.pCommandBuffers=&cb;
    vkQueueSubmit(_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(_queue);
    vkFreeCommandBuffers(_device, _cmd_pool, 1, &cb);
    vkDestroyBuffer(_device, stg_buf, nullptr);
    vkFreeMemory(_device, stg_mem, nullptr);

    // Image view (with full mip range)
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image    = tex.vk_image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format   = VK_FORMAT_R8G8B8A8_SRGB;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, ici.mipLevels, 0, 1};
    vkCreateImageView(_device, &vci, nullptr, &tex.vk_view);

    tex.width  = w;
    tex.height = h;
    return true;
#else
    return false;
#endif
}

bool TextureCache::_make_checkerboard(GpuTexture& tex) {
    // 64×64 magenta/black fallback
    static const int SZ = 64;
    std::vector<uint8_t> px(SZ*SZ*4);
    for (int y=0;y<SZ;++y) for (int x=0;x<SZ;++x) {
        bool c = ((x/8)^(y/8))&1;
        uint8_t* p = px.data()+(y*SZ+x)*4;
        p[0]=c?255:0; p[1]=0; p[2]=c?255:0; p[3]=255;
    }
    bool ok = _use_vk ? _upload_vk(tex, px.data(), SZ, SZ)
                      : _upload_gl(tex, px.data(), SZ, SZ);
    return ok;
}

uint32_t TextureCache::_find_memory_type(uint32_t filter, uint32_t props) const {
#if HV_TEX_VK
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(_phys, &mp);
    for (uint32_t i=0;i<mp.memoryTypeCount;++i)
        if ((filter&(1u<<i))&&(mp.memoryTypes[i].propertyFlags&props)==(uint32_t)props)
            return i;
#endif
    return 0;
}

void TextureCache::shutdown() {
#if HV_TEX_VK
    if (_device && _use_vk) {
        for (auto& t : _textures) {
            if (t.vk_view)  vkDestroyImageView(_device, t.vk_view, nullptr);
            if (t.vk_image) vkDestroyImage    (_device, t.vk_image, nullptr);
            if (t.vk_mem)   vkFreeMemory      (_device, t.vk_mem,  nullptr);
        }
        if (_sampler) vkDestroySampler(_device, _sampler, nullptr);
    }
#endif
#if HV_TEX_GL
    if (!_use_vk) {
        for (auto& t : _textures)
            if (t.gl_handle) glDeleteTextures(1, &t.gl_handle);
    }
#endif
    _textures.clear();
    _name_to_id.clear();
}

} // namespace HellVerdict
