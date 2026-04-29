// world.vert — Vulkan 1.3 / GLSL 450
// Copyright © 2026 Neofilisoft / Studio Balmung
#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location=0) in vec3  inPos;
layout(location=1) in vec3  inNormal;
layout(location=2) in vec2  inUV;
layout(location=3) in uint  inTexSlot;

layout(push_constant) uniform PC { mat4 view; mat4 proj; } pc;

layout(location=0) out vec3  fragNormal;
layout(location=1) out vec2  fragUV;
layout(location=2) out float fragFog;
layout(location=3) out flat uint  fragTex;

void main() {
    vec4 viewPos = pc.view * vec4(inPos, 1.0);
    gl_Position  = pc.proj * viewPos;
    fragNormal   = inNormal;
    fragUV       = inUV;
    fragTex      = inTexSlot;
    fragFog      = clamp((length(viewPos.xyz) - 10.0) / 20.0, 0.0, 1.0);
}
