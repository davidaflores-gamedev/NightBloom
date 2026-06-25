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

		// Allocate reflection UNIFORM descriptor sets (set 0 in the planar
		// reflection pass) — same layout as the camera uniform, points at the
		// mirror-flipped camera's UBO. Same pattern as the shadow uniform above.
		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			m_ReflectionUniformDescriptorSets[i] = AllocateReflectionUniformSet(i);
			if (m_ReflectionUniformDescriptorSets[i] == VK_NULL_HANDLE)
			{
				LOG_ERROR("Failed to allocate reflection uniform descriptor set for frame {}", i);
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

		// Create Heightmap Set Layout
		m_HeightmapSetLayout = CreateHeightmapSetLayout();
		if (m_HeightmapSetLayout == VK_NULL_HANDLE)
		{
			LOG_ERROR("Failed to create heightmap descriptor set layout");
			return false;
		}

		// Create firefly storage + params set layouts
		m_FireflyStorageSetLayout = CreateFireflyStorageSetLayout();
		if (m_FireflyStorageSetLayout == VK_NULL_HANDLE)
		{
			LOG_ERROR("Failed to create firefly storage descriptor set layout");
			return false;
		}

		m_FireflyParamsSetLayout = CreateFireflyParamsSetLayout();
		if (m_FireflyParamsSetLayout == VK_NULL_HANDLE)
		{
			LOG_ERROR("Failed to create firefly params descriptor set layout");
			return false;
		}

		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			m_FireflyParamsDescriptorSets[i] = AllocateFireflyParamsSet(i);
			if (m_FireflyParamsDescriptorSets[i] == VK_NULL_HANDLE)
			{
				LOG_ERROR("Failed to allocate firefly params descriptor set for frame {}", i);
				return false;
			}
		}

		// Create cloud set layout + per-frame sets
		m_CloudSetLayout = CreateCloudSetLayout();
		if (m_CloudSetLayout == VK_NULL_HANDLE)
		{
			LOG_ERROR("Failed to create cloud descriptor set layout");
			return false;
		}

		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			m_CloudDescriptorSets[i] = AllocateCloudSet(i);
			if (m_CloudDescriptorSets[i] == VK_NULL_HANDLE)
			{
				LOG_ERROR("Failed to allocate cloud descriptor set for frame {}", i);
				return false;
			}
		}

		m_CloudResultSetLayout = CreateCloudResultSetLayout();
		if (m_CloudResultSetLayout == VK_NULL_HANDLE)
		{
			LOG_ERROR("Failed to create cloud result descriptor set layout");
			return false;
		}

		// Create foliage storage set layout
		m_FoliageStorageSetLayout = CreateFoliageStorageSetLayout();
		if (m_FoliageStorageSetLayout == VK_NULL_HANDLE)
		{
			LOG_ERROR("Failed to create foliage storage descriptor set layout");
			return false;
		}

		// Create post-process input set layout
		m_PostProcessInputSetLayout = CreatePostProcessInputSetLayout();
		if (m_PostProcessInputSetLayout == VK_NULL_HANDLE)
		{
			LOG_ERROR("Failed to create post-process input descriptor set layout");
			return false;
		}

		// Create reflection input set layout (water samples the reflection target)
		m_ReflectionInputSetLayout = CreateReflectionInputSetLayout();
		if (m_ReflectionInputSetLayout == VK_NULL_HANDLE)
		{
			LOG_ERROR("Failed to create reflection input descriptor set layout");
			return false;
		}

		LOG_INFO("VulkanDescriptorManager initialized successfully");
		return true;
	}

	void VulkanDescriptorManager::FreeDescriptorSet(VkDescriptorSet set)
	{
		if (set == VK_NULL_HANDLE || !m_Device || m_DescriptorPool == VK_NULL_HANDLE)
			return;

		vkFreeDescriptorSets(m_Device->GetDevice(), m_DescriptorPool, 1, &set);
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

		if (m_HeightmapSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_HeightmapSetLayout, nullptr);
			m_HeightmapSetLayout = VK_NULL_HANDLE;
		}

		if (m_FireflyStorageSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_FireflyStorageSetLayout, nullptr);
			m_FireflyStorageSetLayout = VK_NULL_HANDLE;
		}

		if (m_FireflyParamsSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_FireflyParamsSetLayout, nullptr);
			m_FireflyParamsSetLayout = VK_NULL_HANDLE;
		}

		if (m_CloudSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_CloudSetLayout, nullptr);
			m_CloudSetLayout = VK_NULL_HANDLE;
		}

		if (m_CloudResultSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_CloudResultSetLayout, nullptr);
			m_CloudResultSetLayout = VK_NULL_HANDLE;
		}

		if (m_FoliageStorageSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_FoliageStorageSetLayout, nullptr);
			m_FoliageStorageSetLayout = VK_NULL_HANDLE;
		}

		if (m_PostProcessInputSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_PostProcessInputSetLayout, nullptr);
			m_PostProcessInputSetLayout = VK_NULL_HANDLE;
		}

		if (m_ReflectionInputSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_ReflectionInputSetLayout, nullptr);
			m_ReflectionInputSetLayout = VK_NULL_HANDLE;
		}

		if (m_DescriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
			m_DescriptorPool = VK_NULL_HANDLE;
		}
	}

	VkDescriptorSetLayout VulkanDescriptorManager::CreateTextureSetLayout()
	{
		std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

		for (uint32_t i = 0; i < 3; ++i)
		{
			bindings[i].binding = i;
			bindings[i].descriptorCount = 1;
			bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[i].pImmutableSamplers = nullptr;
			bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		}

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

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
		// COMPUTE included so CloudRaymarch.comp can read view/proj/invView/
		// invProj/cameraPos for ray reconstruction - existing graphics
		// pipelines are unaffected by widening this (they just don't use
		// the extra visibility).
		uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

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
		// COMPUTE included so CloudRaymarch.comp can read the sun light -
		// existing graphics pipelines are unaffected by widening this.
		lightingBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

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

	VkDescriptorSet VulkanDescriptorManager::AllocateReflectionUniformSet(uint32_t frameIndex)
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_UniformSetLayout;  // Same layout as camera uniform

		VkDescriptorSet descriptorSet;
		if (vkAllocateDescriptorSets(m_Device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate reflection uniform descriptor set for frame {}", frameIndex);
			return VK_NULL_HANDLE;
		}

		return descriptorSet;
	}

	void VulkanDescriptorManager::UpdateReflectionUniformSet(uint32_t frameIndex, VkBuffer buffer, size_t size)
	{
		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = size;

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = m_ReflectionUniformDescriptorSets[frameIndex];
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

	VkDescriptorSetLayout VulkanDescriptorManager::CreateHeightmapSetLayout()
	{
		// Combined image sampler accessible from the VERTEX stage
		// (unlike the texture set which is fragment-only)
		VkDescriptorSetLayoutBinding binding{};
		binding.binding = 0;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		binding.descriptorCount = 1;
		binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;   // <-- key difference
		binding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &binding;

		VkDescriptorSetLayout layout;
		if (vkCreateDescriptorSetLayout(m_Device->GetDevice(), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create heightmap descriptor set layout");
			return VK_NULL_HANDLE;
		}

		LOG_INFO("Created heightmap (vertex-stage) descriptor set layout");
		return layout;
	}

	VkDescriptorSet VulkanDescriptorManager::AllocateHeightmapSet()
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_HeightmapSetLayout;

		VkDescriptorSet set;
		if (vkAllocateDescriptorSets(m_Device->GetDevice(), &allocInfo, &set) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate heightmap descriptor set");
			return VK_NULL_HANDLE;
		}
		return set;
	}

	void VulkanDescriptorManager::UpdateHeightmapSet(VkDescriptorSet set, VulkanTexture* texture)
	{
		if (set == VK_NULL_HANDLE || !texture) return;

		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler = texture->GetSampler();
		imageInfo.imageView = texture->GetImageView();
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = set;
		write.dstBinding = 0;
		write.dstArrayElement = 0;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.descriptorCount = 1;
		write.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(m_Device->GetDevice(), 1, &write, 0, nullptr);
	}

	// =====================================================================
	// Firefly agent storage buffer (vertex+compute visible, single set)
	// =====================================================================

	VkDescriptorSetLayout VulkanDescriptorManager::CreateFireflyStorageSetLayout()
	{
		VkDescriptorSetLayoutBinding binding{};
		binding.binding = 0;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		binding.descriptorCount = 1;
		binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
		binding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &binding;

		VkDescriptorSetLayout layout;
		if (vkCreateDescriptorSetLayout(m_Device->GetDevice(), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create firefly storage descriptor set layout");
			return VK_NULL_HANDLE;
		}

		LOG_INFO("Created firefly storage (vertex+compute) descriptor set layout");
		return layout;
	}

	VkDescriptorSet VulkanDescriptorManager::AllocateFireflyStorageSet()
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_FireflyStorageSetLayout;

		VkDescriptorSet set;
		if (vkAllocateDescriptorSets(m_Device->GetDevice(), &allocInfo, &set) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate firefly storage descriptor set");
			return VK_NULL_HANDLE;
		}
		return set;
	}

	void VulkanDescriptorManager::UpdateFireflyStorageSet(VkDescriptorSet set, VkBuffer buffer, VkDeviceSize size)
	{
		if (set == VK_NULL_HANDLE || buffer == VK_NULL_HANDLE) return;

		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = size;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = set;
		write.dstBinding = 0;
		write.dstArrayElement = 0;
		write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write.descriptorCount = 1;
		write.pBufferInfo = &bufferInfo;

		vkUpdateDescriptorSets(m_Device->GetDevice(), 1, &write, 0, nullptr);
	}

	// =====================================================================
	// Firefly params UBO (compute-only, double-buffered)
	// =====================================================================

	VkDescriptorSetLayout VulkanDescriptorManager::CreateFireflyParamsSetLayout()
	{
		VkDescriptorSetLayoutBinding binding{};
		binding.binding = 0;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		binding.descriptorCount = 1;
		binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		binding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &binding;

		VkDescriptorSetLayout layout;
		if (vkCreateDescriptorSetLayout(m_Device->GetDevice(), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create firefly params descriptor set layout");
			return VK_NULL_HANDLE;
		}

		LOG_INFO("Created firefly params (compute) descriptor set layout");
		return layout;
	}

	VkDescriptorSet VulkanDescriptorManager::AllocateFireflyParamsSet(uint32_t frameIndex)
	{
		(void)frameIndex;
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_FireflyParamsSetLayout;

		VkDescriptorSet set;
		if (vkAllocateDescriptorSets(m_Device->GetDevice(), &allocInfo, &set) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate firefly params descriptor set");
			return VK_NULL_HANDLE;
		}
		return set;
	}

	void VulkanDescriptorManager::UpdateFireflyParamsSet(uint32_t frameIndex, VkBuffer buffer, VkDeviceSize size)
	{
		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = size;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = m_FireflyParamsDescriptorSets[frameIndex];
		write.dstBinding = 0;
		write.dstArrayElement = 0;
		write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		write.descriptorCount = 1;
		write.pBufferInfo = &bufferInfo;

		vkUpdateDescriptorSets(m_Device->GetDevice(), 1, &write, 0, nullptr);
	}

	// =====================================================================
	// Cloud set (set 1 in Clouds pass): shape sampler, detail sampler, params UBO
	// =====================================================================

	VkDescriptorSetLayout VulkanDescriptorManager::CreateCloudSetLayout()
	{
		// Consumed by CloudRaymarch.comp (the raymarch moved to a low-res
		// compute pass for performance - see .claude/ROADMAP.md Phase 1.4).
		// The graphics composite pass no longer needs shape/detail/params at
		// all; it only samples the small raymarch result via CloudResultSetLayout.
		std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		bindings[1].binding = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		bindings[2].binding = 2;
		bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[2].descriptorCount = 1;
		bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		VkDescriptorSetLayout layout;
		if (vkCreateDescriptorSetLayout(m_Device->GetDevice(), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create cloud descriptor set layout");
			return VK_NULL_HANDLE;
		}

		LOG_INFO("Created cloud descriptor set layout");
		return layout;
	}

	VkDescriptorSet VulkanDescriptorManager::AllocateCloudSet(uint32_t frameIndex)
	{
		(void)frameIndex;
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_CloudSetLayout;

		VkDescriptorSet set;
		if (vkAllocateDescriptorSets(m_Device->GetDevice(), &allocInfo, &set) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate cloud descriptor set");
			return VK_NULL_HANDLE;
		}
		return set;
	}

	void VulkanDescriptorManager::UpdateCloudTextureBindings(uint32_t frameIndex, VulkanTexture* shapeTexture, VulkanTexture* detailTexture)
	{
		if (!shapeTexture || !detailTexture) return;

		VkDescriptorImageInfo shapeInfo{};
		shapeInfo.sampler = shapeTexture->GetSampler();
		shapeInfo.imageView = shapeTexture->GetImageView();
		shapeInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkDescriptorImageInfo detailInfo{};
		detailInfo.sampler = detailTexture->GetSampler();
		detailInfo.imageView = detailTexture->GetImageView();
		detailInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		std::array<VkWriteDescriptorSet, 2> writes{};
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = m_CloudDescriptorSets[frameIndex];
		writes[0].dstBinding = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[0].pImageInfo = &shapeInfo;

		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = m_CloudDescriptorSets[frameIndex];
		writes[1].dstBinding = 1;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[1].pImageInfo = &detailInfo;

		vkUpdateDescriptorSets(m_Device->GetDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
	}

	void VulkanDescriptorManager::UpdateCloudParamsBinding(uint32_t frameIndex, VkBuffer buffer, VkDeviceSize size)
	{
		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = size;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = m_CloudDescriptorSets[frameIndex];
		write.dstBinding = 2;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		write.pBufferInfo = &bufferInfo;

		vkUpdateDescriptorSets(m_Device->GetDevice(), 1, &write, 0, nullptr);
	}

	// =====================================================================
	// Cloud result sampler (set 1 in the graphics Clouds composite pass)
	// =====================================================================

	VkDescriptorSetLayout VulkanDescriptorManager::CreateCloudResultSetLayout()
	{
		VkDescriptorSetLayoutBinding binding{};
		binding.binding = 0;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		binding.descriptorCount = 1;
		binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &binding;

		VkDescriptorSetLayout layout;
		if (vkCreateDescriptorSetLayout(m_Device->GetDevice(), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create cloud result descriptor set layout");
			return VK_NULL_HANDLE;
		}

		LOG_INFO("Created cloud result descriptor set layout");
		return layout;
	}

	VkDescriptorSet VulkanDescriptorManager::AllocateCloudResultSet()
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_CloudResultSetLayout;

		VkDescriptorSet set;
		if (vkAllocateDescriptorSets(m_Device->GetDevice(), &allocInfo, &set) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate cloud result descriptor set");
			return VK_NULL_HANDLE;
		}
		return set;
	}

	void VulkanDescriptorManager::UpdateCloudResultSet(VkDescriptorSet set, VulkanTexture* resultTexture)
	{
		if (set == VK_NULL_HANDLE || !resultTexture) return;

		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler = resultTexture->GetSampler();
		imageInfo.imageView = resultTexture->GetImageView();
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = set;
		write.dstBinding = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(m_Device->GetDevice(), 1, &write, 0, nullptr);
	}

	// =====================================================================
	// Foliage instance storage buffer (vertex-only visible, single set,
	// one-shot generated like Terrain's heightmap — no compute dispatch)
	// =====================================================================

	VkDescriptorSetLayout VulkanDescriptorManager::CreateFoliageStorageSetLayout()
	{
		VkDescriptorSetLayoutBinding binding{};
		binding.binding = 0;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		binding.descriptorCount = 1;
		binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		binding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &binding;

		VkDescriptorSetLayout layout;
		if (vkCreateDescriptorSetLayout(m_Device->GetDevice(), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create foliage storage descriptor set layout");
			return VK_NULL_HANDLE;
		}

		LOG_INFO("Created foliage storage (vertex-only) descriptor set layout");
		return layout;
	}

	VkDescriptorSet VulkanDescriptorManager::AllocateFoliageStorageSet()
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_FoliageStorageSetLayout;

		VkDescriptorSet set;
		if (vkAllocateDescriptorSets(m_Device->GetDevice(), &allocInfo, &set) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate foliage storage descriptor set");
			return VK_NULL_HANDLE;
		}
		return set;
	}

	void VulkanDescriptorManager::UpdateFoliageStorageSet(VkDescriptorSet set, VkBuffer buffer, VkDeviceSize size)
	{
		if (set == VK_NULL_HANDLE || buffer == VK_NULL_HANDLE) return;

		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = size;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = set;
		write.dstBinding = 0;
		write.dstArrayElement = 0;
		write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write.descriptorCount = 1;
		write.pBufferInfo = &bufferInfo;

		vkUpdateDescriptorSets(m_Device->GetDevice(), 1, &write, 0, nullptr);
	}

	// =====================================================================
	// Post-process input (set 0 in the PostProcess/FXAA pass)
	// =====================================================================

	VkDescriptorSetLayout VulkanDescriptorManager::CreatePostProcessInputSetLayout()
	{
		VkDescriptorSetLayoutBinding binding{};
		binding.binding = 0;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		binding.descriptorCount = 1;
		binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		binding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &binding;

		VkDescriptorSetLayout layout;
		if (vkCreateDescriptorSetLayout(m_Device->GetDevice(), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create post-process input descriptor set layout");
			return VK_NULL_HANDLE;
		}

		LOG_INFO("Created post-process input descriptor set layout");
		return layout;
	}

	VkDescriptorSet VulkanDescriptorManager::AllocatePostProcessInputSet()
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_PostProcessInputSetLayout;

		VkDescriptorSet set;
		if (vkAllocateDescriptorSets(m_Device->GetDevice(), &allocInfo, &set) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate post-process input descriptor set");
			return VK_NULL_HANDLE;
		}
		return set;
	}

	void VulkanDescriptorManager::UpdatePostProcessInputSet(VkDescriptorSet set, VkImageView imageView, VkSampler sampler)
	{
		if (set == VK_NULL_HANDLE || imageView == VK_NULL_HANDLE) return;

		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler = sampler;
		imageInfo.imageView = imageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = set;
		write.dstBinding = 0;
		write.dstArrayElement = 0;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.descriptorCount = 1;
		write.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(m_Device->GetDevice(), 1, &write, 0, nullptr);
	}

	// =====================================================================
	// Reflection input (set 2 in the Water pass) — the planar-reflection
	// color target. Same single-sampler shape as the post-process input.
	// =====================================================================

	VkDescriptorSetLayout VulkanDescriptorManager::CreateReflectionInputSetLayout()
	{
		VkDescriptorSetLayoutBinding binding{};
		binding.binding = 0;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		binding.descriptorCount = 1;
		binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		binding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &binding;

		VkDescriptorSetLayout layout;
		if (vkCreateDescriptorSetLayout(m_Device->GetDevice(), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create reflection input descriptor set layout");
			return VK_NULL_HANDLE;
		}

		LOG_INFO("Created reflection input descriptor set layout");
		return layout;
	}

	VkDescriptorSet VulkanDescriptorManager::AllocateReflectionInputSet()
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_ReflectionInputSetLayout;

		VkDescriptorSet set;
		if (vkAllocateDescriptorSets(m_Device->GetDevice(), &allocInfo, &set) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate reflection input descriptor set");
			return VK_NULL_HANDLE;
		}
		return set;
	}

	void VulkanDescriptorManager::UpdateReflectionInputSet(VkDescriptorSet set, VkImageView imageView, VkSampler sampler)
	{
		if (set == VK_NULL_HANDLE || imageView == VK_NULL_HANDLE) return;

		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler = sampler;
		imageInfo.imageView = imageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = set;
		write.dstBinding = 0;
		write.dstArrayElement = 0;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.descriptorCount = 1;
		write.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(m_Device->GetDevice(), 1, &write, 0, nullptr);
	}
}