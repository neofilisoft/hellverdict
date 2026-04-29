// present.frag — blit scene image to swapchain
// Uses NEAREST sampling to avoid any upscale blur on 720p→1080p
#version 450
layout(set=0, binding=0) uniform sampler2D uScene;
layout(location=0) in  vec2 fragUV;
layout(location=0) out vec4 outColor;
void main() {
    outColor = texture(uScene, fragUV);
}
