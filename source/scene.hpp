#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "camera.hpp"
#include "math.hpp"

// Описание сцены в удобном для редактирования виде (то, что меняется в ImGui).
// Здесь нет ничего про Vulkan - только данные и расчёт трансформаций.
namespace scene {

struct Material {
	math::Vec3 albedo = { 0.8f, 0.8f, 0.8f };
	math::Vec3 specular = { 1.0f, 1.0f, 1.0f };
	float shininess = 32.0f;
};

struct Object {
	const char* name = "Object";
	math::Vec3 position;
	math::Vec3 rotation; // градусы
	math::Vec3 scale = { 1.0f, 1.0f, 1.0f };
	Material material;
	bool spin = false; // вращать вокруг оси Y со временем
};

struct DirectionalLight {
	math::Vec3 direction = { -0.4f, -1.0f, -0.3f };
	math::Vec3 color = { 1.0f, 0.95f, 0.85f };
	float intensity = 0.25f;
};

struct PointLight {
	math::Vec3 position;
	math::Vec3 color = { 1.0f, 1.0f, 1.0f };
	float intensity = 3.0f;
	bool orbit = false; // вращать вокруг вертикальной оси сцены
};

struct SpotLight {
	math::Vec3 position;
	math::Vec3 direction = { 0.0f, -1.0f, 0.0f };
	math::Vec3 color = { 1.0f, 1.0f, 1.0f };
	float intensity = 8.0f;
	float inner_angle = 15.0f; // градусы, полный свет внутри этого угла
	float outer_angle = 25.0f; // градусы, за этим углом света нет
	bool attach_to_camera = false; // режим "фонарика"
};

inline constexpr std::size_t object_count = 4;

struct State {
	Camera camera;
	float camera_speed = 3.0f;       // единиц в секунду
	float mouse_sensitivity = 0.15f; // градусов на пиксель

	math::Vec3 ambient_color = { 0.6f, 0.7f, 1.0f };
	float ambient_intensity = 0.08f;
	DirectionalLight directional_light;
	std::vector<PointLight> point_lights;
	std::vector<SpotLight> spot_lights;
	std::array<Object, object_count> objects;

	bool animation_enabled = true;
	float animation_time = 0.0f; // секунды, идёт только при включённой анимации

	bool show_light_gizmos = true;
	bool show_spot_cones = true;
};

// Начальная сцена: пол, стена, два куба и несколько источников.
State createDefaultState();

// Итоговая позиция точечного источника с учётом анимации.
math::Vec3 pointLightPosition(const PointLight& light, float time);

// Позиция и направление прожектора с учётом режима "фонарик".
struct SpotTransform {
	math::Vec3 position;
	math::Vec3 direction;
};
SpotTransform spotLightTransform(const SpotLight& light, const Camera& camera);

math::Mat4 modelMatrix(const Object& object, float time);
math::Mat4 normalMatrix(const Object& object, float time);

} // namespace scene
