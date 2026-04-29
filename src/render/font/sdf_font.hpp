#pragma once
// HellVerdict — sdf_font.hpp
// Copyright © 2026 Neofilisoft / Studio Balmung
//
// Signed Distance Field (SDF) font renderer.
//
// Why SDF?
//   Standard bitmap fonts look blurry or pixelated when scaled up.
//   SDF fonts store the distance to the nearest glyph edge instead of
//   raw alpha — this lets the shader reconstruct a crisp edge at ANY size
//   from a single low-resolution atlas (128×128 per glyph ≈ 16px SDF = OK).
//
// Pipeline:
//   1. At build time: msdf-atlas-gen (or stb_truetype) bakes a .png atlas
//      + .json metrics → bundled in assets/fonts/
//   2. At runtime: atlas loaded once → GPU texture
//   3. Per-frame: caller fills a GlyphBatch (text, position, size, color)
//   4. SdfFontRenderer uploads a dynamic vertex buffer → 1 draw call per pass
//
// Glyph vertex layout (tight, 28 bytes):
//   vec2 pos       — screen NDC
//   vec2 uv        — atlas UV
//   vec4 color     — RGBA float
//   float sdf_size — "sharpness" scale (smoothstep range)
//
// Resolution independence:
//   Same atlas looks sharp at 16px HUD labels AND 72px death screen text.
//   The fragment shader adjusts the SDF threshold based on gl_FragCoord
//   derivative (fwidth) — no rasterization artifacts on 720p or 1080p.

#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <cstdint>

#if __has_include(<vulkan/vulkan.h>)
#  include <vulkan/vulkan.h>
#  define HV_SDF_VK 1
#else
#  define HV_SDF_VK 0
using VkDevice         = void*;
using VkBuffer         = uint64_t;
using VkDeviceMemory   = uint64_t;
using VkDescriptorSet  = uint64_t;
using VkCommandBuffer  = void*;
using VkPipelineLayout = uint64_t;
using VkPipeline       = uint64_t;
#endif

namespace HellVerdict {

// ── Glyph metrics (loaded from atlas JSON) ────────────────────────────────────
struct GlyphInfo {
    uint32_t  codepoint;
    float     u0, v0, u1, v1;     // UV in atlas (0..1)
    float     advance;             // horizontal advance in "em" units
    float     bearing_x;          // left bearing
    float     bearing_y;          // top bearing (from baseline)
    float     w, h;               // glyph size in em units
};

// ── Atlas metadata ─────────────────────────────────────────────────────────────
struct FontAtlas {
    int   tex_w = 0, tex_h = 0;   // atlas texture dimensions
    float em_size    = 64.f;       // SDF em size used at bake time
    float line_height = 1.2f;      // relative to em
    float ascender   = 0.8f;
    float descender  = -0.2f;
    uint32_t texture_id = 0;       // slot in TextureCache

    std::unordered_map<uint32_t, GlyphInfo> glyphs;

    const GlyphInfo* glyph(char32_t cp) const {
        auto it = glyphs.find((uint32_t)cp);
        return (it != glyphs.end()) ? &it->second : nullptr;
    }
};

// ── Per-glyph vertex (28 bytes) ───────────────────────────────────────────────
struct GlyphVertex {
    glm::vec2 pos;       //  8B
    glm::vec2 uv;        //  8B
    glm::vec4 color;     // 16B
    // sdf_size packed into color.a is an option but we use a separate uniform
};
static_assert(sizeof(GlyphVertex) == 32, "GlyphVertex must be 32B");

// ── Text draw call ────────────────────────────────────────────────────────────
struct TextDraw {
    std::string      text;
    glm::vec2        pos_ndc;    // top-left position in NDC (-1..1)
    float            size_px;   // desired height in pixels
    glm::vec4        color;     // RGBA
    bool             shadow = true;  // drop shadow (second pass)
};

// ── SdfFontRenderer ───────────────────────────────────────────────────────────
class SdfFontRenderer {
public:
    SdfFontRenderer() = default;
    ~SdfFontRenderer() { shutdown(); }

    // atlas_path: e.g. "assets/fonts/hell_verdict.png"
    // metrics_path: "assets/fonts/hell_verdict.json"
    // viewport_w/h: current framebuffer size (for px→NDC conversion)
    bool init(const std::string& atlas_path,
              const std::string& metrics_path,
              int viewport_w, int viewport_h);

    // Call when window resizes
    void set_viewport(int w, int h);

    // Queue a text string for rendering this frame
    void draw_text(const TextDraw& td);
    void draw_text(std::string_view text, glm::vec2 pos_ndc,
                   float size_px, glm::vec4 color = {1,1,1,1});

    // Measure text width in NDC units (for centering)
    float measure_width_ndc(std::string_view text, float size_px) const;

    // Flush all queued text to GPU and record draw calls into cb.
    // Must be called inside an active render pass (HUD pass).
    void flush(VkCommandBuffer cb);

    void shutdown();

private:
    FontAtlas _atlas;
    int       _vp_w = 1280, _vp_h = 720;

    // GPU resources
#if HV_SDF_VK
    VkDevice         _device      = VK_NULL_HANDLE;
    VkBuffer         _vert_buf    = VK_NULL_HANDLE;
    VkDeviceMemory   _vert_mem    = VK_NULL_HANDLE;
    VkPipelineLayout _pipe_layout = VK_NULL_HANDLE;
    VkPipeline       _pipeline    = VK_NULL_HANDLE;
    VkDescriptorSet  _desc_set    = VK_NULL_HANDLE;
    static constexpr uint32_t MAX_GLYPHS = 8192;
    static constexpr uint32_t VERTS_PER_GLYPH = 6;
#else
    uint32_t _gl_vao = 0, _gl_vbo = 0, _gl_shader = 0;
    static constexpr uint32_t MAX_GLYPHS = 8192;
    static constexpr uint32_t VERTS_PER_GLYPH = 6;
#endif

    std::vector<GlyphVertex> _verts;   // CPU staging for this frame

    // Load atlas PNG + JSON metrics
    bool _load_atlas(const std::string& png, const std::string& json);
    bool _load_metrics_json(const std::string& path);

    // Convert text → GlyphVertex list (layout pass)
    void _layout_text(const TextDraw& td, std::vector<GlyphVertex>& out,
                      bool is_shadow = false) const;

    // NDC helpers
    glm::vec2 _px_to_ndc(glm::vec2 px) const {
        return { px.x / _vp_w * 2.f - 1.f,
                 1.f - px.y / _vp_h * 2.f };
    }
    float _em_to_ndc_y(float em, float size_px) const {
        return em * size_px / _vp_h * 2.f;
    }
    float _em_to_ndc_x(float em, float size_px) const {
        return em * size_px / _vp_w * 2.f;
    }
};

} // namespace HellVerdict
