#include "ui.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

#include <imgui.h>

namespace ui {

namespace {

using math::Vec3;

void drawCameraSection(scene::State& state) {
	if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}
	Camera& camera = state.camera;
	ImGui::DragFloat3("Position", &camera.position.x, 0.05f);
	ImGui::DragFloat("Yaw", &camera.yaw, 0.5f);
	ImGui::SliderFloat("Pitch", &camera.pitch, -89.0f, 89.0f);
	ImGui::SliderFloat("FOV", &camera.fov, 10.0f, 120.0f);
	ImGui::SliderFloat("Move speed", &state.camera_speed, 0.5f, 10.0f);
	ImGui::TextDisabled("RMB + mouse: look, WASD/QE: move, Shift: faster");
}

void drawGlobalLightSection(scene::State& state) {
	if (!ImGui::CollapsingHeader("Ambient & directional light", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}
	ImGui::PushID("ambient");
	ImGui::SeparatorText("Ambient");
	ImGui::ColorEdit3("Color", &state.ambient_color.x);
	ImGui::SliderFloat("Intensity", &state.ambient_intensity, 0.0f, 1.0f);
	ImGui::PopID();

	scene::DirectionalLight& light = state.directional_light;
	ImGui::PushID("directional");
	ImGui::SeparatorText("Directional");
	ImGui::DragFloat3("Direction", &light.direction.x, 0.01f, -1.0f, 1.0f);
	ImGui::ColorEdit3("Color", &light.color.x);
	ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 3.0f);
	ImGui::PopID();
}

template <typename Light, typename CreateFn, typename DrawFieldsFn>
void drawLightList(const char* header, const char* item_name, std::vector<Light>& lights,
                   CreateFn create_light, DrawFieldsFn draw_fields) {
	if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}
	ImGui::PushID(header);
	if (ImGui::Button("Add")) {
		lights.push_back(create_light());
	}

	std::size_t remove_index = lights.size();
	for (std::size_t i = 0; i < lights.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::TreeNode("light", "%s #%zu", item_name, i)) {
			draw_fields(lights[i]);
			if (ImGui::Button("Remove")) {
				remove_index = i;
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (remove_index < lights.size()) {
		lights.erase(lights.begin() + static_cast<std::ptrdiff_t>(remove_index));
	}
	ImGui::PopID();
}

void drawPointLightsSection(scene::State& state) {
	const Camera& camera = state.camera;
	drawLightList("Point lights", "Point light", state.point_lights,
		[&] {
			return scene::PointLight{ .position = camera.position + camera.forward() * 2.0f };
		},
		[](scene::PointLight& light) {
			ImGui::DragFloat3("Position", &light.position.x, 0.05f);
			ImGui::ColorEdit3("Color", &light.color.x);
			ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 30.0f);
			ImGui::Checkbox("Orbit around center", &light.orbit);
		});
}

void drawSpotLightsSection(scene::State& state) {
	const Camera& camera = state.camera;
	drawLightList("Spot lights", "Spot light", state.spot_lights,
		[&] {
			return scene::SpotLight{
				.position = camera.position + camera.forward(),
				.direction = camera.forward(),
			};
		},
		[](scene::SpotLight& light) {
			ImGui::Checkbox("Attach to camera (flashlight)", &light.attach_to_camera);
			ImGui::BeginDisabled(light.attach_to_camera);
			ImGui::DragFloat3("Position", &light.position.x, 0.05f);
			ImGui::DragFloat3("Direction", &light.direction.x, 0.01f, -1.0f, 1.0f);
			ImGui::EndDisabled();
			ImGui::ColorEdit3("Color", &light.color.x);
			ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 50.0f);
			ImGui::SliderFloat("Inner angle", &light.inner_angle, 1.0f, 89.0f, "%.1f deg");
			ImGui::SliderFloat("Outer angle", &light.outer_angle, 1.0f, 89.0f, "%.1f deg");
			light.outer_angle = std::max(light.outer_angle, light.inner_angle);
		});
}

