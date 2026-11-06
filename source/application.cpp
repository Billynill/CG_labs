#include "application.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <iterator>
#include <vector>

#include "cube.hpp"
#include "gpu_types.hpp"
#include "math.hpp"
#include "scene.hpp"
#include "ui.hpp"
#include "vulkan_utils.hpp"

namespace {

using math::Vec3;
using vulkan_utils::Buffer;
using scene::object_count;

constexpr VkBufferUsageFlags storage_usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
constexpr VkShaderStageFlags vertex_and_fragment = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// Привязки набора дескрипторов сцены (set 0), должны совпадать с lit.frag.
constexpr std::uint32_t scene_data_binding = 0;
constexpr std::uint32_t point_lights_binding = 1;
constexpr std::uint32_t spot_lights_binding = 2;

scene::State state = scene::createDefaultState();
double previous_time = 0.0;

// --- Геометрия ---
cube::Mesh cube_mesh; // используется и для объектов сцены, и для визуализации источников
Buffer vertex_buffer;
Buffer index_buffer;
Buffer line_vertex_buffer; // каркасы конусов прожекторов, пересобираются каждый кадр
std::uint32_t line_vertex_count = 0;

// --- Данные для шейдеров ---
Buffer scene_uniform_buffer;
Buffer point_light_buffer;
Buffer spot_light_buffer;
std::array<Buffer, object_count> model_uniform_buffers;

// --- Дескрипторы ---
// set 0 (сцена): uniform с параметрами сцены + storage буферы источников;
// set 1 (модель): uniform с матрицей модели и материалом.
VkDescriptorSetLayout scene_set_layout = VK_NULL_HANDLE;
VkDescriptorSetLayout model_set_layout = VK_NULL_HANDLE;
VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
VkDescriptorSet scene_set = VK_NULL_HANDLE;
std::array<VkDescriptorSet, object_count> model_sets{};

// --- Пайплайны ---
VkPipelineLayout lit_pipeline_layout = VK_NULL_HANDLE;
VkPipelineLayout unlit_pipeline_layout = VK_NULL_HANDLE;
VkPipeline lit_pipeline = VK_NULL_HANDLE;        // объекты сцены с освещением
VkPipeline unlit_pipeline = VK_NULL_HANDLE;      // кубики-источники
VkPipeline unlit_line_pipeline = VK_NULL_HANDLE; // конусы прожекторов

// ===========================================================================
// Создание и удаление ресурсов
// ===========================================================================

bool createBuffers() {
	cube_mesh = cube::createMesh();
	const VkDeviceSize vertices_size = cube_mesh.vertices.size() * sizeof(cube::Vertex);
	const VkDeviceSize indices_size = cube_mesh.indices.size() * sizeof(std::uint32_t);

	if (!vulkan_utils::createBuffer(vertices_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer) ||
	    !vulkan_utils::createBuffer(indices_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, index_buffer)) {
		std::cerr << "Failed to create geometry buffers\n";
		return false;
	}
	vulkan_utils::writeBuffer(vertex_buffer, cube_mesh.vertices.data(), vertices_size);
	vulkan_utils::writeBuffer(index_buffer, cube_mesh.indices.data(), indices_size);

	// Начальная ёмкость - 16 источников каждого типа, дальше буферы растут сами.
	// Буфер нулевого размера создавать нельзя, поэтому ёмкость всегда > 0.
	constexpr VkDeviceSize initial_light_capacity = 16;
	constexpr VkDeviceSize initial_line_vertices = 64;
	if (!vulkan_utils::createBuffer(sizeof(gpu::SceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	                                scene_uniform_buffer) ||
	    !vulkan_utils::createBuffer(initial_light_capacity * sizeof(gpu::PointLight), storage_usage,
	                                point_light_buffer) ||
	    !vulkan_utils::createBuffer(initial_light_capacity * sizeof(gpu::SpotLight), storage_usage,
	                                spot_light_buffer) ||
	    !vulkan_utils::createBuffer(initial_line_vertices * sizeof(cube::Vertex),
	                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, line_vertex_buffer)) {
		std::cerr << "Failed to create scene buffers\n";
		return false;
	}

	for (Buffer& buffer : model_uniform_buffers) {
		if (!vulkan_utils::createBuffer(sizeof(gpu::ModelData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, buffer)) {
			std::cerr << "Failed to create model uniform buffer\n";
			return false;
		}
	}
	return true;
}

bool createDescriptors() {
	auto& context = graphics::internal::context;

	const VkDescriptorSetLayoutBinding scene_bindings[] = {
		{ .binding = scene_data_binding, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		  .descriptorCount = 1, .stageFlags = vertex_and_fragment },
		{ .binding = point_lights_binding, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		  .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT },
		{ .binding = spot_lights_binding, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		  .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT },
	};
	const VkDescriptorSetLayoutBinding model_bindings[] = {
		{ .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		  .descriptorCount = 1, .stageFlags = vertex_and_fragment },
	};
	scene_set_layout = vulkan_utils::createSetLayout(scene_bindings);
	model_set_layout = vulkan_utils::createSetLayout(model_bindings);
	if (scene_set_layout == VK_NULL_HANDLE || model_set_layout == VK_NULL_HANDLE) {
		return false;
	}

	const VkDescriptorPoolSize pool_sizes[] = {
		{ .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1 + object_count },
		{ .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 2 },
	};
	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1 + object_count,
		.poolSizeCount = static_cast<std::uint32_t>(std::size(pool_sizes)),
		.pPoolSizes = pool_sizes,
	};
	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor pool\n";
		return false;
	}

	VkDescriptorSetAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &scene_set_layout,
	};
	if (vkAllocateDescriptorSets(context.device, &allocate_info, &scene_set) != VK_SUCCESS) {
		std::cerr << "Failed to allocate scene descriptor set\n";
		return false;
	}
	vulkan_utils::writeBufferDescriptor(scene_set, scene_data_binding,
	                                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, scene_uniform_buffer);
	vulkan_utils::writeBufferDescriptor(scene_set, point_lights_binding,
	                                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, point_light_buffer);
	vulkan_utils::writeBufferDescriptor(scene_set, spot_lights_binding,
	                                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, spot_light_buffer);

