#pragma once

#include <cstdint>
#include <span>

#include <vulkan/vulkan_core.h>

#include <vk_mem_alloc.h>

// Небольшие обёртки над Vulkan, чтобы не повторять одни и те же create-info.
namespace vulkan_utils {

// Буфер в памяти, доступной CPU, постоянно отображённый (mapped).
// Для лабораторной этого достаточно: данные маленькие и обновляются каждый кадр.
struct Buffer {
	VkBuffer handle = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	void* mapped = nullptr;
	VkDeviceSize size = 0;
};

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer);
void destroyBuffer(Buffer& buffer);
void writeBuffer(Buffer& buffer, const void* data, VkDeviceSize size);

// Пересоздаёт буфер большего размера, если required_size не помещается.
// Возвращает true, если буфер был пересоздан (значит нужно обновить дескриптор).
bool growBufferIfNeeded(Buffer& buffer, VkDeviceSize required_size, VkBufferUsageFlags usage);

VkShaderModule createShaderModule(const char* filename);

VkDescriptorSetLayout createSetLayout(std::span<const VkDescriptorSetLayoutBinding> bindings);
void writeBufferDescriptor(VkDescriptorSet set, std::uint32_t binding, VkDescriptorType type,
                           const Buffer& buffer);

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
