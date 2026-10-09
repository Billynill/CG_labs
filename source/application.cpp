#include "application.hpp"
#include "cube.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {

constexpr float pi = 3.1415926535f;

struct Mat4 {
	float value[16]{};
};

struct alignas(16) UniformData {
	Mat4 mvp;
	float base_color[4];
};

VkShaderModule vertex_shader = VK_NULL_HANDLE;
VkShaderModule fragment_shader = VK_NULL_HANDLE;
VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
VkPipeline graphics_pipeline = VK_NULL_HANDLE;

VkBuffer vertex_buffer = VK_NULL_HANDLE;
VkBuffer index_buffer = VK_NULL_HANDLE;
VkBuffer uniform_buffer = VK_NULL_HANDLE;
VmaAllocation vertex_allocation = VK_NULL_HANDLE;
VmaAllocation index_allocation = VK_NULL_HANDLE;
VmaAllocation uniform_allocation = VK_NULL_HANDLE;
void* uniform_mapped = nullptr;

//пункт 1, проекция 0-преспектива, 1- ортографическая
int projection_mode = 0;
float object_position[3] = { 0.0f, 0.0f, 0.0f };
float object_rotation[3] = { 20.0f, 30.0f, 0.0f };
float object_scale[3] = { 1.0f, 1.0f, 1.0f };
float base_color[4] = { 1.0f, 0.8f, 0.8f, 1.0f };

//пункт 3 анимации
bool animation_playing = true;
float animation_time = 0.0f;
float animation_speed = 1.0f;
float trajectory_radius = 0.65f;
float trajectory_height = 0.25f;
double previous_time = 0.0;

Mat4 identityMatrix() {
	Mat4 result{};
	result.value[0] = 1.0f;
	result.value[5] = 1.0f;
	result.value[10] = 1.0f;
	result.value[15] = 1.0f;
	return result;
}

Mat4 multiply(const Mat4& left, const Mat4& right) {
	Mat4 result{};
	for (int column = 0; column < 4; ++column) {
		for (int row = 0; row < 4; ++row) {
			for (int k = 0; k < 4; ++k) {
				result.value[column * 4 + row] +=
					left.value[k * 4 + row] * right.value[column * 4 + k];
			}
		}
	}
	return result;
}

Mat4 translationMatrix(float x, float y, float z) {
	Mat4 result = identityMatrix();
	result.value[12] = x;
	result.value[13] = y;
	result.value[14] = z;
	return result;
}

Mat4 scaleMatrix(float x, float y, float z) {
	Mat4 result = identityMatrix();
	result.value[0] = x;
	result.value[5] = y;
	result.value[10] = z;
	return result;
}

Mat4 rotationXMatrix(float angle) {
	Mat4 result = identityMatrix();
	const float c = std::cos(angle);
	const float s = std::sin(angle);
	result.value[5] = c;
	result.value[6] = s;
	result.value[9] = -s;
	result.value[10] = c;
	return result;
}

Mat4 rotationYMatrix(float angle) {
	Mat4 result = identityMatrix();
	const float c = std::cos(angle);
	const float s = std::sin(angle);
	result.value[0] = c;
	result.value[2] = -s;
	result.value[8] = s;
	result.value[10] = c;
	return result;
}

Mat4 rotationZMatrix(float angle) {
	Mat4 result = identityMatrix();
	const float c = std::cos(angle);
	const float s = std::sin(angle);
	result.value[0] = c;
	result.value[1] = s;
	result.value[4] = -s;
	result.value[5] = c;
	return result;
}
//aspect соотношение высоты и ширины
Mat4 perspectiveMatrix(float fov, float aspect, float near_plane, float far_plane) { //fov угол обзора,
	Mat4 result{};
	const float f = 1.0f / std::tan(fov * 0.5f);
	result.value[0] = f / aspect;
	result.value[5] = -f;
	result.value[10] = far_plane / (near_plane - far_plane);
	result.value[11] = -1.0f;
	result.value[14] = (near_plane * far_plane) / (near_plane - far_plane);
	return result;
}

Mat4 orthographicMatrix(float aspect, float near_plane, float far_plane) {
	const float half_height = 1.4f;
	const float half_width = half_height * aspect;
	Mat4 result = identityMatrix();
	result.value[0] = 1.0f / half_width;
	result.value[5] = -1.0f / half_height;
	result.value[10] = 1.0f / (near_plane - far_plane);
	result.value[14] = near_plane / (near_plane - far_plane);
	return result;
}

