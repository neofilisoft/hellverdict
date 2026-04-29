// bill.vert — billboard sprite
#version 450
layout(location=0) in vec3  inPos;
layout(location=1) in vec3  inColor;
layout(location=2) in vec2  inUV;
layout(location=3) in uint  inTexSlot;

layout(push_constant) uniform PC { mat4 view; mat4 proj; } pc;

layout(location=0) out vec3  fragColor;
layout(location=1) out vec2  fragUV;
layout(location=2) out float fragFog;
layout(location=3) out flat uint fragTex;

void main() {
    vec4 vp = pc.view * vec4(inPos, 1.0);
    gl_Position = pc.proj * vp;
    fragColor = inColor;
    fragUV    = inUV;
    fragTex   = inTexSlot;
    fragFog   = clamp((length(vp.xyz) - 8.0) / 16.0, 0.0, 1.0);
}
