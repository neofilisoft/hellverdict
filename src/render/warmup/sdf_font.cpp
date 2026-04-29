// HellVerdict — sdf_font.cpp
// Copyright © 2026 Neofilisoft / Studio Balmung

#define STB_IMAGE_IMPLEMENTATION_SDF  // avoid re-definition if stb already included
#include "sdf_font.hpp"

#include <fstream>
#include <iostream>
#include <cstring>
#include <cmath>
#include <sstream>

// stb_image for atlas PNG
#if __has_include(<stb/stb_image.h>)
#  include <stb/stb_image.h>
#elif __has_include("stb_image.h")
#  include "stb_image.h"
#endif

// Minimal JSON parser for atlas metrics (no external dependency)
// Format expected: msdf-atlas-gen / Hiero output
//   {"atlas":{"width":512,"height":512},"metrics":{"emSize":64,...},
//    "glyphs":[{"unicode":65,"advance":0.6,"atlasBounds":{"left":0,"bottom":0,"right":32,"top":48},...}]}

namespace HellVerdict {

// ── Simple JSON value extractor (no DOM — streaming) ─────────────────────────
static float json_float(const std::string& src, std::string_view key, float def=0.f) {
    auto k = std::string("\"") + std::string(key) + "\"";
    auto pos = src.find(k);
    if (pos == std::string::npos) return def;
    auto colon = src.find(':', pos);
    if (colon == std::string::npos) return def;
    return std::stof(src.substr(colon+1));
}
static int json_int(const std::string& src, std::string_view key, int def=0) {
    return (int)json_float(src, key, (float)def);
}

// ── Init ──────────────────────────────────────────────────────────────────────
bool SdfFontRenderer::init(const std::string& atlas_path,
                            const std::string& metrics_path,
                            int viewport_w, int viewport_h)
{
    _vp_w = viewport_w;
    _vp_h = viewport_h;

    if (!_load_atlas(atlas_path, metrics_path)) {
        std::cerr << "[SDF] Font init failed: " << atlas_path << "\n";
        return false;
    }

    // Reserve vertex buffer (CPU side)
    _verts.reserve(MAX_GLYPHS * VERTS_PER_GLYPH);

    std::cout << "[SDF] Font ready: " << _atlas.glyphs.size()
              << " glyphs, atlas " << _atlas.tex_w << "x" << _atlas.tex_h << "\n";
    return true;
}

void SdfFontRenderer::set_viewport(int w, int h) { _vp_w = w; _vp_h = h; }

// ── Atlas load ────────────────────────────────────────────────────────────────
bool SdfFontRenderer::_load_atlas(const std::string& png,
                                   const std::string& json)
{
    // PNG: load via stb_image (R channel = SDF distance)
    int w, h, ch;
    stbi_set_flip_vertically_on_load(0);   // atlas coords: origin top-left
    uint8_t* px = stbi_load(png.c_str(), &w, &h, &ch, 1); // 1 = grayscale
    if (!px) {
        // Generate a minimal built-in atlas for ASCII 32-127
        // This makes the game runnable without a real font file
        w = 256; h = 256;
        // Leave atlas empty — game will show placeholder boxes
        std::cout << "[SDF] Using built-in fallback atlas\n";
        _atlas.tex_w = w; _atlas.tex_h = h;
        // Populate ASCII glyph metrics with uniform spacing
        for (uint32_t cp = 32; cp < 128; ++cp) {
            GlyphInfo g{};
            g.codepoint = cp;
            int col = (cp - 32) % 16;
            int row = (cp - 32) / 16;
            g.u0 = col / 16.f; g.v0 = row / 6.f;
            g.u1 = (col+1)/16.f; g.v1 = (row+1)/6.f;
            g.advance  = 0.55f;
            g.bearing_x = 0.05f;
            g.bearing_y = 0.7f;
            g.w = 0.45f; g.h = 0.7f;
            _atlas.glyphs[cp] = g;
        }
        return true;
    }

    _atlas.tex_w = w;
    _atlas.tex_h = h;
    // TODO: upload px to GPU here (Vulkan / GL)
    stbi_image_free(px);

    return _load_metrics_json(json);
}

bool SdfFontRenderer::_load_metrics_json(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        std::cout << "[SDF] No metrics JSON: " << path << " — using defaults\n";
        _atlas.em_size     = 64.f;
        _atlas.line_height = 1.2f;
        _atlas.ascender    = 0.8f;
        _atlas.descender   = -0.2f;
        // Fallback ASCII grid
        for (uint32_t cp = 32; cp < 128; ++cp) {
            GlyphInfo g{};
            g.codepoint = cp;
            int idx = cp - 32;
            int cols = 16;
            g.u0 = (idx%cols)/16.f; g.u1 = g.u0+1/16.f;
            g.v0 = (idx/cols)/6.f;  g.v1 = g.v0+1/6.f;
            g.advance   = 0.55f;
            g.bearing_x = 0.05f;
            g.bearing_y = 0.7f;
            g.w = 0.45f; g.h = 0.7f;
            _atlas.glyphs[cp] = g;
        }
        return true;
    }

