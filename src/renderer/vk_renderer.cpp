// HellVerdict — vk_renderer.cpp
// Copyright © 2026 Neofilisoft / Studio Balmung
// Vulkan 1.3 renderer: swapchain, pipelines, push-constant uniforms,
// double-buffered dynamic buffers for billboards + HUD.
// Falls back gracefully to OpenGL if Vulkan is unavailable.

#include "vk_renderer.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>

// Platform surface extension selection
#if defined(_WIN32)
#  define VK_USE_PLATFORM_WIN32_KHR
#  define HV_SURFACE_EXT "VK_KHR_win32_surface"
#elif defined(__ANDROID__)
#  define VK_USE_PLATFORM_ANDROID_KHR
#  define HV_SURFACE_EXT "VK_KHR_android_surface"
#elif defined(__linux__)
#  define VK_USE_PLATFORM_XCB_KHR
#  define HV_SURFACE_EXT "VK_KHR_xcb_surface"
#else
#  define HV_SURFACE_EXT "VK_KHR_xcb_surface"
#endif

namespace HellVerdict {

VkRenderer::VkRenderer()  = default;
VkRenderer::~VkRenderer() { shutdown(); }

// ─────────────────────────────────────────────────────────────────────────────
#if HV_HAS_VK

// ── Helpers ──────────────────────────────────────────────────────────────────

#define VK_CHECK(expr, msg)                                         \
    do {                                                             \
        VkResult _r = (expr);                                        \
        if (_r != VK_SUCCESS) {                                      \
            std::cerr << "[VkRenderer] " msg " failed: " << _r << "\n"; \
            return false;                                            \
        }                                                            \
    } while(0)

VkShaderModule VkRenderer::_load_spv(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        std::cerr << "[VkRenderer] Cannot open SPIR-V: " << path << "\n";
        return VK_NULL_HANDLE;
    }
    size_t sz = f.tellg();
    f.seekg(0);
    std::vector<char> buf(sz);
    f.read(buf.data(), sz);

    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = sz;
    ci.pCode    = reinterpret_cast<const uint32_t*>(buf.data());

    VkShaderModule mod = VK_NULL_HANDLE;
    vkCreateShaderModule(_device, &ci, nullptr, &mod);
    return mod;
}

uint32_t VkRenderer::_find_memory_type(uint32_t filter,
                                        VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(_phys_device, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((filter & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    return UINT32_MAX;
}

bool VkRenderer::_create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags props,
                                  VkBuffer& buf, VkDeviceMemory& mem)
{
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = size;
    bi.usage       = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(_device, &bi, nullptr, &buf), "vkCreateBuffer");

    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(_device, buf, &mr);

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = _find_memory_type(mr.memoryTypeBits, props);
    if (ai.memoryTypeIndex == UINT32_MAX) {
        std::cerr << "[VkRenderer] No suitable memory type\n";
        return false;
    }
    VK_CHECK(vkAllocateMemory(_device, &ai, nullptr, &mem), "vkAllocateMemory");
    vkBindBufferMemory(_device, buf, mem, 0);
    return true;
}

void VkRenderer::_upload_to_device_local(VkBuffer dst, const void* data,
                                          VkDeviceSize size)
{
    // Staging buffer on host-visible memory
    VkBuffer       stg_buf; VkDeviceMemory stg_mem;
    if (!_create_buffer(size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stg_buf, stg_mem)) return;

    void* mapped;
    vkMapMemory(_device, stg_mem, 0, size, 0, &mapped);
    std::memcpy(mapped, data, size);
    vkUnmapMemory(_device, stg_mem);

    // Single-use command buffer for transfer
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = _cmd_pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb; vkAllocateCommandBuffers(_device, &ai, &cb);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    VkBufferCopy copy{};
    copy.size = size;
    vkCmdCopyBuffer(cb, stg_buf, dst, 1, &copy);
    vkEndCommandBuffer(cb);

    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    vkQueueSubmit(_gfx_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(_gfx_queue);

    vkFreeCommandBuffers(_device, _cmd_pool, 1, &cb);
    vkDestroyBuffer(_device, stg_buf, nullptr);
    vkFreeMemory(_device, stg_mem, nullptr);
}

// ── Instance ─────────────────────────────────────────────────────────────────
bool VkRenderer::_create_instance() {
    VkApplicationInfo app{};
    app.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Hell Verdict";
    app.applicationVersion = VK_MAKE_VERSION(1,0,0);
    app.pEngineName      = "Balmung Engine";
    app.engineVersion    = VK_MAKE_VERSION(2,3,0);
    app.apiVersion       = VK_API_VERSION_1_3;

    const char* exts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        HV_SURFACE_EXT,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
    };

    const char* layers[] = {
#ifndef NDEBUG
        "VK_LAYER_KHRONOS_validation"
#endif
    };

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = 3;
    ci.ppEnabledExtensionNames = exts;
#ifndef NDEBUG
    ci.enabledLayerCount       = 1;
    ci.ppEnabledLayerNames     = layers;
#endif

    VK_CHECK(vkCreateInstance(&ci, nullptr, &_instance), "vkCreateInstance");
    return true;
}

// ── Physical device ───────────────────────────────────────────────────────────
bool VkRenderer::_select_physical_device() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(_instance, &count, nullptr);
    if (count == 0) { std::cerr << "[VkRenderer] No GPU found\n"; return false; }

    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(_instance, &count, devs.data());

    // Prefer discrete, then integrated, then other
    VkPhysicalDevice best = VK_NULL_HANDLE;
    int best_score = -1;
    for (auto d : devs) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        int score = 0;
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   score = 3;
        else if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 2;
        else score = 1;
        if (score > best_score) { best_score = score; best = d; }
    }
    _phys_device = best;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(_phys_device, &props);
    _caps.device_name       = props.deviceName;
    _caps.max_push_constants = props.limits.maxPushConstantsSize;
    _caps.is_mobile = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);

    std::cout << "[VkRenderer] GPU: " << props.deviceName
              << " (push_const=" << props.limits.maxPushConstantsSize << "B)\n";
    return true;
}

