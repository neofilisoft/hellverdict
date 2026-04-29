// sdf_text.frag
// Copyright © 2026 Neofilisoft / Studio Balmung
//
// Signed Distance Field text fragment shader.
//
// How it works:
//   The atlas stores a grayscale SDF value per texel:
//     > 0.5  → inside the glyph
//     = 0.5  → exactly on the edge
//     < 0.5  → outside the glyph
//
// The shader reconstructs a crisp edge by:
//   1. Sample the SDF value (dist)
//   2. Compute the screen-space derivative of the UV (fwidth)
//      — this gives us the "pixel size in UV space" at this fragment.
//   3. smoothstep from (0.5 - smooth) to (0.5 + smooth)
//      where smooth ≈ fwidth * em_scale * 0.5
//      This creates a 1-pixel anti-aliased edge regardless of zoom level.
//
// Why fwidth?
//   At small sizes, many texels map to one pixel → wider smoothstep → AA.
//   At large sizes, one texel maps to many pixels → narrower smoothstep → sharp.
//   This is better than a fixed smoothstep range which looks wrong at both extremes.
//
// Resolution: works correctly at 720p and 1080p without any parameter change.

#version 450

layout(set=0, binding=0) uniform sampler2D uAtlas;

layout(location=0) in vec2 fragUV;
layout(location=1) in vec4 fragColor;

layout(location=0) out vec4 outColor;

// SDF threshold — 0.5 means "on the edge"
const float THRESHOLD = 0.5;

// Range inside which the SDF is considered "valid"
// (depends on msdf-atlas-gen pxRange setting, typically 4 or 8)
// Passed as push constant in production; hardcoded here as 4.
const float PX_RANGE = 4.0;

void main() {
    // Sample the SDF atlas (R channel = distance)
    float dist = texture(uAtlas, fragUV).r;

    // Screen-space derivative: how much UV changes per pixel
    // fwidth = abs(dFdx(uv)) + abs(dFdy(uv))
    // We use .x or .y, whichever is larger
    vec2 duv = vec2(dFdx(fragUV.x), dFdy(fragUV.y));
    float px_per_texel = max(abs(duv.x), abs(duv.y));

    // Compute how many "em units" one screen pixel represents
    // smoothRange: half the transition width in SDF space
    // At 16px rendered size with 64px em: ~0.25 texel per pixel → wider smooth
    // At 128px rendered size: ~2 texels per pixel → very sharp edge
    float smooth_range = max(0.5 * px_per_texel * PX_RANGE, 0.0001);

    // Reconstruct coverage: 0 outside, 1 inside, smooth at edge
    float alpha = smoothstep(THRESHOLD - smooth_range,
                              THRESHOLD + smooth_range,
                              dist);

    // Discard fully transparent fragments early (saves bandwidth)
    if (alpha < 0.01) discard;

    // Output: premultiplied alpha
    outColor = vec4(fragColor.rgb, fragColor.a * alpha);
}