	std::array<VkDescriptorSetLayout, object_count> model_layouts;
	model_layouts.fill(model_set_layout);
	allocate_info.descriptorSetCount = static_cast<std::uint32_t>(object_count);
	allocate_info.pSetLayouts = model_layouts.data();
	if (vkAllocateDescriptorSets(context.device, &allocate_info, model_sets.data()) != VK_SUCCESS) {
		std::cerr << "Failed to allocate model descriptor sets\n";
		return false;
	}
	for (std::size_t i = 0; i < object_count; ++i) {
		vulkan_utils::writeBufferDescriptor(model_sets[i], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		                                    model_uniform_buffers[i]);
	}
	return true;
}

bool createPipelineLayouts() {
	auto& context = graphics::internal::context;

	const VkDescriptorSetLayout lit_sets[] = { scene_set_layout, model_set_layout };
	const VkPipelineLayoutCreateInfo lit_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = static_cast<std::uint32_t>(std::size(lit_sets)),
		.pSetLayouts = lit_sets,
	};

	// Визуализации источников хватает push constants: матрица и цвет.
	const VkPushConstantRange push_range = {
		.stageFlags = vertex_and_fragment,
		.offset = 0,
		.size = sizeof(gpu::UnlitPushConstants),
	};
	const VkPipelineLayoutCreateInfo unlit_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push_range,
	};

	if (vkCreatePipelineLayout(context.device, &lit_info, nullptr, &lit_pipeline_layout) != VK_SUCCESS ||
	    vkCreatePipelineLayout(context.device, &unlit_info, nullptr, &unlit_pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create pipeline layouts\n";
		return false;
	}
	return true;
}

