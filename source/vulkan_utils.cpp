#include "vulkan_utils.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#include "graphics_internal.hpp"

namespace vulkan_utils {

namespace {

std::vector<std::uint32_t> readShaderCode(const char* filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open()) {
		std::cerr << "Failed to open shader: " << filename << '\n';
		return {};
	}
	const auto size = static_cast<std::size_t>(file.tellg());
	if (size == 0 || size % sizeof(std::uint32_t) != 0) {
		std::cerr << "Invalid SPIR-V file: " << filename << '\n';
		return {};
	}
	std::vector<std::uint32_t> code(size / sizeof(std::uint32_t));
	file.seekg(0);
	file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(size));
	return file ? code : std::vector<std::uint32_t>{};
}

} // namespace

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer) {
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
	VmaAllocationInfo result{};
	if (vmaCreateBuffer(graphics::internal::context.allocator, &buffer_info, &allocation_info,
	                    &buffer.handle, &buffer.allocation, &result) != VK_SUCCESS) {
		return false;
	}
	buffer.mapped = result.pMappedData;
	buffer.size = size;
	return true;
}

void destroyBuffer(Buffer& buffer) {
	if (buffer.handle != VK_NULL_HANDLE) {
		vmaDestroyBuffer(graphics::internal::context.allocator, buffer.handle, buffer.allocation);
	}
	buffer = {};
}

void writeBuffer(Buffer& buffer, const void* data, VkDeviceSize size) {
	if (size == 0) {
		return;
	}
	std::memcpy(buffer.mapped, data, static_cast<std::size_t>(size));
	// Память может быть не HOST_COHERENT, поэтому явно сбрасываем кэш.
	vmaFlushAllocation(graphics::internal::context.allocator, buffer.allocation, 0, size);
}

bool growBufferIfNeeded(Buffer& buffer, VkDeviceSize required_size, VkBufferUsageFlags usage) {
	if (required_size <= buffer.size) {
		return false;
	}
	Buffer bigger;
	if (!createBuffer(std::max(required_size, buffer.size * 2), usage, bigger)) {
		std::cerr << "Failed to grow buffer to " << required_size << " bytes\n";
		return false;
	}
	// Старый буфер мог использоваться предыдущим кадром - дожидаемся GPU.
	vkDeviceWaitIdle(graphics::internal::context.device);
	destroyBuffer(buffer);
	buffer = bigger;
	return true;
}

VkShaderModule createShaderModule(const char* filename) {
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
	if (vkCreateShaderModule(graphics::internal::context.device, &create_info, nullptr,
	                         &module) != VK_SUCCESS) {
		std::cerr << "Failed to create shader module: " << filename << '\n';
	}
	return module;
}

VkDescriptorSetLayout createSetLayout(std::span<const VkDescriptorSetLayoutBinding> bindings) {
	const VkDescriptorSetLayoutCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = static_cast<std::uint32_t>(bindings.size()),
		.pBindings = bindings.data(),
	};
	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	if (vkCreateDescriptorSetLayout(graphics::internal::context.device, &info, nullptr,
	                                &layout) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor set layout\n";
	}
	return layout;
}

void writeBufferDescriptor(VkDescriptorSet set, std::uint32_t binding, VkDescriptorType type,
                           const Buffer& buffer) {
	const VkDescriptorBufferInfo buffer_info = {
		.buffer = buffer.handle,
		.offset = 0,
		.range = VK_WHOLE_SIZE,
	};
	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = set,
		.dstBinding = binding,
		.descriptorCount = 1,
		.descriptorType = type,
		.pBufferInfo = &buffer_info,
	};
	vkUpdateDescriptorSets(graphics::internal::context.device, 1, &write, 0, nullptr);
}

VkPipeline createPipeline(const PipelineDescription& description) {
	auto& context = graphics::internal::context;

	const VkPipelineShaderStageCreateInfo stages[] = {
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		  .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = description.vertex_shader, .pName = "main" },
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		  .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = description.fragment_shader, .pName = "main" },
	};

	const VkVertexInputBindingDescription vertex_binding = {
		.binding = 0,
		.stride = description.vertex_stride,
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};
	const VkPipelineVertexInputStateCreateInfo vertex_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &vertex_binding,
		.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(description.vertex_attributes.size()),
		.pVertexAttributeDescriptions = description.vertex_attributes.data(),
	};

	const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = description.topology,
	};
	const VkPipelineViewportStateCreateInfo viewport_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};
	// Проекция уже переворачивает ось Y, поэтому лицевые грани остаются
	// против часовой стрелки и задние грани можно отсекать.
	const VkPipelineRasterizationStateCreateInfo rasterization = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
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
		.attachmentCount = 1,
		.pAttachments = &color_attachment,
	};
	const VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	const VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = static_cast<std::uint32_t>(std::size(dynamic_states)),
		.pDynamicStates = dynamic_states,
	};

	const VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = static_cast<std::uint32_t>(std::size(stages)),
		.pStages = stages,
		.pVertexInputState = &vertex_input,
		.pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport_state,
		.pRasterizationState = &rasterization,
		.pMultisampleState = &multisampling,
		.pDepthStencilState = &depth_stencil,
		.pColorBlendState = &color_blending,
		.pDynamicState = &dynamic_state,
		.layout = description.layout,
		.renderPass = context.render_pass,
		.subpass = 0,
	};
	VkPipeline pipeline = VK_NULL_HANDLE;
	if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
	                              &pipeline) != VK_SUCCESS) {
		std::cerr << "Failed to create graphics pipeline\n";
	}
	return pipeline;
}

} // namespace vulkan_utils
