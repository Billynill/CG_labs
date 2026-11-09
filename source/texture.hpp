#pragma once

#include <array>
#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <vk_mem_alloc.h>

namespace vulkan_utils {

struct Texture {
	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
};

enum class TextureEncoding {
	srgb,
	linear,
};

bool loadTexture(const char* filename, TextureEncoding encoding, Texture& texture);

bool createSolidColorTexture(std::array<std::uint8_t, 4> rgba, Texture& texture);

void destroyTexture(Texture& texture);

VkSampler createRepeatSampler();

} // namespace vulkan_utils
