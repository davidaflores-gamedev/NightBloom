//------------------------------------------------------------------------------
// VulkanTexture.cpp
//
// Vulkan texture implementation using VMA
//------------------------------------------------------------------------------

#include "Engine/Renderer/Vulkan/VulkanTexture.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanMemoryManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanDescriptorManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanCommandPool.hpp"
#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <cstring>

namespace Nightbloom
{
	VulkanTexture::VulkanTexture(VulkanDevice* device, VulkanMemoryManager* memoryManager)
		: m_Device(device)
		, m_MemoryManager(memoryManager)
	{
	}

	VulkanTexture::~VulkanTexture()
	{
		Cleanup();
	}

	bool VulkanTexture::Initialize(const TextureDesc& desc)
	{
		m_Width = desc.width;
		m_Height = desc.height;
		m_Depth = desc.depth;
		m_MipLevels = desc.mipLevels;
		m_ArrayLayers = desc.arrayLayers;
		m_Format = desc.format;
		m_Usage = desc.usage;

		// Create image through VMA
		if (!CreateImage())
		{
			LOG_ERROR("Failed to create texture image");
			return false;
		}

		// Create image view
		if (!CreateImageView())
		{
			LOG_ERROR("Failed to create texture image view");
			Cleanup();
			return false;
		}

		// Create sampler if this is a sampled texture
		if (static_cast<int>(m_Usage) & static_cast<int>(TextureUsage::Sampled))
		{
			if (!CreateSampler())
			{
				LOG_ERROR("Failed to create texture sampler");
				Cleanup();
				return false;
			}
		}

		return true;
	}

	bool VulkanTexture::UploadData(const void* data, size_t size, VulkanCommandPool* cmdPool)
	{
		if (!data || size == 0 || !cmdPool || !m_ImageAllocation)
		{
			LOG_ERROR("Invalid parameters for texture upload");
			return false;
		}

		// If you can sanity-check byte size vs image extent, do it here (optional):
		// const size_t expected = size_t(m_Width) * m_Height * m_Depth * BytesPerPixel(m_Format) * m_ArrayLayers;
		// if (size < expected) { LOG_WARN("UploadData: provided size < expected image size"); }

		// Try to use the shared staging pool first
		if (m_MemoryManager)
		{
			StagingBufferPool* pool = m_MemoryManager->GetStagingPool();
			if (pool)
			{
				const bool success = pool->WithStagingBuffer(size,
					[&](VulkanBuffer* stagingBuffer) -> bool
					{
						// Upload CPU data into the pooled staging buffer
						if (!stagingBuffer->Update(data, size, /*dstOffset*/ 0))
						{
							LOG_ERROR("Failed to update pooled staging buffer");
							return false;
						}

						// Record copy into the texture
						VulkanSingleTimeCommand cmd(m_Device, cmdPool);
						VkCommandBuffer commandBuffer = cmd.Begin();

						// Transition to transfer dst
						TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

						VkBufferImageCopy region{};
						region.bufferOffset = 0;
						region.bufferRowLength = 0;      // tightly packed
						region.bufferImageHeight = 0;    // tightly packed
						region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						region.imageSubresource.mipLevel = 0;
						region.imageSubresource.baseArrayLayer = 0;
						region.imageSubresource.layerCount = m_ArrayLayers;
						region.imageOffset = { 0, 0, 0 };
						region.imageExtent = { m_Width, m_Height, m_Depth };

						vkCmdCopyBufferToImage(
							commandBuffer,
							stagingBuffer->GetBuffer(),
							m_ImageAllocation->image,
							VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
							1,
							&region
						);

						// Transition to shader-read
						TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

						cmd.End();
						return true;
					});

				return success;
			}
			else
			{
				LOG_WARN("No staging pool available, falling back to temporary staging buffer");
			}
		}

		// ---- Fallback path: create a one-off staging buffer with VMA (your existing code) ----
		VulkanBuffer stagingBuffer(m_Device, m_MemoryManager);
		BufferDesc stagingDesc;
		stagingDesc.usage = BufferUsage::Staging;
		stagingDesc.memoryAccess = MemoryAccess::CpuToGpu;
		stagingDesc.size = size;
		stagingDesc.debugName = "TextureStaging";

		if (!stagingBuffer.Initialize(stagingDesc))
		{
			LOG_ERROR("Failed to create staging buffer for texture upload");
			return false;
		}

		if (!stagingBuffer.Update(data, size, 0))
		{
			LOG_ERROR("Failed to update staging buffer");
			return false;
		}

		VulkanSingleTimeCommand cmd(m_Device, cmdPool);
		VkCommandBuffer commandBuffer = cmd.Begin();

		TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		VkBufferImageCopy region{};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = m_ArrayLayers;
		region.imageOffset = { 0, 0, 0 };
		region.imageExtent = { m_Width, m_Height, m_Depth };

		vkCmdCopyBufferToImage(
			commandBuffer,
			stagingBuffer.GetBuffer(),
			m_ImageAllocation->image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&region
		);

		TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		cmd.End();

		return true;
	}