    // Read full JSON
    std::string src((std::istreambuf_iterator<char>(f)), {});

    _atlas.em_size     = json_float(src, "size",       64.f);
    _atlas.line_height = json_float(src, "lineHeight",  1.2f);
    _atlas.ascender    = json_float(src, "ascender",    0.8f);
    _atlas.descender   = json_float(src, "descender",  -0.2f);

    // Parse glyph array — scan for "unicode" entries
    size_t pos = 0;
    while ((pos = src.find("\"unicode\"", pos)) != std::string::npos) {
        pos += 9;
        uint32_t cp = (uint32_t)std::stoi(src.substr(pos+1));

        GlyphInfo g{};
        g.codepoint = cp;

        // Extract atlasBounds
        auto ab = src.find("\"atlasBounds\"", pos);
        if (ab != std::string::npos && ab < pos + 2000) {
            g.u0 = json_float(src.substr(ab, 200), "left",   0) / _atlas.tex_w;
            g.v0 = json_float(src.substr(ab, 200), "bottom", 0) / _atlas.tex_h;
            g.u1 = json_float(src.substr(ab, 200), "right",  0) / _atlas.tex_w;
            g.v1 = json_float(src.substr(ab, 200), "top",    0) / _atlas.tex_h;
        }

        // Extract planeBounds (glyph layout in em units)
        auto pb = src.find("\"planeBounds\"", pos);
        if (pb != std::string::npos && pb < pos + 2000) {
            float left   = json_float(src.substr(pb, 200), "left",   0);
            float bottom = json_float(src.substr(pb, 200), "bottom", 0);
            float right  = json_float(src.substr(pb, 200), "right",  0);
            float top    = json_float(src.substr(pb, 200), "top",    0);
            g.w         = right - left;
            g.h         = top - bottom;
            g.bearing_x = left;
            g.bearing_y = top;
        }

        g.advance = json_float(src.substr(pos, 200), "advance", 0.5f);
        _atlas.glyphs[cp] = g;
        pos++;
    }

    std::cout << "[SDF] Parsed " << _atlas.glyphs.size() << " glyphs from JSON\n";
    return true;
}

