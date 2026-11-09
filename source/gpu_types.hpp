#pragma once

#include <cstdint>

#include "math.hpp"

namespace gpu {

struct SceneData {
	math::Mat4 view_projection;
	math::Vec4 camera_position;
	math::Vec4 ambient;               // rgb - цвет, w - интенсивность
	math::Vec4 directional_direction; // xyz - направление
	math::Vec4 directional_color;     // rgb - цвет, w - интенсивность
	std::uint32_t point_light_count;
	std::uint32_t spot_light_count;
	std::uint32_t _padding[2];
};
static_assert(sizeof(SceneData) == 144);

struct ModelData {
	math::Mat4 model;
	math::Mat4 normal_matrix;
	math::Vec4 albedo_tint; // rgb - множитель для текстуры альбедо
	math::Vec4 specular;    // rgb - цвет блика, w - коэффициент блеска
	math::Vec2 uv_scale;
	float emissive_strength;
	float _padding;
};
static_assert(sizeof(ModelData) == 176);

struct SamplingPushConstants {
	std::uint32_t flags;
	float time;
};

constexpr std::uint32_t sampling_flag_animated = 1u << 0;

struct PointLight {
	math::Vec3 position;
	float intensity;
	math::Vec3 color;
	float _padding;
};
static_assert(sizeof(PointLight) == 32);

struct SpotLight {
	math::Vec3 position;
	float intensity;
	math::Vec3 direction;
	float inner_cos;
	math::Vec3 color;
	float outer_cos;
};
static_assert(sizeof(SpotLight) == 48);

struct UnlitPushConstants {
	math::Mat4 mvp;
	math::Vec4 color;
};
static_assert(sizeof(UnlitPushConstants) <= 128, "Vulkan гарантирует только 128 байт push constants");

} // namespace gpu