	bool VulkanTexture::CreateDescriptorSet(VulkanDescriptorManager* descriptorManager)
	{
		if (!descriptorManager)
		{
			LOG_ERROR("Cannot create descriptor set: descriptor manager is null");
			return false;
		}

		if (!m_ImageView || !m_Sampler)
		{
			LOG_ERROR("Cannot create descriptor set: texture not fully initialized");
			return false;
		}

		if (m_DescriptorSet != VK_NULL_HANDLE)
		{
			LOG_WARN("Descriptor set already exists for this texture");
			return true;  // Already created, that's fine
		}

		// Allocate a dedicated descriptor set for this texture
		m_DescriptorSet = descriptorManager->AllocateTextureDescriptorSet();
		if (m_DescriptorSet == VK_NULL_HANDLE)
		{
			LOG_ERROR("Failed to allocate descriptor set for texture");
			return false;
		}

		descriptorManager->UpdateTextureSet(m_DescriptorSet, this);

		return true;
	}

	void VulkanTexture::TransitionLayout(VkCommandBuffer cmd, VkImageLayout newLayout)
	{
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = m_CurrentLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = m_ImageAllocation->image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = m_MipLevels;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = m_ArrayLayers;

		VkPipelineStageFlags sourceStage;
		VkPipelineStageFlags destinationStage;

		if (m_CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
			newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (m_CurrentLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
			newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else
		{
			LOG_WARN("Unsupported layout transition from {} to {}",
				static_cast<int>(m_CurrentLayout), static_cast<int>(newLayout));
			return;
		}

		vkCmdPipelineBarrier(
			cmd,
			sourceStage, destinationStage,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		m_CurrentLayout = newLayout;
	}

	void VulkanTexture::Cleanup()
	{
		VkDevice device = m_Device ? m_Device->GetDevice() : VK_NULL_HANDLE;
		if (device == VK_NULL_HANDLE) return;

		if (m_Sampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_Sampler, nullptr);
			m_Sampler = VK_NULL_HANDLE;
		}

		if (m_ImageView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, m_ImageView, nullptr);
			m_ImageView = VK_NULL_HANDLE;
		}

		if (m_ImageAllocation)
		{
			m_MemoryManager->DestroyImage(m_ImageAllocation);
			m_ImageAllocation = nullptr;
		}
	}

	bool VulkanTexture::CreateImage()
	{
		VulkanMemoryManager::ImageCreateInfo imageInfo = {};
		imageInfo.width = m_Width;
		imageInfo.height = m_Height;
		imageInfo.depth = m_Depth;
		imageInfo.mipLevels = m_MipLevels;
		imageInfo.arrayLayers = m_ArrayLayers;
		imageInfo.format = ConvertToVkFormat(m_Format);
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = 0;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.memoryUsage = VMA_MEMORY_USAGE_AUTO;

		// Set usage flags
		if (static_cast<int>(m_Usage) & static_cast<int>(TextureUsage::Sampled))
			imageInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		if (static_cast<int>(m_Usage) & static_cast<int>(TextureUsage::Storage))
			imageInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
		if (static_cast<int>(m_Usage) & static_cast<int>(TextureUsage::RenderTarget))
			imageInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		if (static_cast<int>(m_Usage) & static_cast<int>(TextureUsage::DepthStencil))
			imageInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

		// Always allow transfers for uploads
		imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		m_ImageAllocation = m_MemoryManager->CreateImage(imageInfo);
		return m_ImageAllocation != nullptr;
	}

	bool VulkanTexture::CreateImageView()
	{
		if (!m_ImageAllocation) return false;

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_ImageAllocation->image;
		viewInfo.viewType = m_ArrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = ConvertToVkFormat(m_Format);
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = m_MipLevels;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = m_ArrayLayers;

		if (vkCreateImageView(m_Device->GetDevice(), &viewInfo, nullptr, &m_ImageView) != VK_SUCCESS)
		{
			return false;
		}

		return true;
	}

	bool VulkanTexture::CreateSampler()
	{
		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

		if (m_Device->IsSamplerAnisotrpyEnabled())
		{
			samplerInfo.anisotropyEnable = VK_TRUE;

			VkPhysicalDeviceProperties props{};
			vkGetPhysicalDeviceProperties(m_Device->GetPhysicalDevice(), &props);

			// Clamp to a sane cap; many drivers expose up to 16
			samplerInfo.maxAnisotropy = std::min(16.0f, props.limits.maxSamplerAnisotropy);
		}
		else
		{
			samplerInfo.anisotropyEnable = VK_FALSE;
			samplerInfo.maxAnisotropy = 1.0f;
		}

		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = (m_MipLevels > 0) ? float(m_MipLevels - 1) : 0.0f;
		// test this samplerInfo.maxLod = static_cast<float>(m_MipLevels); 

		if (vkCreateSampler(m_Device->GetDevice(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS)
		{
			return false;
		}

		return true;
	}

	VkFormat VulkanTexture::ConvertToVkFormat(TextureFormat format)
	{
		switch (format)
		{
		case TextureFormat::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
		case TextureFormat::BGRA8: return VK_FORMAT_B8G8R8A8_UNORM;
		case TextureFormat::RGB8: return VK_FORMAT_R8G8B8_UNORM;
		case TextureFormat::R8: return VK_FORMAT_R8_UNORM;
		case TextureFormat::RG8: return VK_FORMAT_R8G8_UNORM;

		case TextureFormat::R32F: return VK_FORMAT_R32_SFLOAT;
		case TextureFormat::RG32F: return VK_FORMAT_R32G32_SFLOAT;
		case TextureFormat::RGB32F: return VK_FORMAT_R32G32B32_SFLOAT;
		case TextureFormat::RGBA32F: return VK_FORMAT_R32G32B32A32_SFLOAT;

		case TextureFormat::R16F: return VK_FORMAT_R16_SFLOAT;
		case TextureFormat::RG16F: return VK_FORMAT_R16G16_SFLOAT;
		case TextureFormat::RGB16F: return VK_FORMAT_R16G16B16_SFLOAT;
		case TextureFormat::RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;

		case TextureFormat::Depth24Stencil8: return VK_FORMAT_D24_UNORM_S8_UINT;
		case TextureFormat::Depth32F: return VK_FORMAT_D32_SFLOAT;
		case TextureFormat::Depth16: return VK_FORMAT_D16_UNORM;

		default:
			LOG_WARN("Unknown texture format, defaulting to RGBA8");
			return VK_FORMAT_R8G8B8A8_UNORM;
		}
	}
}