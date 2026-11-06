#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

// Набор 0 - данные сцены (общие для всех объектов)
layout(set = 0, binding = 0) uniform SceneData {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 ambient;              // rgb - цвет, a - интенсивность
    vec4 directionalDirection; // xyz - направление, куда светит источник
    vec4 directionalColor;     // rgb - цвет, a - интенсивность
    uint pointLightCount;
    uint spotLightCount;
} scene;

// Набор 1 - данные конкретной модели
layout(set = 1, binding = 0) uniform ModelData {
    mat4 model;
    mat4 normalMatrix;
    vec4 albedo;   // rgb
    vec4 specular; // rgb - цвет блика, a - коэффициент блеска (shininess)
} object;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;

void main() {
    vec4 position = object.model * vec4(inPosition, 1.0);
    worldPosition = position.xyz;
    // Матрица нормалей учитывает неравномерный масштаб (иначе нормали "поплывут").
    worldNormal = mat3(object.normalMatrix) * inNormal;
    gl_Position = scene.viewProjection * position;
}
