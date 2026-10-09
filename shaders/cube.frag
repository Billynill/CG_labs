#version 450

layout(location = 0) in vec3 vertexColor;

layout(set = 0, binding = 0) uniform Uniforms {
    mat4 mvp;
    vec4 baseColor;
} uniforms;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(vertexColor, 1.0) * uniforms.baseColor;
}
