// hud.frag
#version 450
layout(location=0) in  vec3 fragColor;
layout(location=0) out vec4 outColor;
layout(push_constant) uniform PC { mat4 view; mat4 proj; } pc;
void main() {
    float alpha = pc.proj[3][3];
    outColor = vec4(fragColor, alpha < 0.01 ? 1.0 : alpha);
}