// ── Logical device + queue ────────────────────────────────────────────────────
bool VkRenderer::_create_device() {
    uint32_t qfam_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(_phys_device, &qfam_count, nullptr);
    std::vector<VkQueueFamilyProperties> qfams(qfam_count);
    vkGetPhysicalDeviceQueueFamilyProperties(_phys_device, &qfam_count, qfams.data());

    uint32_t gfx_idx = UINT32_MAX;
    for (uint32_t i = 0; i < qfam_count; ++i)
        if (qfams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { gfx_idx = i; break; }
    if (gfx_idx == UINT32_MAX) return false;

    float prio = 1.f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = gfx_idx;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &prio;

    const char* exts[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME  // for VK 1.3 dynamic rendering
    };

    VkPhysicalDeviceDynamicRenderingFeaturesKHR dyn_render{};
    dyn_render.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    dyn_render.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceFeatures feats{};
    feats.fillModeNonSolid = VK_TRUE;

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.pNext                   = &dyn_render;
    ci.queueCreateInfoCount    = 1;
    ci.pQueueCreateInfos       = &qci;
    ci.enabledExtensionCount   = 2;
    ci.ppEnabledExtensionNames = exts;
    ci.pEnabledFeatures        = &feats;

    VK_CHECK(vkCreateDevice(_phys_device, &ci, nullptr, &_device), "vkCreateDevice");
    vkGetDeviceQueue(_device, gfx_idx, 0, &_gfx_queue);
    _caps.dynamic_rendering = true;

    // Command pool
    VkCommandPoolCreateInfo cpci{};
    cpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex = gfx_idx;
    cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(_device, &cpci, nullptr, &_cmd_pool), "vkCreateCommandPool");

    return true;
}

