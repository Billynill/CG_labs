#pragma once

#include <cstdint>
#include <functional>
#include <span>

#include <vulkan/vulkan_core.h>

#include <vk_mem_alloc.h>

namespace vulkan_utils {

struct Buffer {
	VkBuffer handle = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	void* mapped = nullptr;
	VkDeviceSize size = 0;
};

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer);

bool createDeviceLocalBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer);

void destroyBuffer(Buffer& buffer);
void writeBuffer(Buffer& buffer, const void* data, VkDeviceSize size);

bool growBufferIfNeeded(Buffer& buffer, VkDeviceSize required_size, VkBufferUsageFlags usage);

bool submitImmediate(const std::function<void(VkCommandBuffer)>& record);

inline VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
	return (value + alignment - 1) / alignment * alignment;
}

VkShaderModule createShaderModule(const char* filename);

VkDescriptorSetLayout createSetLayout(std::span<const VkDescriptorSetLayoutBinding> bindings);
void writeBufferDescriptor(VkDescriptorSet set, std::uint32_t binding, VkDescriptorType type,
                           const Buffer& buffer, VkDeviceSize range = VK_WHOLE_SIZE);
void writeImageDescriptor(VkDescriptorSet set, std::uint32_t binding, VkImageView view, VkSampler sampler);

struct PipelineDescription {
	VkShaderModule vertex_shader = VK_NULL_HANDLE;
	VkShaderModule fragment_shader = VK_NULL_HANDLE;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	std::uint32_t vertex_stride = 0;
	std::span<const VkVertexInputAttributeDescription> vertex_attributes;
};

VkPipeline createPipeline(const PipelineDescription& description);

} // namespace vulkan_utils
