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
		std::array<VkDescriptorPoolSize, 4> poolSizes{};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[0].descriptorCount = MAX_DESCRIPTOR_SETS;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[1].descriptorCount = MAX_DESCRIPTOR_SETS;
		poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		poolSizes[2].descriptorCount = MAX_DESCRIPTOR_SETS;
		poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		poolSizes[3].descriptorCount = MAX_DESCRIPTOR_SETS;

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

		// Create compute storage layout
		m_ComputeStorageSetLayout = CreateComputeStorageSetLayout();
		if (m_ComputeStorageSetLayout == VK_NULL_HANDLE)
		{
			LOG_ERROR("Failed to create compute storage descriptor set layout");
			return false;
		}

		// Create compute image set layout
		m_ComputeImageSetLayout = CreateComputeImageSetLayout();
		if (m_ComputeImageSetLayout == VK_NULL_HANDLE)
		{
			LOG_ERROR("Failed to create compute image descriptor set layout");
			return false;
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

		if (m_ComputeStorageSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_ComputeStorageSetLayout, nullptr);
			m_ComputeStorageSetLayout = VK_NULL_HANDLE;
		}

		if (m_ComputeImageSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_ComputeImageSetLayout, nullptr);
			m_ComputeImageSetLayout = VK_NULL_HANDLE;
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
	// Compute storage sets (for compute shader input/output buffers)
	// =====================================================================

	VkDescriptorSetLayout VulkanDescriptorManager::CreateComputeStorageSetLayout()
	{
		// Two storage buffer bindings: input (binding 0) and output (binding 1)
		std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

		// Input buffer (binding 0) - read-only in shader
		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[0].pImmutableSamplers = nullptr;

		//Output buffer (binding 1) - write in shader
		bindings[1].binding = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[1].pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		VkDescriptorSetLayout layout;
		if (vkCreateDescriptorSetLayout(m_Device->GetDevice(), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create compute storage descriptor set layout");
			return VK_NULL_HANDLE;
		}

		LOG_INFO("Created compute storage descriptor set layout");
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
	VkDescriptorSet VulkanDescriptorManager::AllocateComputeStorageSet()
	{
		if (m_ComputeStorageSetLayout == VK_NULL_HANDLE)
		{
			LOG_ERROR("Compute storage set layout not created");
			return VK_NULL_HANDLE;
		}

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_ComputeStorageSetLayout;

		VkDescriptorSet descriptorSet;
		if (vkAllocateDescriptorSets(m_Device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate compute storage descriptor set");
			return VK_NULL_HANDLE;
		}

		return descriptorSet;
	}
	void VulkanDescriptorManager::UpdateComputeStorageSet(
		VkDescriptorSet set, 
		VkBuffer inputBuffer, VkDeviceSize inputSize, 
		VkBuffer outputBuffer, VkDeviceSize outputSize)
	{
		if (set == VK_NULL_HANDLE)
		{
			LOG_ERROR("Cannot update null descriptor set");
			return;
		}

		std::array<VkDescriptorBufferInfo, 2> bufferInfos{};

		//Input buffer info
		bufferInfos[0].buffer = inputBuffer;
		bufferInfos[0].offset = 0;
		bufferInfos[0].range = inputSize;

		// Output buffer info
		bufferInfos[1].buffer = outputBuffer;
		bufferInfos[1].offset = 0;
		bufferInfos[1].range = outputSize;

		std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

		//Input buffer write (binding 0)
		descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[0].dstSet = set;
		descriptorWrites[0].dstBinding = 0;
		descriptorWrites[0].dstArrayElement = 0;
		descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[0].descriptorCount = 1;
		descriptorWrites[0].pBufferInfo = &bufferInfos[0];

		// Output buffer writes (binding 1)
		descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[1].dstSet = set;
		descriptorWrites[1].dstBinding = 1;
		descriptorWrites[1].dstArrayElement = 0;
		descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[1].descriptorCount = 1;
		descriptorWrites[1].pBufferInfo = &bufferInfos[1];

		vkUpdateDescriptorSets(m_Device->GetDevice(),
			static_cast<uint32_t>(descriptorWrites.size()),
			descriptorWrites.data(),
			0, nullptr);
	}
	void VulkanDescriptorManager::UpdateComputeStorageSet(
		VkDescriptorSet set, 
		VkBuffer buffer, VkDeviceSize size, uint32_t binding)
	{
		if (set == VK_NULL_HANDLE)
		{
			LOG_ERROR("Cannot update null descriptor set");
			return;
		}

		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = size;

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = set;
		descriptorWrite.dstBinding = binding;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pBufferInfo = &bufferInfo;

		vkUpdateDescriptorSets(m_Device->GetDevice(), 1, &descriptorWrite, 0, nullptr);
	}
	VkDescriptorSetLayout VulkanDescriptorManager::CreateComputeImageSetLayout()
	{
		VkDescriptorSetLayoutBinding imageBinding{};
		imageBinding.binding = 0;
		imageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		imageBinding.descriptorCount = 1;
		imageBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		imageBinding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &imageBinding;

		VkDescriptorSetLayout layout;
		if (vkCreateDescriptorSetLayout(m_Device->GetDevice(), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create compute image descriptor set layout");
			return VK_NULL_HANDLE;
		}

		LOG_INFO("Created compute image descriptor set layout");
		return layout;
	}
	VkDescriptorSet VulkanDescriptorManager::AllocateComputeImageSet()
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_ComputeImageSetLayout;

		VkDescriptorSet descriptorSet;
		if (vkAllocateDescriptorSets(m_Device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate compute image descriptor set");
			return VK_NULL_HANDLE;
		}

		return descriptorSet;
	}
	void VulkanDescriptorManager::UpdateComputeImageSet(VkDescriptorSet set, VkImageView imageView)
	{
		if (set == VK_NULL_HANDLE || imageView == VK_NULL_HANDLE) return;

		// Storage images use GENERAL layout — no sampler
		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageView = imageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageInfo.sampler = VK_NULL_HANDLE;

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = set;
		descriptorWrite.dstBinding = 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(m_Device->GetDevice(), 1, &descriptorWrite, 0, nullptr);
	}
}