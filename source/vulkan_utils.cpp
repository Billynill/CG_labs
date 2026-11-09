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

bool createDeviceLocalBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer) {
	Buffer staging;
	if (!createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging)) {
		std::cerr << "Failed to create staging buffer\n";
		return false;
	}
	writeBuffer(staging, data, size);

	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	const VmaAllocationCreateInfo allocation_info = {
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
	};
	bool success = vmaCreateBuffer(graphics::internal::context.allocator, &buffer_info, &allocation_info,
	                               &buffer.handle, &buffer.allocation, nullptr) == VK_SUCCESS;
	if (success) {
		buffer.size = size;
		success = submitImmediate([&](VkCommandBuffer cmd) {
			const VkBufferCopy region = { .srcOffset = 0, .dstOffset = 0, .size = size };
			vkCmdCopyBuffer(cmd, staging.handle, buffer.handle, 1, &region);
		});
	}
	if (!success) {
		std::cerr << "Failed to create device local buffer\n";
	}
	destroyBuffer(staging);
	return success;
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
	vkDeviceWaitIdle(graphics::internal::context.device);
	destroyBuffer(buffer);
	buffer = bigger;
	return true;
}

bool submitImmediate(const std::function<void(VkCommandBuffer)>& record) {
	auto& context = graphics::internal::context;

	const VkCommandPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = context.graphics_queue_index,
	};
	VkCommandPool pool = VK_NULL_HANDLE;
	if (vkCreateCommandPool(context.device, &pool_info, nullptr, &pool) != VK_SUCCESS) {
		std::cerr << "Failed to create command pool for upload\n";
		return false;
	}

	const VkCommandBufferAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VkCommandBuffer cmd = VK_NULL_HANDLE;
	bool success = vkAllocateCommandBuffers(context.device, &allocate_info, &cmd) == VK_SUCCESS;
	if (success) {
		const VkCommandBufferBeginInfo begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		vkBeginCommandBuffer(cmd, &begin_info);
		record(cmd);
		vkEndCommandBuffer(cmd);

		const VkSubmitInfo submit = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &cmd,
		};
		success = vkQueueSubmit(context.graphics_queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS &&
		          vkQueueWaitIdle(context.graphics_queue) == VK_SUCCESS;
	}
	if (!success) {
		std::cerr << "Failed to submit upload commands\n";
	}
	vkDestroyCommandPool(context.device, pool, nullptr);
	return success;
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
                           const Buffer& buffer, VkDeviceSize range) {
	const VkDescriptorBufferInfo buffer_info = {
		.buffer = buffer.handle,
		.offset = 0,
		.range = range,
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

void writeImageDescriptor(VkDescriptorSet set, std::uint32_t binding, VkImageView view, VkSampler sampler) {
	const VkDescriptorImageInfo image_info = {
		.sampler = sampler,
		.imageView = view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = set,
		.dstBinding = binding,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = &image_info,
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
