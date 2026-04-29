// world.frag — Vulkan 1.3 / GLSL 450
// Copyright © 2026 Neofilisoft / Studio Balmung
// Texture array binding: VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER array
#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(set=0, binding=0) uniform sampler2D textures[];

layout(location=0) in vec3  fragNormal;
layout(location=1) in vec2  fragUV;
layout(location=2) in float fragFog;
layout(location=3) in flat uint  fragTex;

// pain_flash packed in proj[3][3] (unused w component of proj matrix)
layout(push_constant) uniform PC { mat4 view; mat4 proj; } pc;

layout(location=0) out vec4 outColor;

void main() {
    const vec3  LIGHT   = normalize(vec3(0.4, 0.9, 0.3));
    const vec3  FOG_COL = vec3(0.02, 0.01, 0.01);
    float pain  = pc.proj[3][3];   // packed scalar

    vec4  texel = texture(textures[nonuniformEXT(fragTex)], fragUV);
    if (texel.a < 0.5) discard;   // alpha-test for masked textures

    float diff  = max(dot(normalize(fragNormal), LIGHT), 0.0);
    float lit   = 0.38 + diff * 0.62;
    vec3  color = texel.rgb * lit;
    color = mix(color, FOG_COL, fragFog);
    color = mix(color, vec3(0.7, 0.0, 0.0), pain * 0.35);

    outColor = vec4(color, 1.0);
}
