#version 450

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform SceneData {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 ambient;
    vec4 directionalDirection;
    vec4 directionalColor;
    uint pointLightCount;
    uint spotLightCount;
} scene;

struct PointLight {
    vec3 position;
    float intensity;
    vec3 color;
    float _padding;
};

struct SpotLight {
    vec3 position;
    float intensity;
    vec3 direction;
    float innerCos; // косинус внутреннего угла
    vec3 color;
    float outerCos; // косинус внешнего угла
};

// Массивы без фиксированного размера: их длину определяет storage буфер,
// а реальное количество источников приходит в scene.*LightCount.
layout(std430, set = 0, binding = 1) readonly buffer PointLights {
    PointLight pointLights[];
};

layout(std430, set = 0, binding = 2) readonly buffer SpotLights {
    SpotLight spotLights[];
};

layout(set = 1, binding = 0) uniform ModelData {
    mat4 model;
    mat4 normalMatrix;
    vec4 albedo;
    vec4 specular;
} object;

// Модель Блинна-Фонга для одного источника.
// N - нормаль, V - направление на камеру, L - направление на источник.
// radiance - цвет источника, уже умноженный на интенсивность и затухание.
vec3 blinnPhong(vec3 N, vec3 V, vec3 L, vec3 radiance) {
    float diffuse = max(dot(N, L), 0.0);

    vec3 H = normalize(L + V); // половинный вектор
    float specular = 0.0;
    if (diffuse > 0.0) {
        specular = pow(max(dot(N, H), 0.0), object.specular.a);
    }

    return (object.albedo.rgb * diffuse + object.specular.rgb * specular) * radiance;
}

// Затухание по закону обратных квадратов. Ограничиваем снизу,
// чтобы не получить бесконечную яркость рядом с источником.
float inverseSquareAttenuation(float distanceSquared) {
    return 1.0 / max(distanceSquared, 0.0001);
}

void main() {
    vec3 N = normalize(worldNormal);
    vec3 V = normalize(scene.cameraPosition.xyz - worldPosition);

    // Рассеянный (ambient) свет
    vec3 color = object.albedo.rgb * scene.ambient.rgb * scene.ambient.a;

    // Направленный источник
    {
        vec3 L = normalize(-scene.directionalDirection.xyz);
        vec3 radiance = scene.directionalColor.rgb * scene.directionalColor.a;
        color += blinnPhong(N, V, L, radiance);
    }

    // Точечные источники
    for (uint i = 0; i < scene.pointLightCount; ++i) {
        PointLight light = pointLights[i];
        vec3 toLight = light.position - worldPosition;
        vec3 L = normalize(toLight);
        float attenuation = inverseSquareAttenuation(dot(toLight, toLight));
        color += blinnPhong(N, V, L, light.color * light.intensity * attenuation);
    }

    // Прожекторы
    for (uint i = 0; i < scene.spotLightCount; ++i) {
        SpotLight light = spotLights[i];
        vec3 toLight = light.position - worldPosition;
        vec3 L = normalize(toLight);

        // Косинус угла между осью прожектора и направлением на фрагмент.
        float theta = dot(-L, normalize(light.direction));
        // Внутри внутреннего конуса - 1, за внешним - 0, между ними плавный переход.
        // Если углы равны, получаем жёсткую границу (epsilon почти 0).
        float epsilon = max(light.innerCos - light.outerCos, 0.0001);
        float cone = clamp((theta - light.outerCos) / epsilon, 0.0, 1.0);
        if (cone <= 0.0) {
            continue;
        }

        float attenuation = inverseSquareAttenuation(dot(toLight, toLight));
        color += blinnPhong(N, V, L, light.color * light.intensity * attenuation * cone);
    }

    outColor = vec4(color, 1.0);
}
