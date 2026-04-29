#pragma once
// HellVerdict — vk_sync.hpp
// Copyright © 2026 Neofilisoft / Studio Balmung
//
// Vulkan synchronization layer.
// This is the most bug-prone part of any Vulkan renderer — every object here
// has a clear ownership rule and every barrier carries an explicit comment
// explaining WHY it is needed (src/dst stage + access).
//
// Design principles:
//  1. All image layout transitions go through typed helpers — never raw
//     vkCmdPipelineBarrier calls in render code.
//  2. Per-frame resources are double-buffered (FRAMES_IN_FLIGHT = 2).
//  3. Timeline semaphores (VK_KHR_timeline_semaphore, core in VK 1.2+) are
//     used for CPU→GPU and cross-queue sync instead of fences where possible.
//  4. Every barrier specifies srcQueueFamilyIndex/dstQueueFamilyIndex even
//     when both are IGNORED — future-proofs async compute queue addition.
//  5. vkDeviceWaitIdle is NEVER called in the hot path; only in shutdown.

#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <string_view>
#include <stdexcept>

namespace HellVerdict::Sync {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int FRAMES_IN_FLIGHT = 2;

// ─────────────────────────────────────────────────────────────────────────────
// Error helper (throws in Debug, logs in Release)
// ─────────────────────────────────────────────────────────────────────────────
inline void vk_check(VkResult r, std::string_view msg) {
#ifdef NDEBUG
    if (r != VK_SUCCESS)
        fprintf(stderr, "[VkSync] %s failed: %d\n", msg.data(), (int)r);
#else
    if (r != VK_SUCCESS)
        throw std::runtime_error(std::string("[VkSync] ") + std::string(msg) +
                                 " failed: " + std::to_string((int)r));
#endif
}
#define VKS_CHECK(expr) ::HellVerdict::Sync::vk_check((expr), #expr)

// ─────────────────────────────────────────────────────────────────────────────
// FrameSync — per-frame synchronization objects (double-buffered)
// ─────────────────────────────────────────────────────────────────────────────
struct FrameSync {
    // Binary semaphore: signals when swapchain image is available for writing.
    // Wait stage: COLOR_ATTACHMENT_OUTPUT (GPU must have image before writing)
    VkSemaphore image_available = VK_NULL_HANDLE;

    // Binary semaphore: signals when rendering is complete.
    // Waited by vkQueuePresentKHR before flipping.
    VkSemaphore render_finished = VK_NULL_HANDLE;

    // Timeline semaphore: CPU uses this to know when the frame is done.
    // Value incremented by 1 each frame; CPU waits on (last_submitted_value).
    // This replaces the old VkFence pattern and allows CPU to submit work
    // while GPU is still executing the previous frame (true pipelining).
    VkSemaphore timeline        = VK_NULL_HANDLE;
    uint64_t    timeline_value  = 0;   // next value to signal

    VkCommandPool   cmd_pool   = VK_NULL_HANDLE;
    VkCommandBuffer cmd_buf    = VK_NULL_HANDLE;
};

// ─────────────────────────────────────────────────────────────────────────────
// FrameSyncManager
// ─────────────────────────────────────────────────────────────────────────────
class FrameSyncManager {
public:
    FrameSyncManager() = default;
    ~FrameSyncManager() { shutdown(); }

    void init(VkDevice device, uint32_t gfx_queue_family);
    void shutdown();

    // Call at start of each frame.
    // Waits until the CPU can safely write to this frame's resources.
    // Returns the command buffer to record into.
    VkCommandBuffer begin_frame(uint32_t frame_idx);

    // Submit the recorded command buffer and signal sync objects.
    // acquire_semaphore: the image_available semaphore for this frame.
    void submit(uint32_t frame_idx, VkQueue queue,
                VkSemaphore acquire_semaphore,
                uint32_t swapchain_image_idx);

    FrameSync& frame(uint32_t i) { return _frames[i]; }
    const FrameSync& frame(uint32_t i) const { return _frames[i]; }

    // Current frame index (0 or 1)
    uint32_t cur_frame() const { return _cur_frame; }
    void advance_frame() { _cur_frame = (_cur_frame + 1) % FRAMES_IN_FLIGHT; }

private:
    VkDevice   _device = VK_NULL_HANDLE;
    uint32_t   _gfx_family = 0;
    uint32_t   _cur_frame  = 0;

