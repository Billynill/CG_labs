#include "texture.hpp"

#include <algorithm>
#include <bit>
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "graphics_internal.hpp"
#include "vulkan_utils.hpp"

namespace vulkan_utils {

namespace {

void transitionLayout(VkCommandBuffer cmd, VkImage image, std::uint32_t base_mip, std::uint32_t mip_count,
                      VkImageLayout old_layout, VkImageLayout new_layout,
                      VkAccessFlags src_access, VkAccessFlags dst_access,
                      VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage) {
	const VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = src_access,
		.dstAccessMask = dst_access,
		.oldLayout = old_layout,
		.newLayout = new_layout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = base_mip,
			.levelCount = mip_count,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};
	vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

std::uint32_t mipLevelCount(std::uint32_t width, std::uint32_t height) {
	return static_cast<std::uint32_t>(std::bit_width(std::max(width, height)));
}

bool supportsLinearBlit(VkFormat format) {
	VkFormatProperties properties{};
	vkGetPhysicalDeviceFormatProperties(graphics::internal::context.physical_device, format, &properties);
	return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
}

void recordMipmapGeneration(VkCommandBuffer cmd, VkImage image, std::int32_t width, std::int32_t height,
                            std::uint32_t mip_levels) {
	for (std::uint32_t level = 1; level < mip_levels; ++level) {
		transitionLayout(cmd, image, level - 1, 1,
		                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
		                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

		const std::int32_t next_width = std::max(width / 2, 1);
		const std::int32_t next_height = std::max(height / 2, 1);
		const VkImageBlit blit = {
			.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1 },
			.srcOffsets = { { 0, 0, 0 }, { width, height, 1 } },
			.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 },
			.dstOffsets = { { 0, 0, 0 }, { next_width, next_height, 1 } },
		};
		vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		               image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

		transitionLayout(cmd, image, level - 1, 1,
		                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                 VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
		                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		width = next_width;
		height = next_height;
	}
	transitionLayout(cmd, image, mip_levels - 1, 1,
	                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
	                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

bool createTexture(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height,
                   VkFormat format, Texture& texture) {
	auto& context = graphics::internal::context;
	const VkDeviceSize size = VkDeviceSize{ width } * height * 4;
	const std::uint32_t mip_levels = supportsLinearBlit(format) ? mipLevelCount(width, height) : 1;

	Buffer staging;
	if (!createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging)) {
		std::cerr << "Failed to create staging buffer for texture\n";
		return false;
	}
	writeBuffer(staging, pixels, size);

	const VkImageCreateInfo image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = { width, height, 1 },
		.mipLevels = mip_levels,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	const VmaAllocationCreateInfo allocation_info = {
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
	};
	bool success = vmaCreateImage(context.allocator, &image_info, &allocation_info,
	                              &texture.image, &texture.allocation, nullptr) == VK_SUCCESS;

	if (success) {
		success = submitImmediate([&](VkCommandBuffer cmd) {
			transitionLayout(cmd, texture.image, 0, mip_levels,
			                 VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                 0, VK_ACCESS_TRANSFER_WRITE_BIT,
			                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
			const VkBufferImageCopy region = {
				.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
				.imageExtent = { width, height, 1 },
			};
			vkCmdCopyBufferToImage(cmd, staging.handle, texture.image,
			                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
			recordMipmapGeneration(cmd, texture.image, static_cast<std::int32_t>(width),
			                       static_cast<std::int32_t>(height), mip_levels);
		});
	}
	destroyBuffer(staging);

	if (success) {
		const VkImageViewCreateInfo view_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = texture.image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = format,
			.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, 1 },
		};
		success = vkCreateImageView(context.device, &view_info, nullptr, &texture.view) == VK_SUCCESS;
	}

	if (!success) {
		std::cerr << "Failed to create texture\n";
		destroyTexture(texture);
	}
	return success;
}

} // namespace

bool loadTexture(const char* filename, TextureEncoding encoding, Texture& texture) {
	int width = 0, height = 0, channels = 0;
	stbi_uc* pixels = stbi_load(filename, &width, &height, &channels, STBI_rgb_alpha);
	if (pixels == nullptr) {
		std::cerr << "Failed to load texture " << filename << ": " << stbi_failure_reason() << '\n';
		return false;
	}
	const VkFormat format = encoding == TextureEncoding::srgb ? VK_FORMAT_R8G8B8A8_SRGB
	                                                          : VK_FORMAT_R8G8B8A8_UNORM;
	const bool success = createTexture(pixels, static_cast<std::uint32_t>(width),
	                                   static_cast<std::uint32_t>(height), format, texture);
	stbi_image_free(pixels);
	return success;
}

bool createSolidColorTexture(std::array<std::uint8_t, 4> rgba, Texture& texture) {
	return createTexture(rgba.data(), 1, 1, VK_FORMAT_R8G8B8A8_UNORM, texture);
}

void destroyTexture(Texture& texture) {
	auto& context = graphics::internal::context;
	vkDestroyImageView(context.device, texture.view, nullptr);
	if (texture.image != VK_NULL_HANDLE) {
		vmaDestroyImage(context.allocator, texture.image, texture.allocation);
	}
	texture = {};
}

VkSampler createRepeatSampler() {
	const VkSamplerCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.minLod = 0.0f,
		.maxLod = VK_LOD_CLAMP_NONE,
	};
	VkSampler sampler = VK_NULL_HANDLE;
	if (vkCreateSampler(graphics::internal::context.device, &info, nullptr, &sampler) != VK_SUCCESS) {
		std::cerr << "Failed to create sampler\n";
	}
	return sampler;
}

} // namespace vulkan_utils
