// present.vert — fullscreen triangle (index 0,1,2)
#version 450
layout(location=0) out vec2 fragUV;
void main() {
    // Generates a full-screen triangle from gl_VertexIndex without a VBO
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    fragUV = uv;
}
