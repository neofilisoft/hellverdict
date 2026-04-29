// bill.frag
#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(set=0, binding=0) uniform sampler2D textures[];

layout(location=0) in vec3  fragColor;
layout(location=1) in vec2  fragUV;
layout(location=2) in float fragFog;
layout(location=3) in flat uint fragTex;

layout(location=0) out vec4 outColor;

void main() {
    const vec3 FOG = vec3(0.02, 0.01, 0.01);
    vec4 t = (fragTex == 0u)
           ? vec4(fragColor, 1.0)
           : texture(textures[nonuniformEXT(fragTex)], fragUV);
    if (t.a < 0.1) discard;
    outColor = vec4(mix(t.rgb * fragColor, FOG, fragFog), t.a);
}
