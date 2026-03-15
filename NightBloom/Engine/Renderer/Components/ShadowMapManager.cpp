//------------------------------------------------------------------------------
// ShadowMapManager.cpp
//
// Implementation of shadow map resource management
//------------------------------------------------------------------------------

#include "Engine/Renderer/Components/ShadowMapManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanMemoryManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanDescriptorManager.hpp"
#include "Engine/Core/Logger/Logger.hpp"

namespace Nightbloom
{
	bool Nightbloom::ShadowMapManager::Initialize(VulkanDevice* device, VulkanMemoryManager* memoryManager, VulkanDescriptorManager* descriptorManager, const ShadowMapConfig& config)
	{
		m_Device = device;
		m_MemoryManager = memoryManager;
		m_DescriptorManager = descriptorManager;
		m_Config = config;

		LOG_INFO("Initializing ShadowMapManager with {}x{} shadow map",
			m_Config.resolution, m_Config.resolution);

		// Create shadow map texture
		if (!CreateShadowMapTexture())
		{
			LOG_ERROR("Failed to create shadow map texture");
			return false;
		}

		// Create shadow render pass
		if (!CreateShadowRenderPass())
		{
			LOG_ERROR("Failed to create shadow render pass");
			DestroyResources();
			return false;
		}

		// Create shadow framebuffer
		if (!CreateShadowFramebuffer())
		{
			LOG_ERROR("Failed to create shadow framebuffer");
			DestroyResources();
			return false;
		}

		// Create shadow sampler
		if (!CreateShadowSampler())
		{
			LOG_ERROR("Failed to create shadow sampler");
			DestroyResources();
			return false;
		}

		// Create descriptor sets for shadow map sampling
		if (!CreateDescriptorSets())
		{
			LOG_ERROR("Failed to create shadow descriptor sets");
			DestroyResources();
			return false;
		}

		LOG_INFO("ShadowMapManager initialized successfully");
		return true;
	}

	void ShadowMapManager::Cleanup()
	{
		if (m_Device)
		{
			vkDeviceWaitIdle(m_Device->GetDevice());
		}
		DestroyResources();
		LOG_INFO("ShadowMapManager cleaned up");
	}

	VkDescriptorSet Nightbloom::ShadowMapManager::GetShadowMapDescriptorSet(uint32_t frameIndex) const
	{
		if (frameIndex >= MAX_FRAMES_IN_FLIGHT)
		{
			LOG_ERROR("Invalid frame index for shadow descriptor set: {}", frameIndex);
			return VK_NULL_HANDLE;
		}
		return m_ShadowDescriptorSets[frameIndex];
	}

	bool Nightbloom::ShadowMapManager::Resize(uint32_t newResolution)
	{
		if (newResolution == m_Config.resolution)
		{
			return true;  // No change needed
		}

		LOG_INFO("Resizing shadow map from {}x{} to {}x{}",
			m_Config.resolution, m_Config.resolution,
			newResolution, newResolution);

		// Wait for GPU to finish
		vkDeviceWaitIdle(m_Device->GetDevice());

		// Store new resolution
		m_Config.resolution = newResolution;

		// Destroy and recreate resources
		VkDevice device = m_Device->GetDevice();

		// Keep render pass (format hasn't changed)
		// Destroy framebuffer and texture
		if (m_ShadowFramebuffer != VK_NULL_HANDLE)
		{
			vkDestroyFramebuffer(device, m_ShadowFramebuffer, nullptr);
			m_ShadowFramebuffer = VK_NULL_HANDLE;
		}

		if (m_ShadowMapView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, m_ShadowMapView, nullptr);
			m_ShadowMapView = VK_NULL_HANDLE;
		}

		if (m_ShadowMapAllocation && m_MemoryManager)
		{
			m_MemoryManager->DestroyImage(
				static_cast<VulkanMemoryManager::ImageAllocation*>(m_ShadowMapAllocation));
			m_ShadowMapAllocation = nullptr;
			m_ShadowMapImage = VK_NULL_HANDLE;
		}

		// Recreate texture and framebuffer
		if (!CreateShadowMapTexture())
		{
			LOG_ERROR("Failed to recreate shadow map texture");
			return false;
		}

		if (!CreateShadowFramebuffer())
		{
			LOG_ERROR("Failed to recreate shadow framebuffer");
			return false;
		}

		// Update descriptor sets with new image view
		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			VkDescriptorImageInfo imageInfo{};
			imageInfo.sampler = m_ShadowSampler;
			imageInfo.imageView = m_ShadowMapView;
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkWriteDescriptorSet descriptorWrite{};
			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstSet = m_ShadowDescriptorSets[i];
			descriptorWrite.dstBinding = 0;
			descriptorWrite.dstArrayElement = 0;
			descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrite.descriptorCount = 1;
			descriptorWrite.pImageInfo = &imageInfo;

