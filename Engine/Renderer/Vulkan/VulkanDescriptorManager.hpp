// Engine/Renderer/Vulkan/VulkanDescriptorManager.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <unordered_map>

namespace Nightbloom
{
	class VulkanDevice;
	class VulkanTexture;
	class VulkanBuffer;

	class VulkanDescriptorManager
	{
	public:
		static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
		static constexpr uint32_t MAX_DESCRIPTOR_SETS = 1000;

		VulkanDescriptorManager(VulkanDevice* device);
		~VulkanDescriptorManager();

		bool Initialize();
		void Cleanup();

		// Layout creation
		VkDescriptorSetLayout CreateTextureSetLayout();
		VkDescriptorSetLayout CreateUniformSetLayout(size_t bufferSize);

		// Per-frame descriptor sets
		VkDescriptorSet AllocateTextureSet(uint32_t frameIndex);
		void UpdateTextureSet(VkDescriptorSet set, VulkanTexture* texture, uint32_t binding = 0);

		VkDescriptorSetLayout GetTextureSetLayout() const { return m_TextureSetLayout; }

		// Get current frame's descriptor set
		VkDescriptorSet GetTextureDescriptorSet(uint32_t frameIndex) { return m_TextureDescriptorSets[frameIndex]; }

	private:
		VulkanDevice* m_Device = nullptr;
		VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;

		// Layouts
		VkDescriptorSetLayout m_TextureSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_UniformSetLayout = VK_NULL_HANDLE;

		// Per-frame descriptor sets
		std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_TextureDescriptorSets{};
		std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_UniformDescriptorSets{};
	};
}