// ── Swapchain ─────────────────────────────────────────────────────────────────
bool VkRenderer::_create_swapchain(int w, int h) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_phys_device, _surface, &caps);

    _sc_format = VK_FORMAT_B8G8R8A8_SRGB;
    _sc_extent  = {(uint32_t)w, (uint32_t)h};
    if (caps.currentExtent.width != UINT32_MAX)
        _sc_extent = caps.currentExtent;

    uint32_t img_count = std::max(caps.minImageCount + 1u,
                                  std::min(caps.maxImageCount, 3u));

    VkSwapchainCreateInfoKHR sci{};
    sci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface          = _surface;
    sci.minImageCount    = img_count;
    sci.imageFormat      = _sc_format;
    sci.imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sci.imageExtent      = _sc_extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform     = caps.currentTransform;
    sci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;   // VSync on — stable 60+
    sci.clipped          = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(_device, &sci, nullptr, &_swapchain), "vkCreateSwapchainKHR");

    // Retrieve swapchain images
    vkGetSwapchainImagesKHR(_device, _swapchain, &img_count, nullptr);
    _sc_images.resize(img_count);
    vkGetSwapchainImagesKHR(_device, _swapchain, &img_count, _sc_images.data());

    _sc_views.resize(img_count);
    for (uint32_t i = 0; i < img_count; ++i) {
        VkImageViewCreateInfo ivci{};
        ivci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image    = _sc_images[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format   = _sc_format;
        ivci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VK_CHECK(vkCreateImageView(_device, &ivci, nullptr, &_sc_views[i]),
                 "vkCreateImageView");
    }
    return true;
}

// ── Pipelines ─────────────────────────────────────────────────────────────────
bool VkRenderer::_create_pipelines() {
    // Push constant range: 128 bytes (view+proj) — fits mobile minimum
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.offset     = 0;
    push.size       = sizeof(FrameUniforms);

    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &push;
    VK_CHECK(vkCreatePipelineLayout(_device, &plci, nullptr, &_pipe_layout),
             "vkCreatePipelineLayout");

    // ── World pipeline ─────────────────────────────────────────────────────
    auto build_pipeline = [&](const std::string& vert_path,
                               const std::string& frag_path,
                               VkPipeline& out_pipe,
                               bool depth_write  = true,
                               bool alpha_blend  = false,
                               bool cull_back    = true) -> bool
    {
        VkShaderModule vert = _load_spv(vert_path);
        VkShaderModule frag = _load_spv(frag_path);
        if (!vert || !frag) return false;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName  = "main";

        // Vertex input: position(3), normal(3), color(3) × float = 9×4 = 36 B/vertex
        VkVertexInputBindingDescription vbd{};
        vbd.binding   = 0;
        vbd.stride    = 9 * sizeof(float);
        vbd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription vad[3]{};
        vad[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
        vad[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12};
        vad[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 24};

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1;
        vis.pVertexBindingDescriptions      = &vbd;
        vis.vertexAttributeDescriptionCount = 3;
        vis.pVertexAttributeDescriptions    = vad;

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vps{};
        vps.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vps.viewportCount = 1;
        vps.scissorCount  = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode    = cull_back ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
        rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth   = 1.f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = VK_TRUE;
        ds.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState cba{};
        if (alpha_blend) {
            cba.blendEnable         = VK_TRUE;
            cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            cba.colorBlendOp        = VK_BLEND_OP_ADD;
            cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            cba.alphaBlendOp        = VK_BLEND_OP_ADD;
        }
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = 1;
        cbs.pAttachments    = &cba;

        VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates    = dyn_states;

        // Dynamic rendering (VK 1.3) — no explicit render pass needed
        VkPipelineRenderingCreateInfoKHR prc{};
        prc.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
        prc.colorAttachmentCount    = 1;
        prc.pColorAttachmentFormats = &_sc_format;
        prc.depthAttachmentFormat   = VK_FORMAT_D32_SFLOAT;

        VkGraphicsPipelineCreateInfo gpci{};
        gpci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gpci.pNext               = &prc;
        gpci.stageCount          = 2;
        gpci.pStages             = stages;
        gpci.pVertexInputState   = &vis;
        gpci.pInputAssemblyState = &ias;
        gpci.pViewportState      = &vps;
        gpci.pRasterizationState = &rs;
        gpci.pMultisampleState   = &ms;
        gpci.pDepthStencilState  = &ds;
        gpci.pColorBlendState    = &cbs;
        gpci.pDynamicState       = &dyn;
        gpci.layout              = _pipe_layout;

        VkResult r = vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE,
                                               1, &gpci, nullptr, &out_pipe);
        vkDestroyShaderModule(_device, vert, nullptr);
        vkDestroyShaderModule(_device, frag, nullptr);
        return r == VK_SUCCESS;
    };

    if (!build_pipeline("assets/shaders/world.vert.spv",
                         "assets/shaders/world.frag.spv",
                         _world_pipe, true,  false, true))  return false;
    if (!build_pipeline("assets/shaders/bill.vert.spv",
                         "assets/shaders/bill.frag.spv",
                         _bill_pipe,  true,  false, false)) return false;
    if (!build_pipeline("assets/shaders/hud.vert.spv",
                         "assets/shaders/hud.frag.spv",
                         _hud_pipe,   false, true,  false)) return false;

    return true;
}

