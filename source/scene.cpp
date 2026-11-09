#include "scene.hpp"

#include <cmath>

namespace scene {

namespace {

constexpr float light_orbit_speed = 0.8f;  // радиан в секунду
constexpr float object_spin_speed = 40.0f; // градусов в секунду

math::Vec3 rotateAroundY(math::Vec3 v, float angle) {
	const float c = std::cos(angle), s = std::sin(angle);
	return { c * v.x + s * v.z, v.y, -s * v.x + c * v.z };
}

math::Mat4 objectRotation(const Object& object, float time) {
	math::Vec3 degrees = object.rotation;
	if (object.spin) {
		degrees.y += time * object_spin_speed;
	}
	return math::rotationXYZ(degrees);
}

} // namespace

State createDefaultState() {
	State state;

	state.point_lights = {
		{ .position = { 2.0f, 1.0f, 1.0f }, .color = { 1.0f, 0.55f, 0.2f }, .intensity = 4.0f, .orbit = true },
		{ .position = { -2.5f, 1.5f, -1.0f }, .color = { 0.3f, 0.5f, 1.0f }, .intensity = 5.0f },
	};

	state.spot_lights = {
		{
			.position = { 0.0f, 3.5f, 1.5f },
			.direction = { 0.0f, -1.0f, -0.4f },
			.color = { 0.8f, 1.0f, 0.8f },
			.intensity = 14.0f,
		},
	};

	state.objects = {{
		{
			.name = "Floor",
			.position = { 0.0f, -0.55f, 0.0f },
			.scale = { 14.0f, 0.1f, 14.0f },
			.texture_set = tiles_textures,
			.uv_scale = { 7.0f, 7.0f },
			.material = { .shininess = 64.0f },
		},
		{
			.name = "Back wall",
			.position = { 0.0f, 2.0f, -4.0f },
			.scale = { 14.0f, 5.0f, 0.1f },
			.texture_set = bricks_textures,
			.uv_scale = { 7.0f, 2.5f },
			.material = { .shininess = 16.0f },
		},
		{
			.name = "Lava cube",
			.position = { -1.4f, 0.0f, 0.0f },
			.rotation = { 0.0f, 25.0f, 0.0f },
			.texture_set = lava_textures,
			.material = { .specular = { 1.0f, 0.8f, 0.6f }, .shininess = 32.0f, .emissive_strength = 1.5f },
		},
		{
			.name = "Sci-fi pillar",
			.position = { 1.4f, 0.25f, -0.3f },
			.rotation = { 0.0f, -30.0f, 0.0f },
			.scale = { 0.8f, 1.5f, 0.8f },
			.texture_set = scifi_textures,
			.uv_scale = { 1.0f, 2.0f },
			.material = { .shininess = 128.0f },
			.spin = true,
		},
		{
			.name = "Magic cube",
			.position = { 0.0f, -0.1f, 1.3f },
			.rotation = { 0.0f, 15.0f, 0.0f },
			.scale = { 0.8f, 0.8f, 0.8f },
			.texture_set = checker_textures,
			.animated_sampling = true,
		},
	}};

	return state;
}

math::Vec3 pointLightPosition(const PointLight& light, float time) {
	return light.orbit ? rotateAroundY(light.position, time * light_orbit_speed) : light.position;
}

SpotTransform spotLightTransform(const SpotLight& light, const Camera& camera) {
	if (light.attach_to_camera) {
		return { camera.position + math::Vec3{ 0.0f, -0.15f, 0.0f }, camera.forward() };
	}
	return { light.position, math::normalize(light.direction) };
}

math::Mat4 modelMatrix(const Object& object, float time) {
	return math::translation(object.position) * objectRotation(object, time) * math::scale(object.scale);
}

math::Mat4 normalMatrix(const Object& object, float time) {
	const math::Vec3 inverse_scale = { 1.0f / object.scale.x, 1.0f / object.scale.y, 1.0f / object.scale.z };
	return objectRotation(object, time) * math::scale(inverse_scale);
}

} // namespace scene
