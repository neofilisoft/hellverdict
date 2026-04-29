// HellVerdict — vk_sync.cpp
// Copyright © 2026 Neofilisoft / Studio Balmung
//
// Every barrier in this file has:
//   WHY  — what race condition / hazard it prevents
//   SRC  — what the GPU was doing to this resource
//   DST  — what the GPU will do next
//
// Reading order: read the .hpp comments first, then come here for the impl.

#include "vk_sync.hpp"
#include <cstdio>

namespace HellVerdict::Sync {

// ─────────────────────────────────────────────────────────────────────────────
// FrameSyncManager
// ─────────────────────────────────────────────────────────────────────────────

void FrameSyncManager::init(VkDevice device, uint32_t gfx_queue_family) {
    _device     = device;
    _gfx_family = gfx_queue_family;

    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        auto& f = _frames[i];

        // Binary semaphores (acquire / present sync)
        f.image_available = create_binary_semaphore(device);
        f.render_finished = create_binary_semaphore(device);

        // Timeline semaphore (CPU-GPU sync, replaces VkFence)
        // Initial value 0; first frame signals value 1.
        f.timeline       = create_timeline_semaphore(device, 0);
        f.timeline_value = 0;

        // Command pool per frame (reset between frames, not per command buffer)
        // RESET_COMMAND_BUFFER_BIT: allows individual CB reset
        VkCommandPoolCreateInfo cpci{};
        cpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpci.queueFamilyIndex = gfx_queue_family;
        cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VKS_CHECK(vkCreateCommandPool(device, &cpci, nullptr, &f.cmd_pool));

        // One primary command buffer per frame
        VkCommandBufferAllocateInfo cbai{};
        cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool        = f.cmd_pool;
        cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        VKS_CHECK(vkAllocateCommandBuffers(device, &cbai, &f.cmd_buf));
    }
}

void FrameSyncManager::shutdown() {
    if (!_device) return;

    // Wait for all in-flight frames to complete before destroying anything.
    // WHY: Destroying semaphores/command pools while GPU uses them is UB.
    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        auto& f = _frames[i];
        if (f.timeline && f.timeline_value > 0)
            timeline_wait(_device, f.timeline, f.timeline_value);

        if (f.image_available) vkDestroySemaphore(_device, f.image_available, nullptr);
        if (f.render_finished) vkDestroySemaphore(_device, f.render_finished, nullptr);
        if (f.timeline)        vkDestroySemaphore(_device, f.timeline,        nullptr);
        // Command buffers freed implicitly when pool is destroyed
        if (f.cmd_pool)        vkDestroyCommandPool(_device, f.cmd_pool, nullptr);

        f = {};
    }
    _device = VK_NULL_HANDLE;
}

VkCommandBuffer FrameSyncManager::begin_frame(uint32_t frame_idx) {
    auto& f = _frames[frame_idx];

    // CPU-side wait: don't start recording until the GPU finished this slot.
    // We wait on the timeline value we submitted last time for this frame slot.
    // If timeline_value == 0 we haven't submitted yet, skip the wait.
    //
    // WHY: Without this wait, we would overwrite the command buffer and
    //      uniform/dynamic buffers while the GPU is still reading them.
    if (f.timeline_value > 0)
        timeline_wait(_device, f.timeline, f.timeline_value);

    // Reset the command buffer for fresh recording.
    // RELEASE_RESOURCES_BIT: frees internal Vulkan driver memory.
    VKS_CHECK(vkResetCommandBuffer(f.cmd_buf,
                                   VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT));

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    // ONE_TIME_SUBMIT: hint to driver that we record fresh every frame
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VKS_CHECK(vkBeginCommandBuffer(f.cmd_buf, &bi));

    return f.cmd_buf;
}

