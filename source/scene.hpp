#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "camera.hpp"
#include "math.hpp"

namespace scene {

struct TextureSetFiles {
	const char* name;
	const char* albedo;
	const char* specular;
	const char* emissive;
};

inline constexpr std::array<TextureSetFiles, 5> texture_sets = {{
	{ "Bricks", "textures/bricks_albedo.png", "textures/bricks_specular.png", nullptr },
	{ "Tiles", "textures/tiles_albedo.png", "textures/tiles_specular.png", nullptr },
	{ "Lava rock", "textures/lava_albedo.png", "textures/lava_specular.png", "textures/lava_emissive.png" },
	{ "Sci-fi panel", "textures/scifi_albedo.png", "textures/scifi_specular.png", "textures/scifi_emissive.png" },
	{ "Checker", "textures/checker_albedo.png", nullptr, nullptr },
}};

enum TextureSetIndex : int {
	bricks_textures = 0,
	tiles_textures,
	lava_textures,
	scifi_textures,
	checker_textures,
};

struct Material {
	math::Vec3 albedo_tint = { 1.0f, 1.0f, 1.0f };
	math::Vec3 specular = { 1.0f, 1.0f, 1.0f };
	float shininess = 32.0f;
	float emissive_strength = 1.0f;
};

struct Object {
	const char* name = "Object";
	math::Vec3 position;
	math::Vec3 rotation; // градусы
	math::Vec3 scale = { 1.0f, 1.0f, 1.0f };
	int texture_set = bricks_textures;
	math::Vec2 uv_scale = { 1.0f, 1.0f };
	Material material;
	bool spin = false;
	bool animated_sampling = false;
};

struct DirectionalLight {
	math::Vec3 direction = { -0.4f, -1.0f, -0.3f };
	math::Vec3 color = { 1.0f, 0.95f, 0.85f };
	float intensity = 0.15f;
};

struct PointLight {
	math::Vec3 position;
	math::Vec3 color = { 1.0f, 1.0f, 1.0f };
	float intensity = 3.0f;
	bool orbit = false;
};

struct SpotLight {
	math::Vec3 position;
	math::Vec3 direction = { 0.0f, -1.0f, 0.0f };
	math::Vec3 color = { 1.0f, 1.0f, 1.0f };
	float intensity = 8.0f;
	float inner_angle = 15.0f;
	float outer_angle = 25.0f;
	bool attach_to_camera = false;
};

inline constexpr std::size_t object_count = 5;

struct State {
	Camera camera;
	float camera_speed = 3.0f;       // единиц в секунду
	float mouse_sensitivity = 0.15f; // градусов на пиксель

	math::Vec3 ambient_color = { 0.6f, 0.7f, 1.0f };
	float ambient_intensity = 0.05f;
	DirectionalLight directional_light;
	std::vector<PointLight> point_lights;
	std::vector<SpotLight> spot_lights;
	std::array<Object, object_count> objects;

	bool animation_enabled = true;
	float animation_time = 0.0f;

	bool show_light_gizmos = true;
	bool show_spot_cones = true;
};

State createDefaultState();

math::Vec3 pointLightPosition(const PointLight& light, float time);

struct SpotTransform {
	math::Vec3 position;
	math::Vec3 direction;
};
SpotTransform spotLightTransform(const SpotLight& light, const Camera& camera);

math::Mat4 modelMatrix(const Object& object, float time);
math::Mat4 normalMatrix(const Object& object, float time);

} // namespace scene