std::vector<std::uint32_t> readShaderCode(const char* filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open()) {
		std::cerr << "Failed to open shader: " << filename << '\n';
		return {};
	}
	const std::size_t size = static_cast<std::size_t>(file.tellg());
	if (size == 0 || size % sizeof(std::uint32_t) != 0) {
		std::cerr << "Invalid SPIR-V file: " << filename << '\n';
		return {};
	}
	std::vector<std::uint32_t> code(size / sizeof(std::uint32_t));
	file.seekg(0);
	file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(size));
	return file ? code : std::vector<std::uint32_t>{};
}

VkShaderModule createShaderModule(VkDevice device, const char* filename) {
	const std::vector<std::uint32_t> code = readShaderCode(filename);
	if (code.empty()) {
		return VK_NULL_HANDLE;
	}
	const VkShaderModuleCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = code.size() * sizeof(std::uint32_t),
		.pCode = code.data(),
	};
	VkShaderModule module = VK_NULL_HANDLE;
	if (vkCreateShaderModule(device, &create_info, nullptr, &module) != VK_SUCCESS) {
		std::cerr << "Failed to create shader module: " << filename << '\n';
		return VK_NULL_HANDLE;
	}
	return module;
}

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data,
	VkBuffer& buffer, VmaAllocation& allocation, void** mapped = nullptr) {
	auto& context = graphics::internal::context;
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	const VmaAllocationCreateInfo allocation_info = {
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
			VMA_ALLOCATION_CREATE_MAPPED_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};
	VmaAllocationInfo result_info{};
	if (vmaCreateBuffer(context.allocator, &buffer_info, &allocation_info,
		&buffer, &allocation, &result_info) != VK_SUCCESS) {
		return false;
	}
	if (data != nullptr) {
		std::memcpy(result_info.pMappedData, data, static_cast<std::size_t>(size));
		vmaFlushAllocation(context.allocator, allocation, 0, size);
	}
	if (mapped != nullptr) {
		*mapped = result_info.pMappedData;
	}
	return true;
}

bool createGeometryBuffers() {
	if (!createBuffer(sizeof(cube::vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		cube::vertices.data(), vertex_buffer, vertex_allocation)) {
		std::cerr << "Failed to create vertex buffer\n";
		return false;
	}
	if (!createBuffer(sizeof(cube::indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		cube::indices.data(), index_buffer, index_allocation)) {
		std::cerr << "Failed to create index buffer\n";
		return false;
	}
	if (!createBuffer(sizeof(UniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		nullptr, uniform_buffer, uniform_allocation, &uniform_mapped)) {
		std::cerr << "Failed to create uniform buffer\n";
		return false;
	}
	return true;
}

bool createDescriptors() {
	auto& context = graphics::internal::context;
	const VkDescriptorSetLayoutBinding binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
	};
	const VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &binding,
	};
	if (vkCreateDescriptorSetLayout(context.device, &layout_info, nullptr,
		&descriptor_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor set layout\n";
		return false;
	}
	const VkDescriptorPoolSize pool_size = {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
	};
	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
	};
	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr,
		&descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor pool\n";
		return false;
	}
	const VkDescriptorSetAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &descriptor_set_layout,
	};
	if (vkAllocateDescriptorSets(context.device, &allocate_info,
		&descriptor_set) != VK_SUCCESS) {
		std::cerr << "Failed to allocate descriptor set\n";
		return false;
	}
	const VkDescriptorBufferInfo buffer_info = {
		.buffer = uniform_buffer,
		.offset = 0,
		.range = sizeof(UniformData),
	};
	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = descriptor_set,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &buffer_info,
	};
	vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
	return true;
}

