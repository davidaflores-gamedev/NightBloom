#include "Engine/Renderer/Vulkan/VulkanDescriptorManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanTexture.hpp"
#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"
#include "Engine/Core/Logger/Logger.hpp"

namespace Nightbloom
{
	VulkanDescriptorManager::VulkanDescriptorManager(VulkanDevice* device)
		: m_Device(device)
	{
	}

	VulkanDescriptorManager::~VulkanDescriptorManager()
	{
		Cleanup();
	}

	bool VulkanDescriptorManager::Initialize()
	{
		LOG_INFO("Initializing VulkanDescriptorManager");

		// Create descriptor pool
		std::array<VkDescriptorPoolSize, 2> poolSizes{};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[0].descriptorCount = MAX_DESCRIPTOR_SETS;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[1].descriptorCount = MAX_DESCRIPTOR_SETS;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = MAX_DESCRIPTOR_SETS;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

		if (vkCreateDescriptorPool(m_Device->GetDevice(), &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create descriptor pool");
			return false;
		}

		// Create texture set layout
		m_TextureSetLayout = CreateTextureSetLayout();
		if (m_TextureSetLayout == VK_NULL_HANDLE)
		{
			LOG_ERROR("Failed to create texture descriptor set layout");
			return false;
		}

		// Allocate descriptor sets for each frame
		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			m_TextureDescriptorSets[i] = AllocateTextureSet(i);
			if (m_TextureDescriptorSets[i] == VK_NULL_HANDLE)
			{
				LOG_ERROR("Failed to allocate texture descriptor set for frame {}", i);
				return false;
			}
		}

		// Create uniform set layout
		m_UniformSetLayout = CreateUniformSetLayout();
		if (m_UniformSetLayout == VK_NULL_HANDLE)
		{
			LOG_ERROR("Failed to create uniform descriptor set layout");
			return false;
		}

		// Allocate uniform descriptor sets for each frame
		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			m_UniformDescriptorSets[i] = AllocateUniformSet(i);
			if (m_UniformDescriptorSets[i] == VK_NULL_HANDLE)
			{
				LOG_ERROR("Failed to allocate uniform descriptor set for frame {}", i);
				return false;
			}
		}

		// Create lighting set layout
		m_LightingSetLayout = CreateLightingSetLayout();
		if (m_LightingSetLayout == VK_NULL_HANDLE)
		{
			LOG_ERROR("Failed to create lighting descriptor set layout");
			return false;
		}