// ── Sync objects ──────────────────────────────────────────────────────────────
bool VkRenderer::_create_sync_objects() {
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = _cmd_pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = FRAMES_IN_FLIGHT;
    VK_CHECK(vkAllocateCommandBuffers(_device, &ai, _cmd_bufs), "AllocCmdBufs");

    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo     fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK(vkCreateSemaphore(_device, &sci, nullptr, &_img_avail[i]),   "Semaphore imgAvail");
        VK_CHECK(vkCreateSemaphore(_device, &sci, nullptr, &_render_done[i]), "Semaphore renderDone");
        VK_CHECK(vkCreateFence   (_device, &fci, nullptr, &_frame_fences[i]), "Fence");
    }
    return true;
}

// ── Dynamic (host-visible) buffers for billboards + HUD ───────────────────────
bool VkRenderer::_create_dynamic_buffers() {
    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        if (!_create_buffer(BILL_BUF_SIZE,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                _bill_buf[i], _bill_mem[i])) return false;

        if (!_create_buffer(HUD_BUF_SIZE,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                _hud_buf[i], _hud_mem[i])) return false;
    }
    return true;
}

// ── Swapchain cleanup ─────────────────────────────────────────────────────────
void VkRenderer::_destroy_swapchain() {
    for (auto fb : _framebuffers)  vkDestroyFramebuffer(_device, fb, nullptr);
    for (auto iv : _sc_views)      vkDestroyImageView(_device, iv, nullptr);
    if (_swapchain) vkDestroySwapchainKHR(_device, _swapchain, nullptr);
    _framebuffers.clear(); _sc_views.clear(); _sc_images.clear();
    _swapchain = VK_NULL_HANDLE;
}

// ── Init ──────────────────────────────────────────────────────────────────────
bool VkRenderer::init(void* window_handle, int w, int h, bool prefer_mobile_tier) {
    _w = w; _h = h;

    if (!_create_instance())       { _fallback_needed = true; return false; }
    if (!_select_physical_device()){ _fallback_needed = true; return false; }

    // Surface creation is platform-specific — skeleton here
    // In Balmung Engine, window.cpp provides a create_vk_surface() helper
    // _surface = VkCreateSurfaceFromWindow(_instance, window_handle);
    (void)window_handle; // suppress warning in stub

    if (!_create_device())         { _fallback_needed = true; return false; }
    if (!_create_swapchain(w, h))  { _fallback_needed = true; return false; }
    if (!_create_pipelines())      { _fallback_needed = true; return false; }
    if (!_create_sync_objects())   { _fallback_needed = true; return false; }
    if (!_create_dynamic_buffers()){ _fallback_needed = true; return false; }

    _initialized = true;
    std::cout << "[VkRenderer] Ready on: " << _caps.device_name << "\n";
    return true;
}