bool createGraphicsPipeline() {
	auto& context = graphics::internal::context;
	const VkPipelineShaderStageCreateInfo shader_stages[] = {
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		  .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex_shader, .pName = "main" },
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		  .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment_shader, .pName = "main" },
	};
	const VkVertexInputBindingDescription vertex_binding = {
		.binding = 0, .stride = sizeof(cube::Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};
	const VkVertexInputAttributeDescription vertex_attributes[] = {
		{ .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
		  .offset = offsetof(cube::Vertex, position) },
		{ .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
		  .offset = offsetof(cube::Vertex, color) },
	};
	const VkPipelineVertexInputStateCreateInfo vertex_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &vertex_binding,
		.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(std::size(vertex_attributes)),
		.pVertexAttributeDescriptions = vertex_attributes,
	};
	const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};
	const VkPipelineViewportStateCreateInfo viewport_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1, .scissorCount = 1,
	};
	const VkPipelineRasterizationStateCreateInfo rasterization = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};
	const VkPipelineMultisampleStateCreateInfo multisampling = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};
	const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
	};
	const VkPipelineColorBlendAttachmentState color_attachment = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};
	const VkPipelineColorBlendStateCreateInfo color_blending = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1, .pAttachments = &color_attachment,
	};
	const VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	const VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = static_cast<std::uint32_t>(std::size(dynamic_states)),
		.pDynamicStates = dynamic_states,
	};
	const VkPipelineLayoutCreateInfo pipeline_layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &descriptor_set_layout,
	};
	if (vkCreatePipelineLayout(context.device, &pipeline_layout_info, nullptr,
		&pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create pipeline layout\n";
		return false;
	}
	const VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = static_cast<std::uint32_t>(std::size(shader_stages)),
		.pStages = shader_stages,
		.pVertexInputState = &vertex_input,
		.pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport_state,
		.pRasterizationState = &rasterization,
		.pMultisampleState = &multisampling,
		.pDepthStencilState = &depth_stencil,
		.pColorBlendState = &color_blending,
		.pDynamicState = &dynamic_state,
		.layout = pipeline_layout,
		.renderPass = context.render_pass,
		.subpass = 0,
	};
	if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info,
		nullptr, &graphics_pipeline) != VK_SUCCESS) {
		std::cerr << "Failed to create graphics pipeline\n";
		return false;
	}
	return true;
}

void destroyResources() {
	auto& context = graphics::internal::context;
	if (graphics_pipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(context.device, graphics_pipeline, nullptr);
		graphics_pipeline = VK_NULL_HANDLE;
	}
	if (pipeline_layout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
		pipeline_layout = VK_NULL_HANDLE;
	}
	if (descriptor_pool != VK_NULL_HANDLE) {
		vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
		descriptor_pool = VK_NULL_HANDLE;
	}
	if (descriptor_set_layout != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(context.device, descriptor_set_layout, nullptr);
		descriptor_set_layout = VK_NULL_HANDLE;
	}
	if (uniform_buffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(context.allocator, uniform_buffer, uniform_allocation);
		uniform_buffer = VK_NULL_HANDLE;
		uniform_allocation = VK_NULL_HANDLE;
		uniform_mapped = nullptr;
	}
	if (index_buffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(context.allocator, index_buffer, index_allocation);
		index_buffer = VK_NULL_HANDLE;
		index_allocation = VK_NULL_HANDLE;
	}
	if (vertex_buffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(context.allocator, vertex_buffer, vertex_allocation);
		vertex_buffer = VK_NULL_HANDLE;
		vertex_allocation = VK_NULL_HANDLE;
	}
	if (fragment_shader != VK_NULL_HANDLE) {
		vkDestroyShaderModule(context.device, fragment_shader, nullptr);
		fragment_shader = VK_NULL_HANDLE;
	}
	if (vertex_shader != VK_NULL_HANDLE) {
		vkDestroyShaderModule(context.device, vertex_shader, nullptr);
		vertex_shader = VK_NULL_HANDLE;
	}
}

void updateUniformBuffer() {
	auto& context = graphics::internal::context;
	const float width = static_cast<float>(std::max(context.swapchain_extent.width, 1u));
	const float height = static_cast<float>(std::max(context.swapchain_extent.height, 1u));
	const float aspect = width / height;
	float animated_x = 0.0f;
	float animated_y = 0.0f;
	float animated_z = 0.0f;
	float animated_rotation = 0.0f;
	if (animation_playing || animation_time != 0.0f) {
		animated_x = trajectory_radius * std::sin(animation_time);
		animated_y = trajectory_height * std::cos(animation_time * 3.0f);
		animated_z = trajectory_radius * 0.45f * std::sin(animation_time * 2.0f);
		animated_rotation = animation_time * 45.0f;
	}
	const float rx = (object_rotation[0] + animated_rotation * 0.35f) * pi / 180.0f;
	const float ry = (object_rotation[1] + animated_rotation) * pi / 180.0f;
	const float rz = object_rotation[2] * pi / 180.0f;
	Mat4 model = multiply(
		translationMatrix(object_position[0] + animated_x,
			object_position[1] + animated_y, object_position[2] + animated_z),
		multiply(rotationZMatrix(rz), multiply(rotationYMatrix(ry),
			multiply(rotationXMatrix(rx), scaleMatrix(object_scale[0],
				object_scale[1], object_scale[2]))))
	);
	const Mat4 view = translationMatrix(0.0f, 0.0f, -3.0f);
	const Mat4 projection = projection_mode == 0
		? perspectiveMatrix(45.0f * pi / 180.0f, aspect, 0.1f, 100.0f)
		: orthographicMatrix(aspect, 0.1f, 100.0f);
	UniformData data{};
	data.mvp = multiply(projection, multiply(view, model));
	std::memcpy(data.base_color, base_color, sizeof(base_color));
	std::memcpy(uniform_mapped, &data, sizeof(data));
	vmaFlushAllocation(context.allocator, uniform_allocation, 0, sizeof(data));
}

} // namespace