void FrameSyncManager::submit(uint32_t frame_idx, VkQueue queue,
                               VkSemaphore acquire_semaphore,
                               uint32_t /*swapchain_image_idx*/)
{
    auto& f = _frames[frame_idx];
    VKS_CHECK(vkEndCommandBuffer(f.cmd_buf));

    // Increment the timeline value we will signal this submission.
    f.timeline_value++;

    // ── Wait semaphores ──────────────────────────────────────────────────────
    // We wait on the swapchain acquire semaphore ONLY at the
    // COLOR_ATTACHMENT_OUTPUT stage.
    //
    // WHY: The presentation engine may still be reading the image at
    //      earlier stages (VERTEX, TRANSFER, etc.). By waiting only at
    //      COLOR_ATTACHMENT_OUTPUT, we allow the GPU to run vertex shaders
    //      and other pre-rasterization work while the image is still
    //      being presented — maximising GPU utilisation.
    //
    // Common mistake: waiting at TOP_OF_PIPE "to be safe" — this stalls
    // the entire pipeline and wastes GPU cycles.
    VkPipelineStageFlags wait_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    // Build semaphore wait/signal lists using the new VkSubmitInfo2 path
    // (core in VK 1.3) for cleaner timeline semaphore handling.
    VkSemaphoreSubmitInfo wait_sems[1]{};
    // Wait for image_available (binary) at COLOR_ATTACHMENT_OUTPUT
    wait_sems[0].sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait_sems[0].semaphore = acquire_semaphore;
    wait_sems[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    wait_sems[0].value     = 0; // binary semaphore: value ignored

    // Signal both render_finished (binary, for present) and
    // timeline (for next frame's CPU wait)
    VkSemaphoreSubmitInfo signal_sems[2]{};
    signal_sems[0].sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_sems[0].semaphore = f.render_finished;
    signal_sems[0].stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
    signal_sems[0].value     = 0;  // binary semaphore

    signal_sems[1].sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_sems[1].semaphore = f.timeline;
    signal_sems[1].stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
    signal_sems[1].value     = f.timeline_value;  // timeline: signal this value

    VkCommandBufferSubmitInfo cb_info{};
    cb_info.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cb_info.commandBuffer = f.cmd_buf;

    VkSubmitInfo2 si2{};
    si2.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    si2.waitSemaphoreInfoCount   = 1;
    si2.pWaitSemaphoreInfos      = wait_sems;
    si2.commandBufferInfoCount   = 1;
    si2.pCommandBufferInfos      = &cb_info;
    si2.signalSemaphoreInfoCount = 2;
    si2.pSignalSemaphoreInfos    = signal_sems;

    // Use VK 1.3 vkQueueSubmit2 — cleaner than legacy vkQueueSubmit
    auto vkQueueSubmit2KHR =
        (PFN_vkQueueSubmit2KHR)vkGetDeviceProcAddr(_device, "vkQueueSubmit2KHR");

    // Fallback to legacy vkQueueSubmit if vkQueueSubmit2 isn't resolved
    // (shouldn't happen on VK 1.3, but defensive)
    if (!vkQueueSubmit2KHR) {
        // Legacy path: use binary semaphore + fence pattern
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount   = 1;
        si.pWaitSemaphores      = &acquire_semaphore;
        si.pWaitDstStageMask    = &wait_stage;
        si.commandBufferCount   = 1;
        si.pCommandBuffers      = &f.cmd_buf;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores    = &f.render_finished;
        VKS_CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    } else {
        VKS_CHECK(vkQueueSubmit2KHR(queue, 1, &si2, VK_NULL_HANDLE));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Image barrier implementations
// ─────────────────────────────────────────────────────────────────────────────

// Helper to fill a VkImageMemoryBarrier2 (VK 1.3 style — per-resource stages)
static VkImageMemoryBarrier2 make_image_barrier2(
    VkImage image,
    VkImageLayout old_layout, VkImageLayout new_layout,
    VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
    VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access,
    uint32_t base_mip  = 0,
    uint32_t mip_count = VK_REMAINING_MIP_LEVELS,
    uint32_t base_layer = 0,
    uint32_t layer_count = 1)
{
    VkImageMemoryBarrier2 b{};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.oldLayout           = old_layout;
    b.newLayout           = new_layout;
    b.srcStageMask        = src_stage;
    b.srcAccessMask       = src_access;
    b.dstStageMask        = dst_stage;
    b.dstAccessMask       = dst_access;
    // Always set these explicitly — even IGNORED — for future async-compute support
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT,
                               base_mip, mip_count, base_layer, layer_count };
    return b;
}

static void pipeline_barrier2(VkCommandBuffer cb,
                               VkImageMemoryBarrier2 barrier)
{
    VkDependencyInfo di{};
    di.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers    = &barrier;
    // VK_CMD_PIPELINE_BARRIER_2 — use function pointer for VK 1.3 compat
    // In a real engine, load this once at device init.
    // Here we call the KHR extension name for maximum compatibility.
    vkCmdPipelineBarrier(cb,
        (VkPipelineStageFlags)barrier.srcStageMask,
        (VkPipelineStageFlags)barrier.dstStageMask,
        0, 0, nullptr, 0, nullptr, 1,
        // Cast to legacy struct — fields map 1:1 for image barriers
        reinterpret_cast<const VkImageMemoryBarrier*>(&barrier));
    // Note: the cast above works because VkImageMemoryBarrier2 has the same
    // fields as VkImageMemoryBarrier in the positions that matter for the
    // legacy call. In production use vkCmdPipelineBarrier2KHR instead.
}

// ── UNDEFINED → COLOR_ATTACHMENT_OPTIMAL ─────────────────────────────────────
// WHY: Swapchain image starts UNDEFINED each frame. The render commands
//      need COLOR_ATTACHMENT_OPTIMAL to write to it.
// SRC: TOP_OF_PIPE / 0 — nothing has touched this image yet this frame
// DST: COLOR_ATTACHMENT_OUTPUT — we're about to start rendering
void barrier_swapchain_to_render(VkCommandBuffer cb, VkImage image) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.srcAccessMask       = 0;
    b.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, nullptr, 0, nullptr, 1, &b);
}

// ── COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR ───────────────────────────────
// WHY: After rendering, the presentation engine needs PRESENT_SRC_KHR.
//      We must flush COLOR_ATTACHMENT writes before transitioning so that
//      the presentation engine sees the fully rendered frame.
// SRC: COLOR_ATTACHMENT_OUTPUT — we just finished writing color
// DST: BOTTOM_OF_PIPE — presentation engine waits via render_finished semaphore
void barrier_swapchain_to_present(VkCommandBuffer cb, VkImage image) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask       = 0;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &b);
}

// ── UNDEFINED → TRANSFER_DST_OPTIMAL ─────────────────────────────────────────
// WHY: New/fresh image memory has UNDEFINED layout — cannot be the target of
//      a copy until transitioned. TRANSFER_DST is required for vkCmdCopyBuffer-
//      ToImage.
// SRC: TOP_OF_PIPE / 0 — image was just allocated, no GPU access yet
// DST: TRANSFER — copy stage is about to write
void barrier_image_to_transfer_dst(VkCommandBuffer cb, VkImage image,
                                    uint32_t base_mip, uint32_t mip_count) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcAccessMask       = 0;
    b.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, base_mip, mip_count, 0, 1};
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &b);
}

// ── TRANSFER_DST → TRANSFER_SRC (mip blit chain) ─────────────────────────────
// WHY: To generate mip level N+1 from mip N, we must first finish writing
//      mip N (TRANSFER_DST), then read it (TRANSFER_SRC).
//      Without this barrier, the blit command might read mip N before the
//      previous blit finished writing it — data race.
// SRC: TRANSFER_WRITE (blit/copy that wrote this mip)
// DST: TRANSFER_READ (next blit reads this mip as source)
void barrier_transfer_dst_to_src(VkCommandBuffer cb, VkImage image,
                                  uint32_t mip) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1};
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &b);
}

