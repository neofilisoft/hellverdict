// HellVerdict — vk_frame_renderer.cpp
// Copyright © 2026 Neofilisoft / Studio Balmung

#include "vk_frame_renderer.hpp"
#include "sync/vk_sync.hpp"
#include "../ecs/components.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cstring>
#include <vector>
#include <algorithm>

namespace HellVerdict {

using namespace Sync;

// ─────────────────────────────────────────────────────────────────────────────
// Memory type helper
// ─────────────────────────────────────────────────────────────────────────────
uint32_t VkFrameRenderer::_find_memory_type(uint32_t filter,
                                              VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(_phys_dev, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((filter & (1u<<i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    return UINT32_MAX;
}

// ─────────────────────────────────────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────────────────────────────────────
bool VkFrameRenderer::init(SDL_Window* window, int w, int h) {
    _win_w = w; _win_h = h;

    if (!_create_instance(window))   return false;
    if (!_select_gpu())              return false;
    if (!_create_device())           return false;
    if (!_create_swapchain(w, h))    return false;
    if (!_create_depth_image(w, h))  return false;
    if (!_create_descriptor_pool())  return false;
    if (!_create_dyn_buffers())      return false;

    // Sync manager
    _sync.init(_device, _gfx_family);

    // Pipeline manager — load SPIR-V shaders from disk
    if (!_pipelines.init(_device, _sc_format, _depth_fmt,
                         "assets/shaders",
                         "pipeline.cache")) {
        std::cerr << "[VkFrame] Pipeline init failed\n";
        return false;
    }

    // Texture cache
    _tex.init_vk(_device, _phys_dev,
                 _sync.frame(0).cmd_pool,   // borrow pool for uploads
                 _gfx_queue,
                 "assets/textures");

    // Shader warmup system
    _warmup.init(_device, _phys_dev, _gfx_queue, _gfx_family,
                 _pipelines.vk_cache(),
                 "pipeline.cache");

    _initialized = true;
    std::cout << "[VkFrame] Renderer ready " << w << "×" << h << "\n";
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Instance
// ─────────────────────────────────────────────────────────────────────────────
bool VkFrameRenderer::_create_instance(SDL_Window* window) {
    // Query SDL for required Vulkan extensions
    uint32_t ext_count = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &ext_count, nullptr);
    std::vector<const char*> exts(ext_count);
    SDL_Vulkan_GetInstanceExtensions(window, &ext_count, exts.data());

    // Always add GET_PHYSICAL_DEVICE_PROPERTIES_2 for feature queries
    exts.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName    = "Hell Verdict";
    app.applicationVersion  = VK_MAKE_VERSION(1,0,0);
    app.pEngineName         = "Balmung Engine";
    app.engineVersion       = VK_MAKE_VERSION(2,3,0);
    app.apiVersion          = VK_API_VERSION_1_3;  // require VK 1.3

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();

#ifndef NDEBUG
    const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
    ci.enabledLayerCount   = 1;
    ci.ppEnabledLayerNames = layers;
#endif

    VKS_CHECK(vkCreateInstance(&ci, nullptr, &_instance));

    // Create SDL surface
    if (!SDL_Vulkan_CreateSurface(window, _instance, &_surface)) {
        std::cerr << "[VkFrame] SDL surface: " << SDL_GetError() << "\n";
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Physical device selection
// ─────────────────────────────────────────────────────────────────────────────
bool VkFrameRenderer::_select_gpu() {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(_instance, &n, nullptr);
    if (n == 0) { std::cerr << "[VkFrame] No Vulkan GPU\n"; return false; }

    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(_instance, &n, devs.data());

    // Score: discrete > integrated > virtual
    int best = -1; VkPhysicalDevice best_dev = VK_NULL_HANDLE;
    for (auto d : devs) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        int score = 0;
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   score = 3;
        else if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 2;
        else score = 1;
        // Require VK 1.3
        if (VK_VERSION_MAJOR(p.apiVersion) < 1 ||
            (VK_VERSION_MAJOR(p.apiVersion)==1 && VK_VERSION_MINOR(p.apiVersion)<3))
            score = 0;
        if (score > best) { best = score; best_dev = d; }
    }
    if (!best_dev) { std::cerr << "[VkFrame] No VK 1.3 GPU\n"; return false; }
    _phys_dev = best_dev;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(_phys_dev, &props);
    std::cout << "[VkFrame] GPU: " << props.deviceName << "\n";
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Logical device
// ─────────────────────────────────────────────────────────────────────────────
bool VkFrameRenderer::_create_device() {
    // Find graphics queue that also supports present
    uint32_t qfc = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(_phys_dev, &qfc, nullptr);
    std::vector<VkQueueFamilyProperties> qfams(qfc);
    vkGetPhysicalDeviceQueueFamilyProperties(_phys_dev, &qfc, qfams.data());

    _gfx_family = UINT32_MAX;
    for (uint32_t i = 0; i < qfc; ++i) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(_phys_dev, i, _surface, &present);
        if ((qfams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
            _gfx_family = i; break;
        }
    }
    if (_gfx_family == UINT32_MAX) {
        std::cerr << "[VkFrame] No graphics+present queue\n"; return false;
    }

    float prio = 1.f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = _gfx_family;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &prio;

    const char* dev_exts[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,     // bindless textures
        VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,       // timeline semaphores
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,        // vkQueueSubmit2
    };

    // Enable dynamic rendering + descriptor indexing + timeline semaphores
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dyn_render{};
    dyn_render.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    dyn_render.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceDescriptorIndexingFeatures desc_idx{};
    desc_idx.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    desc_idx.pNext = &dyn_render;
    desc_idx.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    desc_idx.runtimeDescriptorArray                    = VK_TRUE;
    desc_idx.descriptorBindingPartiallyBound           = VK_TRUE;

    VkPhysicalDeviceTimelineSemaphoreFeatures tl_sem{};
    tl_sem.sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    tl_sem.pNext             = &desc_idx;
    tl_sem.timelineSemaphore = VK_TRUE;

    VkPhysicalDeviceSynchronization2FeaturesKHR sync2{};
    sync2.sType           = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
    sync2.pNext           = &tl_sem;
    sync2.synchronization2 = VK_TRUE;

    VkPhysicalDeviceFeatures feats{};
    feats.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.pNext                   = &sync2;
    ci.queueCreateInfoCount    = 1;
    ci.pQueueCreateInfos       = &qci;
    ci.enabledExtensionCount   = (uint32_t)std::size(dev_exts);
    ci.ppEnabledExtensionNames = dev_exts;
    ci.pEnabledFeatures        = &feats;

    VKS_CHECK(vkCreateDevice(_phys_dev, &ci, nullptr, &_device));
    vkGetDeviceQueue(_device, _gfx_family, 0, &_gfx_queue);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Swapchain
// ─────────────────────────────────────────────────────────────────────────────
bool VkFrameRenderer::_create_swapchain(int w, int h) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_phys_dev, _surface, &caps);

    // Prefer SRGB format for correct gamma on 720p/1080p displays
    uint32_t fmt_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(_phys_dev, _surface, &fmt_count, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmt_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(_phys_dev, _surface, &fmt_count, fmts.data());

    _sc_format = VK_FORMAT_B8G8R8A8_SRGB;
    VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    for (auto& f : fmts) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            _sc_format = f.format; color_space = f.colorSpace; break;
        }
    }

    _sc_extent = caps.currentExtent.width != UINT32_MAX
               ? caps.currentExtent
               : VkExtent2D{(uint32_t)w, (uint32_t)h};

    uint32_t img_count = std::min(caps.minImageCount + 1u,
                                   caps.maxImageCount > 0 ? caps.maxImageCount : 3u);

    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface          = _surface;
    sci.minImageCount    = img_count;
    sci.imageFormat      = _sc_format;
    sci.imageColorSpace  = color_space;
    sci.imageExtent      = _sc_extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform     = caps.currentTransform;
    sci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;  // VSync — stable FPS
    sci.clipped          = VK_TRUE;

    VKS_CHECK(vkCreateSwapchainKHR(_device, &sci, nullptr, &_swapchain));

    vkGetSwapchainImagesKHR(_device, _swapchain, &img_count, nullptr);
    _sc_images.resize(img_count);
    vkGetSwapchainImagesKHR(_device, _swapchain, &img_count, _sc_images.data());

    _sc_views.resize(img_count);
    for (uint32_t i = 0; i < img_count; ++i) {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image            = _sc_images[i];
        vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vci.format           = _sc_format;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VKS_CHECK(vkCreateImageView(_device, &vci, nullptr, &_sc_views[i]));
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Depth image
// ─────────────────────────────────────────────────────────────────────────────
bool VkFrameRenderer::_create_depth_image(int w, int h) {
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType   = VK_IMAGE_TYPE_2D;
    ici.format      = _depth_fmt;
    ici.extent      = {(uint32_t)w, (uint32_t)h, 1};
    ici.mipLevels   = 1; ici.arrayLayers = 1;
    ici.samples     = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling      = VK_IMAGE_TILING_OPTIMAL;
    ici.usage       = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VKS_CHECK(vkCreateImage(_device, &ici, nullptr, &_depth_img));

    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(_device, _depth_img, &mr);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = _find_memory_type(mr.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VKS_CHECK(vkAllocateMemory(_device, &mai, nullptr, &_depth_mem));
    vkBindImageMemory(_device, _depth_img, _depth_mem, 0);

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image            = _depth_img;
    vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    vci.format           = _depth_fmt;
    vci.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    VKS_CHECK(vkCreateImageView(_device, &vci, nullptr, &_depth_view));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor pool (bindless texture array)
// ─────────────────────────────────────────────────────────────────────────────
bool VkFrameRenderer::_create_descriptor_pool() {
    VkDescriptorPoolSize ps{};
    ps.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = 64;  // max textures in the array

    VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    ci.poolSizeCount = 1; ci.pPoolSizes = &ps;
    ci.maxSets       = 4;
    ci.flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    VKS_CHECK(vkCreateDescriptorPool(_device, &ci, nullptr, &_desc_pool));

    // Descriptor set layout: binding 0 = sampler2D array (partially bound)
    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 64;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorBindingFlags flags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo bf{};
    bf.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bf.bindingCount  = 1;
    bf.pBindingFlags = &flags;

    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.pNext        = &bf;
    lci.bindingCount = 1;
    lci.pBindings    = &binding;
    lci.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    VKS_CHECK(vkCreateDescriptorSetLayout(_device, &lci, nullptr, &_desc_layout));

    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool     = _desc_pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &_desc_layout;
    VKS_CHECK(vkAllocateDescriptorSets(_device, &ai, &_desc_set));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dynamic buffers (billboard + HUD, persistently mapped)
// ─────────────────────────────────────────────────────────────────────────────
bool VkFrameRenderer::_create_dyn_buffer(VkDeviceSize size, DynBuffer& out) {
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size        = size;
    bci.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VKS_CHECK(vkCreateBuffer(_device, &bci, nullptr, &out.buf));

    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(_device, out.buf, &mr);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = _find_memory_type(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VKS_CHECK(vkAllocateMemory(_device, &mai, nullptr, &out.mem));
    vkBindBufferMemory(_device, out.buf, out.mem, 0);
    // Persistent map — safe for HOST_COHERENT memory
    vkMapMemory(_device, out.mem, 0, size, 0, &out.mapped);
    out.capacity = size;
    return true;
}

bool VkFrameRenderer::_create_dyn_buffers() {
    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        if (!_create_dyn_buffer(BILL_BUF_SZ, _bill_buf[i])) return false;
        if (!_create_dyn_buffer(HUD_BUF_SZ,  _hud_buf[i]))  return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Level load: upload chunk meshes + run shader warmup
// ─────────────────────────────────────────────────────────────────────────────
bool VkFrameRenderer::_upload_buffer(const void* data, VkDeviceSize size,
                                      VkBufferUsageFlags usage,
                                      VkBuffer& out_buf, VkDeviceMemory& out_mem)
{
    if (size == 0) return true;
    // Staging
    VkBuffer stg; VkDeviceMemory stg_mem;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = size; bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    vkCreateBuffer(_device, &bci, nullptr, &stg);
    VkMemoryRequirements mr{}; vkGetBufferMemoryRequirements(_device, stg, &mr);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = _find_memory_type(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(_device, &mai, nullptr, &stg_mem);
    vkBindBufferMemory(_device, stg, stg_mem, 0);
    void* m; vkMapMemory(_device, stg_mem, 0, size, 0, &m);
    memcpy(m, data, size); vkUnmapMemory(_device, stg_mem);

    // Device-local
    bci.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    vkCreateBuffer(_device, &bci, nullptr, &out_buf);
    vkGetBufferMemoryRequirements(_device, out_buf, &mr);
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = _find_memory_type(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(_device, &mai, nullptr, &out_mem);
    vkBindBufferMemory(_device, out_buf, out_mem, 0);

    // One-shot copy CB
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = _sync.frame(0).cmd_pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cb; vkAllocateCommandBuffers(_device, &cbai, &cb);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);
    VkBufferCopy copy{0,0,size};
    vkCmdCopyBuffer(cb, stg, out_buf, 1, &copy);

    // Barrier: TRANSFER_WRITE → VERTEX/INDEX read
    // WHY: GPU must see the copied data before vertex shader reads it.
    barrier_buffer_transfer_to_vertex(cb, out_buf, size);

    vkEndCommandBuffer(cb);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount=1; si.pCommandBuffers=&cb;
    vkQueueSubmit(_gfx_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(_gfx_queue);
    vkFreeCommandBuffers(_device, _sync.frame(0).cmd_pool, 1, &cb);
    vkDestroyBuffer(_device, stg, nullptr);
    vkFreeMemory   (_device, stg_mem, nullptr);
    return true;
}

void VkFrameRenderer::load_level(int level_index, const WorldMap& map) {
    vkDeviceWaitIdle(_device);
    _destroy_chunks();

    const auto& meshes = map.chunk_meshes();
    _chunks.resize(meshes.size());

    for (size_t i = 0; i < meshes.size(); ++i) {
        auto& m = meshes[i];
        auto& g = _chunks[i];
        // Full LOD
        _upload_buffer(m.verts_full.data(),
                       m.verts_full.size()*sizeof(WorldVertex),
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, g.vbo_full, g.vbo_full_m);
        _upload_buffer(m.idx_full.data(),
                       m.idx_full.size()*sizeof(uint32_t),
                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT, g.ibo_full, g.ibo_full_m);
        g.idx_full = (uint32_t)m.idx_full.size();
        // Half LOD
        _upload_buffer(m.verts_half.data(),
                       m.verts_half.size()*sizeof(WorldVertex),
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, g.vbo_half, g.vbo_half_m);
        _upload_buffer(m.idx_half.data(),
                       m.idx_half.size()*sizeof(uint32_t),
                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT, g.ibo_half, g.ibo_half_m);
        g.idx_half = (uint32_t)m.idx_half.size();
        // Quarter LOD
        _upload_buffer(m.verts_quarter.data(),
                       m.verts_quarter.size()*sizeof(WorldVertex),
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, g.vbo_qtr, g.vbo_qtr_m);
        _upload_buffer(m.idx_quarter.data(),
                       m.idx_quarter.size()*sizeof(uint32_t),
                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT, g.ibo_qtr, g.ibo_qtr_m);
        g.idx_qtr = (uint32_t)m.idx_quarter.size();
    }

    // Shader warmup during level load (blocks until done)
    std::cout << "[VkFrame] Shader warmup for level " << level_index+1 << "...\n";
    _warmup.execute(level_index, _pipelines,
                    [](int p){ std::cout << "\r  Compiling shaders " << p << "%  " << std::flush; });
    std::cout << "\n[VkFrame] Warmup complete. Level loaded.\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// begin_frame
// ─────────────────────────────────────────────────────────────────────────────
bool VkFrameRenderer::begin_frame() {
    uint32_t fi = _sync.cur_frame();

    // CPU-side wait via timeline semaphore (see vk_sync.hpp)
    VkCommandBuffer cb = _sync.begin_frame(fi);

    // Acquire swapchain image
    VkResult r = vkAcquireNextImageKHR(_device, _swapchain, UINT64_MAX,
                                        _sync.frame(fi).image_available,
                                        VK_NULL_HANDLE, &_cur_image_idx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        // Swapchain needs rebuild — signal to main loop
        return false;
    }
    VKS_CHECK(r);

    // ── Barrier: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL ────────────────────────
    // This is the "start of frame" barrier explained in vk_sync.hpp.
    barrier_swapchain_to_render(cb, _sc_images[_cur_image_idx]);

    // ── Depth image: UNDEFINED → DEPTH_STENCIL_ATTACHMENT_OPTIMAL ────────────
    // WHY: Depth image layout is undefined at frame start — must transition
    //      before the depth test/write in the world render pass.
    {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = _depth_img;
        b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
    }

    // Begin dynamic rendering (VK 1.3 — no render pass object)
    VkRenderingAttachmentInfoKHR col_att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    col_att.imageView   = _sc_views[_cur_image_idx];
    col_att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    col_att.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    col_att.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    col_att.clearValue.color = {{0.02f, 0.01f, 0.01f, 1.f}};

    VkRenderingAttachmentInfoKHR dep_att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    dep_att.imageView   = _depth_view;
    dep_att.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    dep_att.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    dep_att.storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    dep_att.clearValue.depthStencil = {1.f, 0};

    VkRenderingInfoKHR ri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    ri.renderArea           = {{0,0}, _sc_extent};
    ri.layerCount           = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments    = &col_att;
    ri.pDepthAttachment     = &dep_att;

    auto vkCmdBeginRenderingKHR_fn =
        (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(_device, "vkCmdBeginRenderingKHR");
    if (vkCmdBeginRenderingKHR_fn) vkCmdBeginRenderingKHR_fn(cb, &ri);

    VkViewport vp{0,0,(float)_sc_extent.width,(float)_sc_extent.height,0,1};
    VkRect2D   sc{{0,0}, _sc_extent};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor (cb, 0, 1, &sc);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// render_world — draw visible chunks at their LOD tier
// ─────────────────────────────────────────────────────────────────────────────
void VkFrameRenderer::render_world(const ECSWorld& world,
                                    const PlayerState_GL& cam) {
    auto cb = _sync.frame(_sync.cur_frame()).cmd_buf;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      _pipelines.get(PipelineId::World));
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             _pipelines.layout(), 0, 1, &_desc_set, 0, nullptr);

    PushConstants pc{};
    memcpy(pc.view, glm::value_ptr(cam.view()), 64);
    memcpy(pc.proj, glm::value_ptr(cam.proj()), 64);
    // Pack pain_flash into proj[3][3]
    ((float*)pc.proj)[15] = cam.pain_flash;

    vkCmdPushConstants(cb, _pipelines.layout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &pc);

    // Draw chunks according to LOD level assigned by Systems::update_lod()
    world.reg().view<const CChunk>().each([&](const CChunk& chunk) {
        if (chunk.lod == LODLevel::Culled) return;
        if (chunk.index_count == 0 && chunk.vbo_offset >= _chunks.size()) return;

        // Map chunk entity coord to _chunks index
        int ci = chunk.coord.x + chunk.coord.y * 100; // rough index
        if ((size_t)ci >= _chunks.size()) return;
        auto& g = _chunks[ci];

        VkBuffer   vbo; uint32_t idx_count;
        VkBuffer   ibo;
        switch (chunk.lod) {
        case LODLevel::Half:
            vbo=g.vbo_half; ibo=g.ibo_half; idx_count=g.idx_half;
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              _pipelines.get(PipelineId::WorldLOD));
            break;
        case LODLevel::Quarter:
            vbo=g.vbo_qtr; ibo=g.ibo_qtr; idx_count=g.idx_qtr;
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              _pipelines.get(PipelineId::WorldLOD));
            break;
        default:  // Full
            vbo=g.vbo_full; ibo=g.ibo_full; idx_count=g.idx_full;
            break;
        }
        if (!vbo || !idx_count) return;
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &vbo, &off);
        vkCmdBindIndexBuffer  (cb, ibo, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed      (cb, idx_count, 1, 0, 0, 0);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// render_billboards — enemies + pickups
// ─────────────────────────────────────────────────────────────────────────────
void VkFrameRenderer::render_billboards(const ECSWorld& world,
                                         const PlayerState_GL& cam) {
    auto  fi = _sync.cur_frame();
    auto  cb = _sync.frame(fi).cmd_buf;
    auto& db = _bill_buf[fi];

    // Billboard vertex: pos(3) color(3) uv(2) tex_slot(uint) = 36B
    struct BillVert { float px,py,pz, cr,cg,cb2, u,v; uint32_t tex; };
    BillVert* dst = static_cast<BillVert*>(db.mapped);
    uint32_t vcount = 0;
    const uint32_t max_v = (uint32_t)(db.capacity / sizeof(BillVert));

    glm::vec3 cam_right{cam.view()[0][0], cam.view()[1][0], cam.view()[2][0]};
    glm::vec3 cam_up   {0,1,0};

    world.reg().view<const CEnemy, const CTransform, const CBillboard>()
        .each([&](const CEnemy& e, const CTransform& t, const CBillboard& b) {
        if (vcount + 6 > max_v) return;
        float hw = b.width * 0.5f;
        glm::vec3 base = t.position;
        glm::vec3 c[4] = {
            base + cam_right*(-hw),
            base + cam_right*( hw),
            base + cam_right*( hw) + cam_up*(b.height),
            base + cam_right*(-hw) + cam_up*(b.height),
        };
        glm::vec3 col = e.alive ? e.color
            : glm::vec3{0.15f,0.05f,0.05f};
        uint32_t tex = b.texture_id;
        // 2 triangles
        float uvs[4][2] = {{0,0},{1,0},{1,1},{0,1}};
        int tri[6] = {0,1,2, 0,2,3};
        for (int i : tri) {
            dst[vcount++] = {
                c[i].x,c[i].y,c[i].z,
                col.r,col.g,col.b,
                uvs[i][0],uvs[i][1], tex
            };
        }
    });

    // Pickup billboards
    world.reg().view<const CPickup, const CTransform, const CBillboard>()
        .each([&](const CPickup& pk, const CTransform& t, const CBillboard& b) {
        if (pk.taken || vcount + 6 > max_v) return;
        float hw = b.width * 0.5f;
        glm::vec3 base = t.position + glm::vec3{0, 0.2f + std::sin(pk.bob_phase)*0.1f, 0};
        glm::vec3 c[4] = {
            base+cam_right*(-hw), base+cam_right*(hw),
            base+cam_right*(hw)+cam_up*b.height, base+cam_right*(-hw)+cam_up*b.height
        };
        uint32_t tex = b.texture_id;
        float uvs[4][2] = {{0,0},{1,0},{1,1},{0,1}};
        int tri[6]={0,1,2,0,2,3};
        for (int i:tri) {
            dst[vcount++]={c[i].x,c[i].y,c[i].z,
                b.color.r,b.color.g,b.color.b,uvs[i][0],uvs[i][1],tex};
        }
    });

    if (vcount == 0) return;
    // HOST_COHERENT: no flush needed
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      _pipelines.get(PipelineId::Billboard));
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             _pipelines.layout(), 0, 1, &_desc_set, 0, nullptr);
    PushConstants pc{}; memcpy(pc.view,glm::value_ptr(cam.view()),64);
    memcpy(pc.proj,glm::value_ptr(cam.proj()),64);
    vkCmdPushConstants(cb,_pipelines.layout(),
        VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(pc),&pc);
    VkDeviceSize off=0;
    vkCmdBindVertexBuffers(cb,0,1,&db.buf,&off);
    vkCmdDraw(cb, vcount, 1, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// render_hud — 2D colored quads (health/ammo bars, crosshair, pain vignette)
// ─────────────────────────────────────────────────────────────────────────────
void VkFrameRenderer::render_hud(const CPlayer& pl, int score) {
    auto  fi = _sync.cur_frame();
    auto  cb = _sync.frame(fi).cmd_buf;
    auto& db = _hud_buf[fi];

    struct HudVert { float x,y,r,g,b; };
    HudVert* dst = static_cast<HudVert*>(db.mapped);
    uint32_t vc = 0;
    auto push_rect = [&](float x0,float y0,float x1,float y1,
                          float r,float g,float b2) {
        HudVert vs[6]={
            {x0,y0,r,g,b2},{x1,y0,r,g,b2},{x1,y1,r,g,b2},
            {x0,y0,r,g,b2},{x1,y1,r,g,b2},{x0,y1,r,g,b2}};
        for(auto& v:vs) dst[vc++]=v;
    };

    // Health bar
    float hp = std::clamp(pl.health/100.f, 0.f, 1.f);
    push_rect(-0.95f,-0.97f,-0.05f,-0.90f, 0.25f,0,0);
    push_rect(-0.95f,-0.97f,-0.95f+0.90f*hp,-0.90f, 0.8f,0.05f,0.05f);

    // Ammo bar
    if (pl.has_shotgun) {
        float ammo = std::clamp(pl.ammo_shells/24.f, 0.f, 1.f);
        push_rect(0.05f,-0.97f,0.95f,-0.90f, 0.1f,0.1f,0);
        push_rect(0.05f,-0.97f,0.05f+0.90f*ammo,-0.90f, 0.9f,0.8f,0.1f);
    }

    // Weapon indicator dots
    for (int i=0;i<3;++i) {
        float bx=-0.065f+(float)i*0.075f;
        bool active = (int)pl.active_weapon==i;
        bool avail  = (i<2)||(i==2&&pl.has_shotgun);
        float r=active?1.f:(avail?0.4f:0.15f);
        float g2=active?0.85f:(avail?0.4f:0.15f);
        float b2=active?0.05f:(avail?0.4f:0.15f);
        push_rect(bx,-0.875f,bx+0.055f,-0.835f, r,g2,b2);
    }

    // Crosshair
    push_rect(-0.005f,-0.025f, 0.005f, 0.025f, 1,1,1);
    push_rect(-0.025f,-0.005f, 0.025f, 0.005f, 1,1,1);

    // Pain vignette (four edges)
    if (pl.pain_flash > 0.01f) {
        float pf=pl.pain_flash;
        push_rect(-1.f,-1.f,-0.72f,1.f,  0.6f*pf,0,0);
        push_rect( 0.72f,-1.f,1.f,1.f,   0.6f*pf,0,0);
        push_rect(-1.f, 0.72f,1.f,1.f,   0.6f*pf,0,0);
        push_rect(-1.f,-1.f,1.f,-0.72f,  0.6f*pf,0,0);
    }

    if (!vc) return;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      _pipelines.get(PipelineId::HUD));
    // Pack alpha=1.0 into push constant proj[3][3]
    PushConstants pc{}; ((float*)pc.proj)[15]=1.f;
    vkCmdPushConstants(cb,_pipelines.layout(),
        VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(pc),&pc);
    VkDeviceSize off=0;
    vkCmdBindVertexBuffers(cb,0,1,&db.buf,&off);
    vkCmdDraw(cb, vc, 1, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// render_text — SDF font pass
// ─────────────────────────────────────────────────────────────────────────────
void VkFrameRenderer::render_text(SdfFontRenderer& font) {
    font.flush(current_cb());
}

// ─────────────────────────────────────────────────────────────────────────────
// end_frame
// ─────────────────────────────────────────────────────────────────────────────
void VkFrameRenderer::end_frame() {
    auto fi = _sync.cur_frame();
    auto cb = _sync.frame(fi).cmd_buf;

    auto vkCmdEndRenderingKHR_fn =
        (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(_device, "vkCmdEndRenderingKHR");
    if (vkCmdEndRenderingKHR_fn) vkCmdEndRenderingKHR_fn(cb);

    // ── Barrier: COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR ─────────────────
    // (explained in vk_sync.hpp — must happen before present)
    barrier_swapchain_to_present(cb, _sc_images[_cur_image_idx]);

    // Submit with sync objects (timeline semaphore for CPU wait next frame)
    _sync.submit(fi, _gfx_queue,
                 _sync.frame(fi).image_available,
                 _cur_image_idx);

    // Present
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &_sync.frame(fi).render_finished;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &_swapchain;
    pi.pImageIndices      = &_cur_image_idx;
    vkQueuePresentKHR(_gfx_queue, &pi);

    _sync.advance_frame();

    // Hot-reload shaders in Debug builds
    _pipelines.hot_reload_if_changed();
}

// ─────────────────────────────────────────────────────────────────────────────
// Resize
// ─────────────────────────────────────────────────────────────────────────────
void VkFrameRenderer::resize(int w, int h) {
    if (w == _win_w && h == _win_h) return;
    vkDeviceWaitIdle(_device);
    _destroy_swapchain();
    _destroy_depth_image();
    _create_swapchain(w, h);
    _create_depth_image(w, h);
    _win_w = w; _win_h = h;
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown + cleanup
// ─────────────────────────────────────────────────────────────────────────────
void VkFrameRenderer::_destroy_swapchain() {
    for (auto v : _sc_views) vkDestroyImageView(_device, v, nullptr);
    _sc_views.clear(); _sc_images.clear();
    if (_swapchain) vkDestroySwapchainKHR(_device, _swapchain, nullptr);
    _swapchain = VK_NULL_HANDLE;
}

void VkFrameRenderer::_destroy_depth_image() {
    if (_depth_view) vkDestroyImageView(_device, _depth_view, nullptr);
    if (_depth_img)  vkDestroyImage    (_device, _depth_img,  nullptr);
    if (_depth_mem)  vkFreeMemory      (_device, _depth_mem,  nullptr);
    _depth_view=VK_NULL_HANDLE; _depth_img=VK_NULL_HANDLE; _depth_mem=VK_NULL_HANDLE;
}

void VkFrameRenderer::_destroy_chunks() {
    for (auto& g : _chunks) {
        auto del = [&](VkBuffer b, VkDeviceMemory m) {
            if (b) vkDestroyBuffer(_device, b, nullptr);
            if (m) vkFreeMemory   (_device, m, nullptr);
        };
        del(g.vbo_full,g.vbo_full_m); del(g.ibo_full,g.ibo_full_m);
        del(g.vbo_half,g.vbo_half_m); del(g.ibo_half,g.ibo_half_m);
        del(g.vbo_qtr, g.vbo_qtr_m);  del(g.ibo_qtr, g.ibo_qtr_m);
    }
    _chunks.clear();
}

void VkFrameRenderer::_destroy_dyn_buffers() {
    for (int i=0;i<FRAMES_IN_FLIGHT;++i) {
        auto del_dyn=[&](DynBuffer& db){
            if(db.mapped) vkUnmapMemory(_device,db.mem);
            if(db.buf)    vkDestroyBuffer(_device,db.buf,nullptr);
            if(db.mem)    vkFreeMemory   (_device,db.mem,nullptr);
            db={};
        };
        del_dyn(_bill_buf[i]); del_dyn(_hud_buf[i]);
    }
}

void VkFrameRenderer::shutdown() {
    if (!_device) return;
    vkDeviceWaitIdle(_device);

    _warmup.shutdown();
    _pipelines.shutdown();
    _tex.shutdown();
    _sync.shutdown();
    _destroy_chunks();
    _destroy_dyn_buffers();
    _destroy_depth_image();
    _destroy_swapchain();

    if (_desc_set)    vkFreeDescriptorSets(_device,_desc_pool,1,&_desc_set);
    if (_desc_layout) vkDestroyDescriptorSetLayout(_device,_desc_layout,nullptr);
    if (_desc_pool)   vkDestroyDescriptorPool(_device,_desc_pool,nullptr);
    if (_surface)     vkDestroySurfaceKHR(_instance,_surface,nullptr);
    if (_device)      vkDestroyDevice(_device,nullptr);
    if (_instance)    vkDestroyInstance(_instance,nullptr);

    _device=VK_NULL_HANDLE; _instance=VK_NULL_HANDLE;
    _initialized=false;
    std::cout << "[VkFrame] Shutdown complete\n";
}

} // namespace HellVerdict