			vkUpdateDescriptorSets(m_Device->GetDevice(), 1, &descriptorWrite, 0, nullptr);
		}

		LOG_INFO("Shadow map resized successfully");
		return true;
	}

	bool Nightbloom::ShadowMapManager::CreateShadowMapTexture()
	{
		VulkanMemoryManager::ImageCreateInfo imageInfo{};
		imageInfo.width = m_Config.resolution;
		imageInfo.height = m_Config.resolution;
		imageInfo.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = m_Config.depthFormat;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

		auto* allocation = m_MemoryManager->CreateImage(imageInfo);
		if (!allocation)
		{
			LOG_ERROR("Failed to create shadow map image");
			return false;
		}

		m_ShadowMapAllocation = allocation;
		m_ShadowMapImage = allocation->image;

		// Create image view
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_ShadowMapImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = m_Config.depthFormat;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		if (vkCreateImageView(m_Device->GetDevice(), &viewInfo, nullptr, &m_ShadowMapView) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create shadow map image view");
			return false;
		}

		LOG_INFO("Shadow map texture created: {}x{}, format={}",
			m_Config.resolution, m_Config.resolution, static_cast<int>(m_Config.depthFormat));
		return true;
	}

	bool Nightbloom::ShadowMapManager::CreateShadowRenderPass()
	{
		// Depth-only attachment
		VkAttachmentDescription depthAttachment{};
		depthAttachment.format = m_Config.depthFormat;
		depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // We need to sample it later!
		depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // Ready for sampling

		VkAttachmentReference depthAttachmentRef{};
		depthAttachmentRef.attachment = 0;
		depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		// Subpass - depth only, no color attachments
		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 0;
		subpass.pColorAttachments = nullptr;
		subpass.pDepthStencilAttachment = &depthAttachmentRef;

		// Dependencies for layout transitions
		std::array<VkSubpassDependency, 2> dependencies{};

		// Transition from whatever to depth write
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		// Transition from depth write to shader read
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &depthAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		if (vkCreateRenderPass(m_Device->GetDevice(), &renderPassInfo, nullptr, &m_ShadowRenderPass) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create shadow render pass");
			return false;
		}

		LOG_INFO("Shadow render pass created");
		return true;
	}

	bool Nightbloom::ShadowMapManager::CreateShadowFramebuffer()
	{
		VkFramebufferCreateInfo framebufferInfo{};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = m_ShadowRenderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = &m_ShadowMapView;
		framebufferInfo.width = m_Config.resolution;
		framebufferInfo.height = m_Config.resolution;
		framebufferInfo.layers = 1;

		if (vkCreateFramebuffer(m_Device->GetDevice(), &framebufferInfo, nullptr, &m_ShadowFramebuffer) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create shadow framebuffer");
			return false;
		}

		LOG_INFO("Shadow framebuffer created: {}x{}", m_Config.resolution, m_Config.resolution);
		return true;
	}

	bool Nightbloom::ShadowMapManager::CreateShadowSampler()
	{
		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 1.0f;

		// Border color: 1.0 means "in light" for areas outside shadow map
		samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

		// Enable depth comparison for hardware PCF
		if (m_Config.enablePCF)
		{
			samplerInfo.compareEnable = VK_TRUE;
			samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		}
		else
		{
			samplerInfo.compareEnable = VK_FALSE;
			samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
		}

		if (vkCreateSampler(m_Device->GetDevice(), &samplerInfo, nullptr, &m_ShadowSampler) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create shadow sampler");
			return false;
		}

		LOG_INFO("Shadow sampler created (PCF: {})", m_Config.enablePCF);
		return true;
	}

	bool Nightbloom::ShadowMapManager::CreateDescriptorSets()
	{
		if (!m_DescriptorManager)
		{
			LOG_ERROR("Descriptor manager not set");
			return false;
		}

		// Get the shadow set layout from descriptor manager
		VkDescriptorSetLayout shadowLayout = m_DescriptorManager->GetShadowSetLayout();
		if (shadowLayout == VK_NULL_HANDLE)
		{
			LOG_ERROR("Shadow descriptor set layout not available");
			return false;
		}

		// Allocate descriptor sets for each frame
		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			m_ShadowDescriptorSets[i] = m_DescriptorManager->AllocateShadowSet(i);
			if (m_ShadowDescriptorSets[i] == VK_NULL_HANDLE)
			{
				LOG_ERROR("Failed to allocate shadow descriptor set for frame {}", i);
				return false;
			}

			// Update the descriptor set with our shadow map
			VkDescriptorImageInfo imageInfo{};
			imageInfo.sampler = m_ShadowSampler;
			imageInfo.imageView = m_ShadowMapView;
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkWriteDescriptorSet descriptorWrite{};
			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstSet = m_ShadowDescriptorSets[i];
			descriptorWrite.dstBinding = 0;
			descriptorWrite.dstArrayElement = 0;
			descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrite.descriptorCount = 1;
			descriptorWrite.pImageInfo = &imageInfo;

			vkUpdateDescriptorSets(m_Device->GetDevice(), 1, &descriptorWrite, 0, nullptr);
		}

		LOG_INFO("Shadow descriptor sets created and updated");
		return true;
	}

	void ShadowMapManager::DestroyResources()
	{
		VkDevice device = m_Device ? m_Device->GetDevice() : VK_NULL_HANDLE;
		if (device == VK_NULL_HANDLE) return;

		// Descriptor sets are freed when pool is destroyed, no need to free individually

		if (m_ShadowSampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_ShadowSampler, nullptr);
			m_ShadowSampler = VK_NULL_HANDLE;
		}

		if (m_ShadowFramebuffer != VK_NULL_HANDLE)
		{
			vkDestroyFramebuffer(device, m_ShadowFramebuffer, nullptr);
			m_ShadowFramebuffer = VK_NULL_HANDLE;
		}

		if (m_ShadowRenderPass != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(device, m_ShadowRenderPass, nullptr);
			m_ShadowRenderPass = VK_NULL_HANDLE;
		}

		if (m_ShadowMapView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, m_ShadowMapView, nullptr);
			m_ShadowMapView = VK_NULL_HANDLE;
		}

		if (m_ShadowMapAllocation && m_MemoryManager)
		{
			m_MemoryManager->DestroyImage(
				static_cast<VulkanMemoryManager::ImageAllocation*>(m_ShadowMapAllocation));
			m_ShadowMapAllocation = nullptr;
			m_ShadowMapImage = VK_NULL_HANDLE;
		}
	}
}