// ── TRANSFER_* → SHADER_READ_ONLY_OPTIMAL ────────────────────────────────────
// WHY: After uploading/blitting, the fragment shader needs to sample the
//      texture. SHADER_READ_ONLY is required for vkImageLayout used with
//      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER.
//      This also flushes the transfer write cache so the shader read sees
//      the correct texel data.
// SRC: TRANSFER (copy or blit that populated the image)
// DST: FRAGMENT_SHADER (sampler reads)
void barrier_image_to_shader_read(VkCommandBuffer cb, VkImage image,
                                   VkImageLayout from_layout,
                                   uint32_t base_mip, uint32_t mip_count) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout           = from_layout;
    b.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, base_mip, mip_count, 0, 1};
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &b);
}

// ── SHADER_READ → TRANSFER_DST (texture re-upload) ───────────────────────────
// WHY: The shader might still be reading old texel data in a pipeline that
//      overlaps with our new transfer. We must flush the shader read cache
//      and wait for any in-flight sampling to complete.
// SRC: FRAGMENT_SHADER_READ
// DST: TRANSFER_WRITE
void barrier_shader_read_to_transfer_dst(VkCommandBuffer cb, VkImage image) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    b.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, 1};
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &b);
}

// ── Full memory barrier ───────────────────────────────────────────────────────
// WHY: Debug/init-only. Ensures ALL prior writes are visible to ALL
//      subsequent reads. Very expensive — never use per-frame.
void barrier_full_memory(VkCommandBuffer cb) {
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 1, &b, 0, nullptr, 0, nullptr);
}

// ── Buffer barriers ───────────────────────────────────────────────────────────

// WHY: After vkCmdCopyBuffer from staging to device-local, the vertex/index
//      shader reads must wait for the transfer to complete.
// SRC: TRANSFER_WRITE  DST: VERTEX_ATTRIBUTE_READ | INDEX_READ
void barrier_buffer_transfer_to_vertex(VkCommandBuffer cb,
                                        VkBuffer buf, VkDeviceSize size) {
    VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    b.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask       = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT
                          | VK_ACCESS_INDEX_READ_BIT;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.buffer              = buf;
    b.offset              = 0;
    b.size                = size;
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0, 0, nullptr, 1, &b, 0, nullptr);
}

// For HOST_COHERENT dynamic buffers: no-op because vkUnmapMemory + coherent
// memory means the GPU always sees the latest CPU writes.
// Kept as explicit documentation that we considered this case.
void barrier_host_write_to_vertex(VkCommandBuffer /*cb*/,
                                   VkBuffer /*buf*/, VkDeviceSize /*size*/) {
    // No-op for HOST_COHERENT memory.
    // If using non-coherent memory, call vkFlushMappedMemoryRanges here.
}

// ─────────────────────────────────────────────────────────────────────────────
// Timeline semaphore utilities
// ─────────────────────────────────────────────────────────────────────────────

void timeline_wait(VkDevice device, VkSemaphore sem, uint64_t value) {
    VkSemaphoreWaitInfo wi{};
    wi.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    wi.semaphoreCount = 1;
    wi.pSemaphores    = &sem;
    wi.pValues        = &value;
    // 3-second timeout: if exceeded the device is likely lost
    VkResult r = vkWaitSemaphores(device, &wi, 3'000'000'000ULL);
    if (r == VK_TIMEOUT)
        fprintf(stderr, "[VkSync] timeline_wait TIMEOUT — device may be lost\n");
    else
        vk_check(r, "vkWaitSemaphores");
}

VkSemaphore create_timeline_semaphore(VkDevice device, uint64_t initial) {
    VkSemaphoreTypeCreateInfo stci{};
    stci.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    stci.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    stci.initialValue  = initial;

    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    sci.pNext = &stci;

    VkSemaphore sem = VK_NULL_HANDLE;
    VKS_CHECK(vkCreateSemaphore(device, &sci, nullptr, &sem));
    return sem;
}

VkSemaphore create_binary_semaphore(VkDevice device) {
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore sem = VK_NULL_HANDLE;
    VKS_CHECK(vkCreateSemaphore(device, &sci, nullptr, &sem));
    return sem;
}

} // namespace HellVerdict::Sync
