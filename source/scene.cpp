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
			.material = { .albedo = { 0.55f, 0.55f, 0.6f }, .specular = { 0.2f, 0.2f, 0.2f }, .shininess = 8.0f },
		},
		{
			.name = "Back wall",
			.position = { 0.0f, 2.0f, -4.0f },
			.scale = { 14.0f, 5.0f, 0.1f },
			.material = { .albedo = { 0.6f, 0.5f, 0.45f }, .specular = { 0.1f, 0.1f, 0.1f }, .shininess = 4.0f },
		},
		{
			.name = "Red cube",
			.position = { -1.2f, 0.0f, 0.0f },
			.rotation = { 0.0f, 25.0f, 0.0f },
			.material = { .albedo = { 0.85f, 0.2f, 0.2f }, .specular = { 1.0f, 1.0f, 1.0f }, .shininess = 64.0f },
		},
		{
			.name = "Gold cube",
			.position = { 1.2f, 0.25f, 0.0f },
			.rotation = { 20.0f, -30.0f, 10.0f },
			.scale = { 0.8f, 1.5f, 0.8f },
			.material = { .albedo = { 0.9f, 0.7f, 0.25f }, .specular = { 1.0f, 0.85f, 0.5f }, .shininess = 128.0f },
			.spin = true,
		},
	}};

	return state;
}

math::Vec3 pointLightPosition(const PointLight& light, float time) {
	return light.orbit ? rotateAroundY(light.position, time * light_orbit_speed) : light.position;
}

SpotTransform spotLightTransform(const SpotLight& light, const Camera& camera) {
	if (light.attach_to_camera) {
		// Чуть ниже камеры, как фонарик в руке.
		return { camera.position + math::Vec3{ 0.0f, -0.15f, 0.0f }, camera.forward() };
	}
	return { light.position, math::normalize(light.direction) };
}

math::Mat4 modelMatrix(const Object& object, float time) {
	return math::translation(object.position) * objectRotation(object, time) * math::scale(object.scale);
}

// Для M = T * R * S матрица нормалей (M^-1)^T равна R * S^-1:
// перенос на нормали не влияет, а поворот ортогонален. Это дешевле
// и понятнее, чем считать полную обратную матрицу.
math::Mat4 normalMatrix(const Object& object, float time) {
	const math::Vec3 inverse_scale = { 1.0f / object.scale.x, 1.0f / object.scale.y, 1.0f / object.scale.z };
	return objectRotation(object, time) * math::scale(inverse_scale);
}

} // namespace scene