// ── Shutdown ──────────────────────────────────────────────────────────────────
void VkRenderer::shutdown() {
    if (!_device) return;
    vkDeviceWaitIdle(_device);

    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        if (_bill_buf[i]) vkDestroyBuffer(_device, _bill_buf[i], nullptr);
        if (_bill_mem[i]) vkFreeMemory   (_device, _bill_mem[i], nullptr);
        if (_hud_buf[i])  vkDestroyBuffer(_device, _hud_buf[i],  nullptr);
        if (_hud_mem[i])  vkFreeMemory   (_device, _hud_mem[i],  nullptr);
        if (_img_avail[i])   vkDestroySemaphore(_device, _img_avail[i],   nullptr);
        if (_render_done[i]) vkDestroySemaphore(_device, _render_done[i], nullptr);
        if (_frame_fences[i]) vkDestroyFence(_device, _frame_fences[i],   nullptr);
    }
    if (_map_vbuf)     vkDestroyBuffer(_device, _map_vbuf, nullptr);
    if (_map_vmem)     vkFreeMemory   (_device, _map_vmem, nullptr);
    if (_map_ibuf)     vkDestroyBuffer(_device, _map_ibuf, nullptr);
    if (_map_imem)     vkFreeMemory   (_device, _map_imem, nullptr);
    if (_world_pipe)   vkDestroyPipeline      (_device, _world_pipe,  nullptr);
    if (_bill_pipe)    vkDestroyPipeline      (_device, _bill_pipe,   nullptr);
    if (_hud_pipe)     vkDestroyPipeline      (_device, _hud_pipe,    nullptr);
    if (_pipe_layout)  vkDestroyPipelineLayout(_device, _pipe_layout, nullptr);
    if (_cmd_pool)     vkDestroyCommandPool   (_device, _cmd_pool,    nullptr);
    _destroy_swapchain();
    if (_device)   vkDestroyDevice  (_device,   nullptr);
    if (_instance) vkDestroyInstance(_instance, nullptr);
    _device = VK_NULL_HANDLE; _instance = VK_NULL_HANDLE;
}

// ── Resize ────────────────────────────────────────────────────────────────────
void VkRenderer::resize(int w, int h) {
    if (!_initialized || (w == _w && h == _h)) return;
    vkDeviceWaitIdle(_device);
    _destroy_swapchain();
    _create_swapchain(w, h);
    _w = w; _h = h;
}

// ── Upload map ────────────────────────────────────────────────────────────────
void VkRenderer::upload_map(const MapMeshData& mesh) {
    if (!_initialized) return;

    VkDeviceSize vsize = mesh.vertices.size() * sizeof(MapVertex);
    VkDeviceSize isize = mesh.indices.size()  * sizeof(uint32_t);
    _map_idx_count = (uint32_t)mesh.indices.size();

    if (_map_vbuf) { vkDestroyBuffer(_device,_map_vbuf,nullptr); vkFreeMemory(_device,_map_vmem,nullptr); }
    if (_map_ibuf) { vkDestroyBuffer(_device,_map_ibuf,nullptr); vkFreeMemory(_device,_map_imem,nullptr); }

    _create_buffer(vsize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _map_vbuf, _map_vmem);
    _create_buffer(isize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT  | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _map_ibuf, _map_imem);

    _upload_to_device_local(_map_vbuf, mesh.vertices.data(), vsize);
    _upload_to_device_local(_map_ibuf, mesh.indices.data(),  isize);

    std::cout << "[VkRenderer] Map uploaded to device-local: "
              << mesh.vertices.size() << " verts\n";
}

// ── begin_frame / end_frame ───────────────────────────────────────────────────
void VkRenderer::begin_frame() {
    if (!_initialized) return;

    vkWaitForFences(_device, 1, &_frame_fences[_frame_idx], VK_TRUE, UINT64_MAX);
    vkResetFences  (_device, 1, &_frame_fences[_frame_idx]);

    vkAcquireNextImageKHR(_device, _swapchain, UINT64_MAX,
                          _img_avail[_frame_idx], VK_NULL_HANDLE, &_cur_image_idx);

    VkCommandBuffer cb = _cmd_bufs[_frame_idx];
    vkResetCommandBuffer(cb, 0);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    // Transition swapchain image to color attachment
    VkImageMemoryBarrier barr{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barr.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barr.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barr.srcAccessMask = 0;
    barr.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barr.image         = _sc_images[_cur_image_idx];
    barr.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0,1,0,1};
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0,nullptr, 0,nullptr, 1,&barr);

    // Begin dynamic rendering
    VkClearValue clear_col{}, clear_dep{};
    clear_col.color.float32[3] = 1.f;
    clear_dep.depthStencil     = {1.f, 0};

    VkRenderingAttachmentInfoKHR col_att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    col_att.imageView   = _sc_views[_cur_image_idx];
    col_att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    col_att.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    col_att.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    col_att.clearValue  = clear_col;

    VkRenderingInfoKHR ri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    ri.renderArea           = {{0,0}, _sc_extent};
    ri.layerCount           = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments    = &col_att;

    auto vkCmdBeginRenderingKHR =
        (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(_device, "vkCmdBeginRenderingKHR");
    if (vkCmdBeginRenderingKHR) vkCmdBeginRenderingKHR(cb, &ri);

    // Viewport + scissor
    VkViewport vp{0.f, 0.f, (float)_sc_extent.width, (float)_sc_extent.height, 0.f, 1.f};
    VkRect2D   sc{{0,0}, _sc_extent};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor (cb, 0, 1, &sc);
}