    std::array<FrameSync, FRAMES_IN_FLIGHT> _frames{};
};

// ─────────────────────────────────────────────────────────────────────────────
// Image barrier helpers
//
// Always use these instead of raw vkCmdPipelineBarrier to make intent clear.
// Each function is named after WHAT is being synchronized (not HOW).
// ─────────────────────────────────────────────────────────────────────────────

// Transition swapchain image: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
// Use at the start of a frame before any rendering commands.
// WHY: The swapchain image layout is undefined at frame start (or after
//      present). We must transition to COLOR_ATTACHMENT_OPTIMAL so the
//      render commands can write to it.
//      src: TOP_OF_PIPE (no prior access) — nothing to wait for
//      dst: COLOR_ATTACHMENT_OUTPUT (rendering stage)
void barrier_swapchain_to_render(VkCommandBuffer cb, VkImage image);

// Transition swapchain image: COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR
// Use after all rendering is done, before vkQueuePresentKHR.
// WHY: The presentation engine requires PRESENT_SRC_KHR. We must ensure
//      all color writes are visible (flush caches) before transitioning.
//      src: COLOR_ATTACHMENT_OUTPUT (we just wrote color)
//      dst: BOTTOM_OF_PIPE (presentation engine waits via semaphore)
void barrier_swapchain_to_present(VkCommandBuffer cb, VkImage image);

// Transition a texture image: UNDEFINED → TRANSFER_DST_OPTIMAL
// Use before uploading pixel data to a freshly allocated image.
// WHY: New images start UNDEFINED — we can't copy into them without
//      transitioning. TRANSFER_DST is the required layout for copy ops.
//      src: TOP_OF_PIPE / 0 (nothing was done to this image yet)
//      dst: TRANSFER (copy stage)
void barrier_image_to_transfer_dst(VkCommandBuffer cb, VkImage image,
                                   uint32_t base_mip  = 0,
                                   uint32_t mip_count = 1);

// Transition: TRANSFER_DST → TRANSFER_SRC (for mip blit chain)
// Use between mip levels when generating mipmaps via vkCmdBlitImage.
// WHY: After writing mip N as DST, we need it as SRC to blit into mip N+1.
//      src: TRANSFER_WRITE (we just wrote this mip as dst)
//      dst: TRANSFER_READ (we will read it as src)
void barrier_transfer_dst_to_src(VkCommandBuffer cb, VkImage image,
                                 uint32_t mip);

// Transition: TRANSFER_SRC or TRANSFER_DST → SHADER_READ_ONLY_OPTIMAL
// Use after upload/mip-gen is complete, before using in shaders.
// WHY: Shaders can only sample SHADER_READ_ONLY images. Transition from
//      whichever transfer layout was last used.
//      src: TRANSFER (any transfer op)
//      dst: FRAGMENT_SHADER (sampling)
void barrier_image_to_shader_read(VkCommandBuffer cb, VkImage image,
                                  VkImageLayout from_layout,
                                  uint32_t base_mip  = 0,
                                  uint32_t mip_count = VK_REMAINING_MIP_LEVELS);

// Transition: SHADER_READ → TRANSFER_DST (for texture streaming/update)
// Use when re-uploading a texture that's already in use.
// WHY: Must flush shader read cache before overwriting image contents.
//      src: FRAGMENT_SHADER_READ
//      dst: TRANSFER_WRITE
void barrier_shader_read_to_transfer_dst(VkCommandBuffer cb, VkImage image);

// Full memory barrier — use sparingly (sledgehammer).
// Only for debugging or one-time init paths where you need all caches flushed.
// NOT for use in hot render paths.
void barrier_full_memory(VkCommandBuffer cb);

// ─────────────────────────────────────────────────────────────────────────────
// Buffer barrier helpers
// ─────────────────────────────────────────────────────────────────────────────

// Transfer write → vertex/index read
// Use after uploading to a device-local buffer via staging.
// WHY: GPU must wait for the copy to finish before the vertex shader reads.
//      src: TRANSFER_WRITE  dst: VERTEX_ATTRIBUTE_READ / INDEX_READ
void barrier_buffer_transfer_to_vertex(VkCommandBuffer cb,
                                       VkBuffer buf, VkDeviceSize size);

// Host write → vertex read (for dynamic per-frame buffers like billboards)
// WHY: We map/write on CPU and then bind as vertex buffer. Vulkan requires
//      an explicit barrier to ensure the GPU sees the new data.
//      (HOST_COHERENT memory omits this, but non-coherent needs it.)
// For HOST_COHERENT memory (which we always request), this is a no-op
// but left here as documentation and future robustness.
void barrier_host_write_to_vertex(VkCommandBuffer cb,
                                  VkBuffer buf, VkDeviceSize size);

// ─────────────────────────────────────────────────────────────────────────────
// Timeline semaphore helpers
// ─────────────────────────────────────────────────────────────────────────────

// CPU-side wait: block until timeline semaphore reaches value.
// Timeout: 3 seconds (device lost guard).
void timeline_wait(VkDevice device, VkSemaphore sem, uint64_t value);

// Create a timeline semaphore
VkSemaphore create_timeline_semaphore(VkDevice device, uint64_t initial = 0);

// Create a binary semaphore
VkSemaphore create_binary_semaphore(VkDevice device);

} // namespace HellVerdict::Sync