		// Allocate lighting descriptor sets for each frame
		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			m_LightingDescriptorSets[i] = AllocateLightingSet(i);
			if (m_LightingDescriptorSets[i] == VK_NULL_HANDLE)
			{
				LOG_ERROR("Failed to allocate lighting descriptor set for frame {}", i);
				return false;
			}
		}

		// Create shadow map sampler set layout
		m_ShadowSetLayout = CreateShadowSetLayout();
		if (m_ShadowSetLayout == VK_NULL_HANDLE)
		{
			LOG_ERROR("Failed to create shadow descriptor set layout");
			return false;
		}

		// Allocate shadow map sampler sets
		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			m_ShadowDescriptorSets[i] = AllocateShadowSet(i);
			if (m_ShadowDescriptorSets[i] == VK_NULL_HANDLE)
			{
				LOG_ERROR("Failed to allocate shadow descriptor set for frame {}", i);
				return false;
			}
		}

		// Allocate shadow UNIFORM descriptor sets
		// These use the same layout as the camera uniform (m_UniformSetLayout)
		// but will point at a different buffer containing the light's view/proj
		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			m_ShadowUniformDescriptorSets[i] = AllocateShadowUniformSet(i);
			if (m_ShadowUniformDescriptorSets[i] == VK_NULL_HANDLE)
			{
				LOG_ERROR("Failed to allocate shadow uniform descriptor set for frame {}", i);
				return false;
			}
		}

		LOG_INFO("VulkanDescriptorManager initialized successfully");
		return true;
	}

	void VulkanDescriptorManager::Cleanup()
	{
		VkDevice device = m_Device ? m_Device->GetDevice() : VK_NULL_HANDLE;
		if (device == VK_NULL_HANDLE) return;

		vkDeviceWaitIdle(m_Device->GetDevice());

		if (m_DescriptorPool != VK_NULL_HANDLE)
		{
			vkResetDescriptorPool(m_Device->GetDevice(), m_DescriptorPool, 0);
		}

		if (m_TextureSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_TextureSetLayout, nullptr);
			m_TextureSetLayout = VK_NULL_HANDLE;
		}

		if (m_UniformSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_UniformSetLayout, nullptr);
			m_UniformSetLayout = VK_NULL_HANDLE;
		}

		if (m_LightingSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_LightingSetLayout, nullptr);
			m_LightingSetLayout = VK_NULL_HANDLE;
		}

		if (m_ShadowSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_ShadowSetLayout, nullptr);
			m_ShadowSetLayout = VK_NULL_HANDLE;
		}

		if (m_DescriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
			m_DescriptorPool = VK_NULL_HANDLE;
		}
	}

	VkDescriptorSetLayout VulkanDescriptorManager::CreateTextureSetLayout()
	{
		VkDescriptorSetLayoutBinding samplerLayoutBinding{};
		samplerLayoutBinding.binding = 0;
		samplerLayoutBinding.descriptorCount = 1;
		samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerLayoutBinding.pImmutableSamplers = nullptr;
		samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &samplerLayoutBinding;

		VkDescriptorSetLayout layout;
		if (vkCreateDescriptorSetLayout(m_Device->GetDevice(), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
		{
			return VK_NULL_HANDLE;
		}

		return layout;
	}

	VkDescriptorSetLayout VulkanDescriptorManager::CreateUniformSetLayout()
	{
		VkDescriptorSetLayoutBinding uboBinding{};
		uboBinding.binding = 0;
		uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboBinding.descriptorCount = 1;
		uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &uboBinding;

		VkDescriptorSetLayout layout;
		if (vkCreateDescriptorSetLayout(m_Device->GetDevice(), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
		{
			return VK_NULL_HANDLE;
		}

		return layout;
	}

	VkDescriptorSetLayout VulkanDescriptorManager::CreateLightingSetLayout()
	{
		VkDescriptorSetLayoutBinding lightingBinding{};
		lightingBinding.binding = 0;
		lightingBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		lightingBinding.descriptorCount = 1;
		lightingBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &lightingBinding;

		VkDescriptorSetLayout layout;
		if (vkCreateDescriptorSetLayout(m_Device->GetDevice(), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create lighting descriptor set layout");
			return VK_NULL_HANDLE;
		}

		return layout;
	}

	VkDescriptorSetLayout VulkanDescriptorManager::CreateShadowSetLayout()
	{
		VkDescriptorSetLayoutBinding shadowBinding{};
		shadowBinding.binding = 0;
		shadowBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		shadowBinding.descriptorCount = 1;
		shadowBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		shadowBinding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &shadowBinding;

		VkDescriptorSetLayout layout;
		if (vkCreateDescriptorSetLayout(m_Device->GetDevice(), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create shadow descriptor set layout");
			return VK_NULL_HANDLE;
		}

		LOG_INFO("Shadow descriptor set layout created");
		return layout;
	}

	// =====================================================================
	// Texture sets
	// =====================================================================

	VkDescriptorSet VulkanDescriptorManager::AllocateTextureSet(uint32_t frameIndex)
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_TextureSetLayout;

		VkDescriptorSet descriptorSet;
		if (vkAllocateDescriptorSets(m_Device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate descriptor set");
			return VK_NULL_HANDLE;
		}

		return descriptorSet;
	}

	VkDescriptorSet VulkanDescriptorManager::AllocateTextureDescriptorSet()
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_TextureSetLayout;

		VkDescriptorSet descriptorSet;
		if (vkAllocateDescriptorSets(m_Device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate per-texture descriptor set");
			return VK_NULL_HANDLE;
		}

		return descriptorSet;
	}

	void VulkanDescriptorManager::UpdateTextureSet(VkDescriptorSet set, VulkanTexture* texture, uint32_t binding)
	{
		if (!texture || set == VK_NULL_HANDLE) return;

		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = texture->GetImageView();
		imageInfo.sampler = texture->GetSampler();

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = set;
		descriptorWrite.dstBinding = binding;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(m_Device->GetDevice(), 1, &descriptorWrite, 0, nullptr);
	}

	// =====================================================================
	// Frame uniform sets (camera view/proj - set 0 in main pass)
	// =====================================================================

	VkDescriptorSet VulkanDescriptorManager::AllocateUniformSet(uint32_t frameIndex)
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_UniformSetLayout;

		VkDescriptorSet descriptorSet;
		if (vkAllocateDescriptorSets(m_Device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate descriptor set");
			return VK_NULL_HANDLE;
		}

		return descriptorSet;
	}

	void VulkanDescriptorManager::UpdateUniformSet(uint32_t frameIndex, VkBuffer buffer, size_t size)
	{
		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = size;

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = m_UniformDescriptorSets[frameIndex];
		descriptorWrite.dstBinding = 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pBufferInfo = &bufferInfo;

		vkUpdateDescriptorSets(m_Device->GetDevice(), 1, &descriptorWrite, 0, nullptr);
	}

	// =====================================================================
	// Lighting sets (set 2)
	// =====================================================================

	VkDescriptorSet VulkanDescriptorManager::AllocateLightingSet(uint32_t frameIndex)
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_LightingSetLayout;

		VkDescriptorSet descriptorSet;
		if (vkAllocateDescriptorSets(m_Device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate lighting descriptor set for frame {}", frameIndex);
			return VK_NULL_HANDLE;
		}

		return descriptorSet;
	}

	void VulkanDescriptorManager::UpdateLightingSet(uint32_t frameIndex, VkBuffer buffer, size_t size)
	{
		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = size;

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = m_LightingDescriptorSets[frameIndex];
		descriptorWrite.dstBinding = 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pBufferInfo = &bufferInfo;

		vkUpdateDescriptorSets(m_Device->GetDevice(), 1, &descriptorWrite, 0, nullptr);
	}

	// =====================================================================
	// Shadow map sampler sets (set 3 in main pass)
	// =====================================================================

	VkDescriptorSet VulkanDescriptorManager::AllocateShadowSet(uint32_t frameIndex)
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_ShadowSetLayout;

		VkDescriptorSet descriptorSet;
		if (vkAllocateDescriptorSets(m_Device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate shadow descriptor set for frame {}", frameIndex);
			return VK_NULL_HANDLE;
		}

		return descriptorSet;
	}

	void VulkanDescriptorManager::UpdateShadowSet(uint32_t frameIndex, VkImageView shadowMapView, VkSampler shadowSampler)
	{
		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler = shadowSampler;
		imageInfo.imageView = shadowMapView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = m_ShadowDescriptorSets[frameIndex];
		descriptorWrite.dstBinding = 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(m_Device->GetDevice(), 1, &descriptorWrite, 0, nullptr);
	}

	// =====================================================================
	// Shadow UNIFORM sets (set 0 in shadow pass - light's view/proj)
	// Uses the same layout as the camera uniform, just a different buffer
	// =====================================================================

	VkDescriptorSet VulkanDescriptorManager::AllocateShadowUniformSet(uint32_t frameIndex)
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_UniformSetLayout;  // Same layout as camera uniform

		VkDescriptorSet descriptorSet;
		if (vkAllocateDescriptorSets(m_Device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate shadow uniform descriptor set for frame {}", frameIndex);
			return VK_NULL_HANDLE;
		}

		return descriptorSet;
	}

	void VulkanDescriptorManager::UpdateShadowUniformSet(uint32_t frameIndex, VkBuffer buffer, size_t size)
	{
		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = size;

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = m_ShadowUniformDescriptorSets[frameIndex];
		descriptorWrite.dstBinding = 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pBufferInfo = &bufferInfo;

		vkUpdateDescriptorSets(m_Device->GetDevice(), 1, &descriptorWrite, 0, nullptr);
	}
}