void VkRenderer::render_world(const PlayerState& player) {
    if (!_initialized || !_map_vbuf) return;
    VkCommandBuffer cb = _cmd_bufs[_frame_idx];

    // Build push constants
    FrameUniforms fu{};
    float aspect = _sc_extent.height > 0
        ? (float)_sc_extent.width / (float)_sc_extent.height : 1.f;

    // Re-use HellVerdict math to build view/proj then memcpy
    Vec3 eye    = player.eye_pos();
    Vec3 center = eye + player.forward();
    Mat4 view   = mat4_look_at(eye, center, {0,1,0});
    Mat4 proj   = mat4_perspective(70.f * DEG2RAD, aspect, 0.05f, 200.f);
    std::memcpy(fu.view, view.m, 64);
    std::memcpy(fu.proj, proj.m, 64);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, _world_pipe);
    vkCmdPushConstants(cb, _pipe_layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(FrameUniforms), &fu);

    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &_map_vbuf, &off);
    vkCmdBindIndexBuffer  (cb, _map_ibuf, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed      (cb, _map_idx_count, 1, 0, 0, 0);
}

void VkRenderer::render_enemies(const std::vector<Enemy>& enemies,
                                 const PlayerState& player)
{
    if (!_initialized || enemies.empty()) return;
    VkCommandBuffer cb = _cmd_bufs[_frame_idx];

    // Build billboard vertex data (same helper as OpenGL path)
    float aspect = _sc_extent.height > 0
        ? (float)_sc_extent.width / (float)_sc_extent.height : 1.f;
    Vec3 eye    = player.eye_pos();
    Vec3 center = eye + player.forward();
    Mat4 view   = mat4_look_at(eye, center, {0,1,0});
    Vec3 cam_right = {view.m[0], view.m[4], view.m[8]};
    Vec3 cam_up    = {0,1,0};

    std::vector<float> vdata;
    vdata.reserve(enemies.size() * 6 * 6);
    for (const auto& e : enemies) {
        if (!e.alive) continue;
        float r   = get_enemy_def(e.type).radius;
        Color3 c  = e.get_color();
        Vec3 base = {e.pos.x, e.pos.y, e.pos.z};
        Vec3 corners[4] = {
            base + cam_right*(-r),
            base + cam_right*( r),
            base + cam_right*( r) + cam_up*(r*2.f),
            base + cam_right*(-r) + cam_up*(r*2.f),
        };
        int ord[6] = {0,1,2, 0,2,3};
        for (int i : ord) {
            vdata.push_back(corners[i].x); vdata.push_back(corners[i].y); vdata.push_back(corners[i].z);
            vdata.push_back(c.r); vdata.push_back(c.g); vdata.push_back(c.b);
        }
    }
    if (vdata.empty()) return;

    size_t sz = vdata.size() * sizeof(float);
    void* mapped;
    vkMapMemory(_device, _bill_mem[_frame_idx], 0, sz, 0, &mapped);
    std::memcpy(mapped, vdata.data(), sz);
    vkUnmapMemory(_device, _bill_mem[_frame_idx]);

    // Push constants (same view/proj)
    FrameUniforms fu{};
    Mat4 proj = mat4_perspective(70.f*DEG2RAD, aspect, 0.05f, 200.f);
    std::memcpy(fu.view, view.m, 64);
    std::memcpy(fu.proj, proj.m, 64);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, _bill_pipe);
    vkCmdPushConstants(cb, _pipe_layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(FrameUniforms), &fu);

    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &_bill_buf[_frame_idx], &off);
    vkCmdDraw(cb, (uint32_t)(vdata.size()/6), 1, 0, 0);
}