namespace application {

bool initialize() {
	auto& context = graphics::internal::context;
	vertex_shader = createShaderModule(context.device, "shaders/cube.vert.spv");
	fragment_shader = createShaderModule(context.device, "shaders/cube.frag.spv");
	if (vertex_shader == VK_NULL_HANDLE || fragment_shader == VK_NULL_HANDLE) {
		destroyResources();
		return false;
	}
	if (!createGeometryBuffers() || !createDescriptors() || !createGraphicsPipeline()) {
		destroyResources();
		return false;
	}
	vkDestroyShaderModule(context.device, fragment_shader, nullptr);
	vkDestroyShaderModule(context.device, vertex_shader, nullptr);
	fragment_shader = VK_NULL_HANDLE;
	vertex_shader = VK_NULL_HANDLE;
	return true;
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);
	destroyResources();
}

void update(double time) {
	if (previous_time == 0.0) {
		previous_time = time;
	}
	const float delta = static_cast<float>(std::min(time - previous_time, 0.1));
	previous_time = time;
	if (animation_playing) {
		animation_time += delta * animation_speed;
	}
	ImGui::Begin("Lab 1 - Cube");
	const char* projection_names[] = { "Perspective", "Orthographic" }; //пункт 1. проекции интерфейс
	ImGui::Combo("Projection", &projection_mode, projection_names, 2); //пункт 2 UI
	ImGui::DragFloat3("Position", object_position, 0.02f);
	ImGui::DragFloat3("Rotation", object_rotation, 1.0f);
	ImGui::SliderFloat3("Scale", object_scale, 0.1f, 3.0f);
	ImGui::ColorEdit4("Base color", base_color);
	ImGui::SeparatorText("Animation");
	if (ImGui::Button(animation_playing ? "Pause" : "Play")) {
		animation_playing = !animation_playing;
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset")) {
		animation_time = 0.0f;
	}
	ImGui::SliderFloat("Speed", &animation_speed, 0.1f, 3.0f);
	ImGui::SliderFloat("Radius", &trajectory_radius, 0.0f, 1.2f);
	ImGui::SliderFloat("Height", &trajectory_height, 0.0f, 0.8f);
	ImGui::Text("Trajectory: figure eight with vertical wave");
	ImGui::End();
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;
	updateUniformBuffer();
	vkResetCommandBuffer(fd.command_buffer, 0);
	const VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(fd.command_buffer, &begin_info);
	const VkClearValue clear_values[] = {
		{ .color = {{ 0.15f, 0.15f, 0.20f, 1.0f }} },
		{ .depthStencil = { 1.0f, 0 } },
	};
	const VkRenderPassBeginInfo render_pass_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = { .offset = { 0, 0 }, .extent = context.swapchain_extent },
		.clearValueCount = static_cast<std::uint32_t>(std::size(clear_values)),
		.pClearValues = clear_values,
	};
	vkCmdBeginRenderPass(fd.command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
	const VkViewport viewport = {
		.x = 0.0f, .y = 0.0f,
		.width = static_cast<float>(context.swapchain_extent.width),
		.height = static_cast<float>(context.swapchain_extent.height),
		.minDepth = 0.0f, .maxDepth = 1.0f,
	};
	const VkRect2D scissor = { .offset = { 0, 0 }, .extent = context.swapchain_extent };
	vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
	vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);
	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer, &offset);
	vkCmdBindIndexBuffer(fd.command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(fd.command_buffer,
		static_cast<std::uint32_t>(cube::indices.size()), 1, 0, 0, 0);
	vkCmdEndRenderPass(fd.command_buffer);
	vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application
