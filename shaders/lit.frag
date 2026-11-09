#version 450

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 uv;

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

layout(std430, set = 0, binding = 1) readonly buffer PointLights {
    PointLight pointLights[];
};

layout(std430, set = 0, binding = 2) readonly buffer SpotLights {
    SpotLight spotLights[];
};

layout(set = 1, binding = 0) uniform ModelData {
    mat4 model;
    mat4 normalMatrix;
    vec4 albedoTint;
    vec4 specular;
    vec2 uvScale;
    float emissiveStrength;
} object;

layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D specularMap;
layout(set = 1, binding = 3) uniform sampler2D emissiveMap;

layout(push_constant) uniform SamplingParams {
    uint flags;
    float time;
} sampling;

const uint SAMPLING_ANIMATED = 1u;

vec3 sampleAnimatedAlbedo(vec2 baseUv, float t) {
    // 1. Прокрутка по диагонали
    vec2 scrolledUv = baseUv + vec2(0.15, 0.05) * t;

    // 2. Вращение вокруг центра грани с "дышащим" масштабом
    float angle = 0.4 * t;
    float zoom = 1.0 + 0.3 * sin(0.7 * t);
    mat2 rotation = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
    vec2 rotatedUv = rotation * (baseUv - 0.5) * zoom + 0.5;

    // 3. Волновое искажение
    vec2 wavyUv = baseUv + 0.04 * vec2(sin(baseUv.y * 12.0 + 2.0 * t), cos(baseUv.x * 12.0 + 1.5 * t));

    vec3 color = texture(albedoMap, scrolledUv).rgb * 0.4
               + texture(albedoMap, rotatedUv).rgb * 0.35
               + texture(albedoMap, wavyUv).rgb * 0.25;

    vec3 tint = 0.65 + 0.35 * cos(t + vec3(0.0, 2.094, 4.189));
    return color * tint;
}

vec3 sampleAlbedo() {
    if ((sampling.flags & SAMPLING_ANIMATED) != 0u) {
        return sampleAnimatedAlbedo(uv, sampling.time);
    }
    return texture(albedoMap, uv).rgb;
}

vec3 blinnPhong(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, vec3 specularColor) {
    float diffuse = max(dot(N, L), 0.0);

    vec3 H = normalize(L + V); // половинный вектор
    float specular = 0.0;
    if (diffuse > 0.0) {
        specular = pow(max(dot(N, H), 0.0), object.specular.a);
    }

    return (albedo * diffuse + specularColor * specular) * radiance;
}

float inverseSquareAttenuation(float distanceSquared) {
    return 1.0 / max(distanceSquared, 0.0001);
}

void main() {
    vec3 N = normalize(worldNormal);
    vec3 V = normalize(scene.cameraPosition.xyz - worldPosition);

    vec3 albedo = sampleAlbedo() * object.albedoTint.rgb;
    vec3 specularColor = object.specular.rgb * texture(specularMap, uv).rgb;

    // Рассеянный (ambient) свет
    vec3 color = albedo * scene.ambient.rgb * scene.ambient.a;

    // Направленный источник
    {
        vec3 L = normalize(-scene.directionalDirection.xyz);
        vec3 radiance = scene.directionalColor.rgb * scene.directionalColor.a;
        color += blinnPhong(N, V, L, radiance, albedo, specularColor);
    }

    // Точечные источники
    for (uint i = 0; i < scene.pointLightCount; ++i) {
        PointLight light = pointLights[i];
        vec3 toLight = light.position - worldPosition;
        vec3 L = normalize(toLight);
        float attenuation = inverseSquareAttenuation(dot(toLight, toLight));
        color += blinnPhong(N, V, L, light.color * light.intensity * attenuation, albedo, specularColor);
    }

    // Прожекторы
    for (uint i = 0; i < scene.spotLightCount; ++i) {
        SpotLight light = spotLights[i];
        vec3 toLight = light.position - worldPosition;
        vec3 L = normalize(toLight);

        float theta = dot(-L, normalize(light.direction));
        float epsilon = max(light.innerCos - light.outerCos, 0.0001);
        float cone = clamp((theta - light.outerCos) / epsilon, 0.0, 1.0);
        if (cone <= 0.0) {
            continue;
        }

        float attenuation = inverseSquareAttenuation(dot(toLight, toLight));
        vec3 radiance = light.color * light.intensity * attenuation * cone;
        color += blinnPhong(N, V, L, radiance, albedo, specularColor);
    }

    color += texture(emissiveMap, uv).rgb * object.emissiveStrength;

    outColor = vec4(color, 1.0);
}