void VkRenderer::render_hud(const PlayerState& player, int score, GamePhase phase) {
    if (!_initialized) return;
    VkCommandBuffer cb = _cmd_bufs[_frame_idx];
    // HUD geometry: same NDC quads — 5 floats/vert (xy + rgb)
    // Simplified version of GL path
    struct HudVert { float x,y,r,g,b; };
    auto push_rect = [](std::vector<HudVert>& buf,
                        float x0,float y0,float x1,float y1, Color3 c) {
        HudVert vs[6] = {
            {x0,y0,c.r,c.g,c.b},{x1,y0,c.r,c.g,c.b},{x1,y1,c.r,c.g,c.b},
            {x0,y0,c.r,c.g,c.b},{x1,y1,c.r,c.g,c.b},{x0,y1,c.r,c.g,c.b}
        };
        for (auto& v : vs) buf.push_back(v);
    };

    std::vector<HudVert> hud;
    float hp_f = clampf(player.health/100.f, 0.f, 1.f);
    push_rect(hud, -0.95f,-0.97f,-0.05f,-0.90f, {0.25f,0,0});
    push_rect(hud, -0.95f,-0.97f,-0.95f+0.90f*hp_f,-0.90f, COL_RED);
    // Crosshair
    push_rect(hud, -0.005f,-0.025f, 0.005f, 0.025f, COL_WHITE);
    push_rect(hud, -0.025f,-0.005f, 0.025f, 0.005f, COL_WHITE);
    if (phase == GamePhase::Dead)
        push_rect(hud, -1.f,-1.f,1.f,1.f, {0.3f,0,0});
    else if (phase == GamePhase::Victory)
        push_rect(hud, -1.f,-1.f,1.f,1.f, {0,0.15f,0});

    size_t sz = hud.size() * sizeof(HudVert);
    void* mapped;
    vkMapMemory(_device, _hud_mem[_frame_idx], 0, sz, 0, &mapped);
    std::memcpy(mapped, hud.data(), sz);
    vkUnmapMemory(_device, _hud_mem[_frame_idx]);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, _hud_pipe);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &_hud_buf[_frame_idx], &off);
    vkCmdDraw(cb, (uint32_t)hud.size(), 1, 0, 0);
}

void VkRenderer::end_frame() {
    if (!_initialized) return;
    VkCommandBuffer cb = _cmd_bufs[_frame_idx];

    auto vkCmdEndRenderingKHR =
        (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(_device, "vkCmdEndRenderingKHR");
    if (vkCmdEndRenderingKHR) vkCmdEndRenderingKHR(cb);

    // Transition to present layout
    VkImageMemoryBarrier barr{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barr.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barr.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barr.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barr.dstAccessMask = 0;
    barr.image         = _sc_images[_cur_image_idx];
    barr.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0,nullptr, 0,nullptr, 1,&barr);

    vkEndCommandBuffer(cb);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &_img_avail[_frame_idx];
    si.pWaitDstStageMask    = &wait_stage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cb;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &_render_done[_frame_idx];
    vkQueueSubmit(_gfx_queue, 1, &si, _frame_fences[_frame_idx]);

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &_render_done[_frame_idx];
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &_swapchain;
    pi.pImageIndices      = &_cur_image_idx;
    vkQueuePresentKHR(_gfx_queue, &pi);

    _frame_idx = (_frame_idx + 1) % FRAMES_IN_FLIGHT;
}

#else // !HV_HAS_VK
bool VkRenderer::init(void*, int w, int h, bool) {
    std::cout << "[VkRenderer] Vulkan headers not found — falling back to OpenGL\n";
    _fallback_needed = true; _w=w; _h=h; return false;
}
void VkRenderer::shutdown() {}
void VkRenderer::resize(int,int) {}
void VkRenderer::upload_map(const MapMeshData&) {}
void VkRenderer::begin_frame() {}
void VkRenderer::render_world(const PlayerState&) {}
void VkRenderer::render_enemies(const std::vector<Enemy>&, const PlayerState&) {}
void VkRenderer::render_hud(const PlayerState&, int, GamePhase) {}
void VkRenderer::end_frame() {}
#endif

} // namespace HellVerdict