bool createPipelines() {
	auto& context = graphics::internal::context;
	const VkShaderModule lit_vertex = vulkan_utils::createShaderModule("shaders/lit.vert.spv");
	const VkShaderModule lit_fragment = vulkan_utils::createShaderModule("shaders/lit.frag.spv");
	const VkShaderModule unlit_vertex = vulkan_utils::createShaderModule("shaders/unlit.vert.spv");
	const VkShaderModule unlit_fragment = vulkan_utils::createShaderModule("shaders/unlit.frag.spv");

	// Освещённым объектам нужны позиция и нормаль, визуализации - только позиция.
	const VkVertexInputAttributeDescription attributes[] = {
		{ .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
		  .offset = offsetof(cube::Vertex, position) },
		{ .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
		  .offset = offsetof(cube::Vertex, normal) },
	};
	const std::span<const VkVertexInputAttributeDescription> position_and_normal = attributes;
	const auto position_only = position_and_normal.first(1);
	constexpr auto stride = static_cast<std::uint32_t>(sizeof(cube::Vertex));

	bool success = lit_vertex && lit_fragment && unlit_vertex && unlit_fragment;
	if (success) {
		lit_pipeline = vulkan_utils::createPipeline({
			.vertex_shader = lit_vertex,
			.fragment_shader = lit_fragment,
			.layout = lit_pipeline_layout,
			.vertex_stride = stride,
			.vertex_attributes = position_and_normal,
		});
		unlit_pipeline = vulkan_utils::createPipeline({
			.vertex_shader = unlit_vertex,
			.fragment_shader = unlit_fragment,
			.layout = unlit_pipeline_layout,
			.vertex_stride = stride,
			.vertex_attributes = position_only,
		});
		unlit_line_pipeline = vulkan_utils::createPipeline({
			.vertex_shader = unlit_vertex,
			.fragment_shader = unlit_fragment,
			.layout = unlit_pipeline_layout,
			.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
			.vertex_stride = stride,
			.vertex_attributes = position_only,
		});
		success = lit_pipeline && unlit_pipeline && unlit_line_pipeline;
	}

	// После создания пайплайнов шейдерные модули больше не нужны.
	// vkDestroyShaderModule безопасно вызывать и для VK_NULL_HANDLE.
	for (VkShaderModule module : { lit_vertex, lit_fragment, unlit_vertex, unlit_fragment }) {
		vkDestroyShaderModule(context.device, module, nullptr);
	}
	return success;
}

void destroyResources() {
	auto& context = graphics::internal::context;
	// Все vkDestroy* допускают VK_NULL_HANDLE, поэтому функция безопасна
	// и при частично выполненной инициализации.
	vkDestroyPipeline(context.device, unlit_line_pipeline, nullptr);
	vkDestroyPipeline(context.device, unlit_pipeline, nullptr);
	vkDestroyPipeline(context.device, lit_pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, unlit_pipeline_layout, nullptr);
	vkDestroyPipelineLayout(context.device, lit_pipeline_layout, nullptr);
	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr); // освобождает и сами наборы
	vkDestroyDescriptorSetLayout(context.device, model_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, scene_set_layout, nullptr);
	unlit_line_pipeline = unlit_pipeline = lit_pipeline = VK_NULL_HANDLE;
	unlit_pipeline_layout = lit_pipeline_layout = VK_NULL_HANDLE;
	descriptor_pool = VK_NULL_HANDLE;
	model_set_layout = scene_set_layout = VK_NULL_HANDLE;

	for (Buffer& buffer : model_uniform_buffers) {
		vulkan_utils::destroyBuffer(buffer);
	}
	vulkan_utils::destroyBuffer(spot_light_buffer);
	vulkan_utils::destroyBuffer(point_light_buffer);
	vulkan_utils::destroyBuffer(scene_uniform_buffer);
	vulkan_utils::destroyBuffer(line_vertex_buffer);
	vulkan_utils::destroyBuffer(index_buffer);
	vulkan_utils::destroyBuffer(vertex_buffer);
}

// ===========================================================================
// Подготовка данных кадра (до записи команд)
// ===========================================================================

float aspectRatio() {
	const VkExtent2D extent = graphics::internal::context.swapchain_extent;
	return static_cast<float>(std::max(extent.width, 1u)) / static_cast<float>(std::max(extent.height, 1u));
}

// Копирует источники в storage буфер, при необходимости увеличивая его.
// Возвращает, сколько источников реально записано (меньше, если буфер не удалось увеличить).
template <typename GpuLight>
std::uint32_t uploadLights(const std::vector<GpuLight>& lights, Buffer& buffer, std::uint32_t binding) {
	const VkDeviceSize required = lights.size() * sizeof(GpuLight);
	if (vulkan_utils::growBufferIfNeeded(buffer, required, storage_usage)) {
		vulkan_utils::writeBufferDescriptor(scene_set, binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffer);
	}
	const std::size_t count = std::min<std::size_t>(lights.size(), buffer.size / sizeof(GpuLight));
	vulkan_utils::writeBuffer(buffer, lights.data(), count * sizeof(GpuLight));
	return static_cast<std::uint32_t>(count);
}