void drawTextureSetCombo(int& texture_set) {
	if (!ImGui::BeginCombo("Texture", scene::texture_sets[static_cast<std::size_t>(texture_set)].name)) {
		return;
	}
	for (int i = 0; i < static_cast<int>(scene::texture_sets.size()); ++i) {
		const bool selected = i == texture_set;
		if (ImGui::Selectable(scene::texture_sets[static_cast<std::size_t>(i)].name, selected)) {
			texture_set = i;
		}
		if (selected) {
			ImGui::SetItemDefaultFocus();
		}
	}
	ImGui::EndCombo();
}

void drawObjectsSection(scene::State& state) {
	if (!ImGui::CollapsingHeader("Objects & materials", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}
	for (std::size_t i = 0; i < state.objects.size(); ++i) {
		scene::Object& object = state.objects[i];
		scene::Material& material = object.material;
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::TreeNode(object.name)) {
			ImGui::DragFloat3("Position", &object.position.x, 0.05f);
			ImGui::DragFloat3("Rotation", &object.rotation.x, 1.0f);
			ImGui::DragFloat3("Scale", &object.scale.x, 0.02f, 0.05f, 20.0f);
			ImGui::Checkbox("Spin", &object.spin);

			ImGui::SeparatorText("Texture");
			drawTextureSetCombo(object.texture_set);
			ImGui::DragFloat2("UV scale", &object.uv_scale.x, 0.05f, 0.1f, 20.0f);
			ImGui::Checkbox("Animated sampling", &object.animated_sampling);

			ImGui::SeparatorText("Material");
			ImGui::ColorEdit3("Albedo tint", &material.albedo_tint.x);
			ImGui::ColorEdit3("Specular color", &material.specular.x);
			ImGui::SliderFloat("Shininess", &material.shininess, 1.0f, 256.0f, "%.0f",
			                   ImGuiSliderFlags_Logarithmic);
			ImGui::SliderFloat("Emissive strength", &material.emissive_strength, 0.0f, 5.0f);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}

void drawVisualizationSection(scene::State& state) {
	if (!ImGui::CollapsingHeader("Visualization & animation", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}
	ImGui::Checkbox("Show light sources", &state.show_light_gizmos);
	ImGui::Checkbox("Show spot light cones", &state.show_spot_cones);
	ImGui::Checkbox("Animation", &state.animation_enabled);
}

} // namespace

void handleCameraInput(scene::State& state, float delta_time) {
	const ImGuiIO& io = ImGui::GetIO();
	Camera& camera = state.camera;

	if (!io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
		camera.yaw += io.MouseDelta.x * state.mouse_sensitivity;
		camera.pitch -= io.MouseDelta.y * state.mouse_sensitivity;
	}

	if (!io.WantCaptureKeyboard) {
		const Vec3 forward = camera.forward();
		const Vec3 right = camera.right();
		const Vec3 up = { 0.0f, 1.0f, 0.0f };
		Vec3 move;
		if (ImGui::IsKeyDown(ImGuiKey_W)) move = move + forward;
		if (ImGui::IsKeyDown(ImGuiKey_S)) move = move - forward;
		if (ImGui::IsKeyDown(ImGuiKey_D)) move = move + right;
		if (ImGui::IsKeyDown(ImGuiKey_A)) move = move - right;
		if (ImGui::IsKeyDown(ImGuiKey_E)) move = move + up;
		if (ImGui::IsKeyDown(ImGuiKey_Q)) move = move - up;

		if (math::length(move) > 0.0f) {
			const float speed = state.camera_speed * (io.KeyShift ? 3.0f : 1.0f);
			camera.position = camera.position + math::normalize(move) * (speed * delta_time);
		}
	}
	camera.clampAngles();
}

void drawControlPanel(scene::State& state) {
	ImGui::SetNextWindowSize({ 380.0f, 600.0f }, ImGuiCond_FirstUseEver);
	ImGui::Begin("Lab 3 - Texturing");
	ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
	drawCameraSection(state);
	drawGlobalLightSection(state);
	drawPointLightsSection(state);
	drawSpotLightsSection(state);
	drawObjectsSection(state);
	drawVisualizationSection(state);
	ImGui::End();
}

} // namespace ui
