// Engine/Renderer/Vulkan/VulkanDescriptorManager.cpp
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

		LOG_INFO("VulkanDescriptorManager initialized successfully");
		return true;
	}

	void VulkanDescriptorManager::Cleanup()
	{
		VkDevice device = m_Device ? m_Device->GetDevice() : VK_NULL_HANDLE;
		if (device == VK_NULL_HANDLE) return;

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
}