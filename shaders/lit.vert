#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(set = 0, binding = 0) uniform SceneData {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 ambient;              // rgb - цвет, a - интенсивность
    vec4 directionalDirection; // xyz - направление, куда светит источник
    vec4 directionalColor;     // rgb - цвет, a - интенсивность
    uint pointLightCount;
    uint spotLightCount;
} scene;

layout(set = 1, binding = 0) uniform ModelData {
    mat4 model;
    mat4 normalMatrix;
    vec4 albedoTint;
    vec4 specular; // rgb - цвет блика, a - коэффициент блеска (shininess)
    vec2 uvScale;
    float emissiveStrength;
} object;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 uv;

void main() {
    vec4 position = object.model * vec4(inPosition, 1.0);
    worldPosition = position.xyz;
    worldNormal = mat3(object.normalMatrix) * inNormal;
    uv = inUv * object.uvScale;
    gl_Position = scene.viewProjection * position;
}