void uploadSceneData(const math::Mat4& view_projection) {
	std::vector<gpu::PointLight> point_lights;
	point_lights.reserve(state.point_lights.size());
	for (const auto& light : state.point_lights) {
		point_lights.push_back({
			.position = scene::pointLightPosition(light, state.animation_time),
			.intensity = light.intensity,
			.color = light.color,
		});
	}

	std::vector<gpu::SpotLight> spot_lights;
	spot_lights.reserve(state.spot_lights.size());
	for (const auto& light : state.spot_lights) {
		const scene::SpotTransform transform = scene::spotLightTransform(light, state.camera);
		spot_lights.push_back({
			.position = transform.position,
			.intensity = light.intensity,
			.direction = transform.direction,
			// В шейдере сравниваем косинусы - так не нужен acos для каждого пикселя.
			.inner_cos = std::cos(math::radians(light.inner_angle)),
			.color = light.color,
			.outer_cos = std::cos(math::radians(light.outer_angle)),
		});
	}

	const scene::DirectionalLight& sun = state.directional_light;
	gpu::SceneData data{};
	data.view_projection = view_projection;
	data.camera_position = math::toVec4(state.camera.position, 1.0f);
	data.ambient = math::toVec4(state.ambient_color, state.ambient_intensity);
	data.directional_direction = math::toVec4(math::normalize(sun.direction), 0.0f);
	data.directional_color = math::toVec4(sun.color, sun.intensity);
	data.point_light_count = uploadLights(point_lights, point_light_buffer, point_lights_binding);
	data.spot_light_count = uploadLights(spot_lights, spot_light_buffer, spot_lights_binding);
	vulkan_utils::writeBuffer(scene_uniform_buffer, &data, sizeof(data));

	for (std::size_t i = 0; i < object_count; ++i) {
		const scene::Object& object = state.objects[i];
		const gpu::ModelData model = {
			.model = scene::modelMatrix(object, state.animation_time),
			.normal_matrix = scene::normalMatrix(object, state.animation_time),
			.albedo = math::toVec4(object.material.albedo, 1.0f),
			.specular = math::toVec4(object.material.specular, object.material.shininess),
		};
		vulkan_utils::writeBuffer(model_uniform_buffers[i], &model, sizeof(model));
	}
}

// Каркас конуса прожектора: окружность на конце + образующие + ось.
std::vector<cube::Vertex> buildSpotConeLines() {
	constexpr int circle_segments = 32;
	constexpr int side_lines = 8;
	constexpr float cone_length = 1.5f;

	std::vector<cube::Vertex> lines;
	for (const auto& light : state.spot_lights) {
		if (light.attach_to_camera) {
			continue; // конус фонарика выходит из глаз камеры, рисовать его бессмысленно
		}
		const scene::SpotTransform transform = scene::spotLightTransform(light, state.camera);
		const Vec3 apex = transform.position;
		const Vec3 axis = transform.direction;
		const Vec3 side = math::anyPerpendicular(axis);
		const Vec3 up = math::cross(side, axis);
		const Vec3 base_center = apex + axis * cone_length;
		const float radius = cone_length * std::tan(math::radians(light.outer_angle));

		auto circlePoint = [&](int index) {
			const float angle = 2.0f * math::pi * static_cast<float>(index) / circle_segments;
			return base_center + side * (radius * std::cos(angle)) + up * (radius * std::sin(angle));
		};

		for (int i = 0; i < circle_segments; ++i) {
			lines.push_back({ circlePoint(i) });
			lines.push_back({ circlePoint(i + 1) });
		}
		for (int i = 0; i < side_lines; ++i) {
			lines.push_back({ apex });
			lines.push_back({ circlePoint(i * circle_segments / side_lines) });
		}
		lines.push_back({ apex });
		lines.push_back({ base_center });
	}
	return lines;
}

void uploadSpotConeLines() {
	const std::vector<cube::Vertex> lines = state.show_spot_cones ? buildSpotConeLines()
	                                                              : std::vector<cube::Vertex>{};
	const VkDeviceSize required = lines.size() * sizeof(cube::Vertex);
	vulkan_utils::growBufferIfNeeded(line_vertex_buffer, required, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

	const VkDeviceSize written = std::min(required, line_vertex_buffer.size);
	vulkan_utils::writeBuffer(line_vertex_buffer, lines.data(), written);
	line_vertex_count = static_cast<std::uint32_t>(written / sizeof(cube::Vertex));
}

// ===========================================================================
// Запись команд
// ===========================================================================

void beginRenderPass(VkCommandBuffer cmd, VkFramebuffer framebuffer) {
	auto& context = graphics::internal::context;

	const VkClearValue clear_values[] = {
		{ .color = {{ 0.02f, 0.02f, 0.03f, 1.0f }} },
		{ .depthStencil = { 1.0f, 0 } },
	};
	const VkRenderPassBeginInfo render_pass_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = framebuffer,
		.renderArea = { .offset = { 0, 0 }, .extent = context.swapchain_extent },
		.clearValueCount = static_cast<std::uint32_t>(std::size(clear_values)),
		.pClearValues = clear_values,
	};
	vkCmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

	const VkViewport viewport = {
		.x = 0.0f, .y = 0.0f,
		.width = static_cast<float>(context.swapchain_extent.width),
		.height = static_cast<float>(context.swapchain_extent.height),
		.minDepth = 0.0f, .maxDepth = 1.0f,
	};
	const VkRect2D scissor = { .offset = { 0, 0 }, .extent = context.swapchain_extent };
	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void bindCubeGeometry(VkCommandBuffer cmd) {
	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer.handle, &offset);
	vkCmdBindIndexBuffer(cmd, index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);
}