// ── Text layout ───────────────────────────────────────────────────────────────
// Converts UTF-8 text + position into a list of GlyphVertex quads.
// SDF rendering: each glyph is a quad; the shader reads the atlas channel
// and applies a smoothstep threshold at 0.5 ± (0.5/sdf_size) to reconstruct
// a perfectly sharp edge regardless of scale.
void SdfFontRenderer::_layout_text(const TextDraw& td,
                                    std::vector<GlyphVertex>& out,
                                    bool is_shadow) const
{
    float scale_x = td.size_px / _vp_w * 2.f;
    float scale_y = td.size_px / _vp_h * 2.f;

    glm::vec4 col = is_shadow
        ? glm::vec4{0.f, 0.f, 0.f, td.color.a * 0.6f}
        : td.color;

    glm::vec2 shadow_offset = is_shadow
        ? glm::vec2{ 2.f/_vp_w, -2.f/_vp_h }
        : glm::vec2{ 0.f };

    glm::vec2 cursor = td.pos_ndc + shadow_offset;
    // Baseline: offset down by ascender
    cursor.y -= _atlas.ascender * scale_y;

    // UTF-8 decode (ASCII fast-path)
    for (unsigned char c : td.text) {
        if (c == '\n') {
            cursor.x  = td.pos_ndc.x;
            cursor.y -= _atlas.line_height * scale_y;
            continue;
        }

        const GlyphInfo* g = _atlas.glyph(c);
        if (!g) g = _atlas.glyph('?');
        if (!g) { cursor.x += scale_x * 0.5f; continue; }

        // Quad corners in NDC
        float x0 = cursor.x + g->bearing_x  * scale_x;
        float y1 = cursor.y + g->bearing_y  * scale_y;
        float x1 = x0       + g->w          * scale_x;
        float y0 = y1       - g->h          * scale_y;

        // Two triangles (CCW winding)
        GlyphVertex v[6];
        // tri 0
        v[0] = {{x0,y0},{g->u0,g->v1},col};
        v[1] = {{x1,y0},{g->u1,g->v1},col};
        v[2] = {{x1,y1},{g->u1,g->v0},col};
        // tri 1
        v[3] = {{x0,y0},{g->u0,g->v1},col};
        v[4] = {{x1,y1},{g->u1,g->v0},col};
        v[5] = {{x0,y1},{g->u0,g->v0},col};

        for (auto& vv : v) out.push_back(vv);

        cursor.x += g->advance * scale_x;
    }
}

// ── Public API ────────────────────────────────────────────────────────────────
void SdfFontRenderer::draw_text(const TextDraw& td) {
    if (_verts.size() + VERTS_PER_GLYPH * td.text.size() >=
        MAX_GLYPHS * VERTS_PER_GLYPH) return; // buffer full

    if (td.shadow) _layout_text(td, _verts, true);
    _layout_text(td, _verts, false);
}

void SdfFontRenderer::draw_text(std::string_view text, glm::vec2 pos_ndc,
                                 float size_px, glm::vec4 color) {
    draw_text({ std::string(text), pos_ndc, size_px, color, true });
}

float SdfFontRenderer::measure_width_ndc(std::string_view text,
                                          float size_px) const {
    float scale_x = size_px / _vp_w * 2.f;
    float total = 0.f;
    for (unsigned char c : text) {
        const GlyphInfo* g = _atlas.glyph(c);
        if (g) total += g->advance * scale_x;
    }
    return total;
}

// ── Flush → GPU ───────────────────────────────────────────────────────────────
void SdfFontRenderer::flush(VkCommandBuffer cb) {
    if (_verts.empty()) return;

    // Upload vertices to dynamic buffer, bind pipeline, draw.
    // (GL and Vulkan paths would differ here — stub for now)
    // In Vulkan: vkMapMemory → memcpy → vkUnmapMemory
    //            vkCmdBindPipeline(_pipeline)
    //            vkCmdBindDescriptorSets (atlas texture)
    //            vkCmdBindVertexBuffers
    //            vkCmdDraw(_verts.size(), 1, 0, 0)
    // No barriers needed here: dynamic buffer is HOST_COHERENT,
    // and we're inside the HUD render pass which already barriers
    // after the 3D world pass.

    std::cout << "[SDF] flush " << _verts.size()/6 << " glyphs\n";
    _verts.clear();
}

void SdfFontRenderer::shutdown() {
    _verts.clear();
}

} // namespace HellVerdict