void drawCube(VkCommandBuffer cmd) {
	vkCmdDrawIndexed(cmd, static_cast<std::uint32_t>(cube_mesh.indices.size()), 1, 0, 0, 0);
}

void pushUnlitConstants(VkCommandBuffer cmd, const math::Mat4& mvp, Vec3 color) {
	const gpu::UnlitPushConstants constants = { .mvp = mvp, .color = math::toVec4(color, 1.0f) };
	vkCmdPushConstants(cmd, unlit_pipeline_layout, vertex_and_fragment, 0, sizeof(constants), &constants);
}

void drawLitObjects(VkCommandBuffer cmd) {
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lit_pipeline);
	// Набор сцены привязывается один раз, а набор модели - перед каждым объектом.
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lit_pipeline_layout,
	                        0, 1, &scene_set, 0, nullptr);
	for (VkDescriptorSet model_set : model_sets) {
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lit_pipeline_layout,
		                        1, 1, &model_set, 0, nullptr);
		drawCube(cmd);
	}
}

void drawLightGizmos(VkCommandBuffer cmd, const math::Mat4& view_projection) {
	const math::Mat4 gizmo_scale = math::scale({ 0.15f, 0.15f, 0.15f });
	auto drawGizmo = [&](Vec3 position, Vec3 color) {
		pushUnlitConstants(cmd, view_projection * math::translation(position) * gizmo_scale, color);
		drawCube(cmd);
	};

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, unlit_pipeline);
	for (const auto& light : state.point_lights) {
		drawGizmo(scene::pointLightPosition(light, state.animation_time), light.color);
	}
	for (const auto& light : state.spot_lights) {
		if (!light.attach_to_camera) { // куб внутри камеры закрыл бы весь экран
			drawGizmo(light.position, light.color);
		}
	}
}

void drawSpotCones(VkCommandBuffer cmd, const math::Mat4& view_projection) {
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, unlit_line_pipeline);
	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &line_vertex_buffer.handle, &offset);
	pushUnlitConstants(cmd, view_projection, { 1.0f, 1.0f, 1.0f });
	vkCmdDraw(cmd, line_vertex_count, 1, 0, 0);
}

} // namespace

namespace application {

bool initialize() {
	if (!createBuffers() || !createDescriptors() || !createPipelineLayouts() || !createPipelines()) {
		destroyResources();
		return false;
	}
	return true;
}

void shutdown() {
	vkDeviceWaitIdle(graphics::internal::context.device);
	destroyResources();
}

void update(double time) {
	if (previous_time == 0.0) {
		previous_time = time;
	}
	// Ограничиваем шаг, чтобы после паузы (например, перетаскивания окна) ничего не "прыгало".
	const float delta_time = static_cast<float>(std::min(time - previous_time, 0.1));
	previous_time = time;
	if (state.animation_enabled) {
		state.animation_time += delta_time;
	}

	ui::handleCameraInput(state, delta_time);
	ui::drawControlPanel(state);
}

void render(const graphics::internal::FrameData& fd) {
	// prepare() уже дождался окончания предыдущего кадра, поэтому буферы
	// можно перезаписывать. Все данные готовим до начала записи команд.
	const math::Mat4 view_projection = state.camera.projection(aspectRatio()) * state.camera.view();
	uploadSceneData(view_projection);
	uploadSpotConeLines();

	const VkCommandBuffer cmd = fd.command_buffer;
	vkResetCommandBuffer(cmd, 0);
	const VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(cmd, &begin_info);
	beginRenderPass(cmd, fd.framebuffer);

	bindCubeGeometry(cmd);
	drawLitObjects(cmd);
	if (state.show_light_gizmos) {
		drawLightGizmos(cmd, view_projection);
	}
	if (line_vertex_count > 0) {
		drawSpotCones(cmd, view_projection); // перепривязывает vertex buffer, поэтому последним
	}

	vkCmdEndRenderPass(cmd);
	vkEndCommandBuffer(cmd);
}

} // namespace application
