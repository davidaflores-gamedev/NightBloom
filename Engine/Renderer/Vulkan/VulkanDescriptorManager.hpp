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
		VkDescriptorSetLayout CreateUniformSetLayout();
		VkDescriptorSetLayout CreateLightingSetLayout();
		VkDescriptorSetLayout CreateShadowSetLayout();

		// --- Texture (set 1 in main pass) ---
		VkDescriptorSet AllocateTextureSet(uint32_t frameIndex);
		VkDescriptorSet AllocateTextureDescriptorSet();
		void UpdateTextureSet(VkDescriptorSet set, VulkanTexture* texture, uint32_t binding = 0);
		VkDescriptorSetLayout GetTextureSetLayout() const { return m_TextureSetLayout; }
		VkDescriptorSet GetTextureDescriptorSet(uint32_t frameIndex) { return m_TextureDescriptorSets[frameIndex]; }

		// --- Frame uniform (set 0 in main pass) ---
		VkDescriptorSet AllocateUniformSet(uint32_t frameIndex);
		void UpdateUniformSet(uint32_t frameIndex, VkBuffer buffer, size_t size);
		VkDescriptorSetLayout GetUniformSetLayout() const { return m_UniformSetLayout; }
		VkDescriptorSet GetUniformDescriptorSet(uint32_t frameIndex) { return m_UniformDescriptorSets[frameIndex]; }

		// --- Lighting UBO (set 2 in main pass) ---
		VkDescriptorSet AllocateLightingSet(uint32_t frameIndex);
		void UpdateLightingSet(uint32_t frameIndex, VkBuffer buffer, size_t size);
		VkDescriptorSetLayout GetLightingSetLayout() const { return m_LightingSetLayout; }
		VkDescriptorSet GetLightingDescriptorSet(uint32_t frameIndex) { return m_LightingDescriptorSets[frameIndex]; }

		// --- Shadow map sampler (set 3 in main pass) ---
		VkDescriptorSet AllocateShadowSet(uint32_t frameIndex);
		void UpdateShadowSet(uint32_t frameIndex, VkImageView shadowMapView, VkSampler shadowSampler);
		VkDescriptorSetLayout GetShadowSetLayout() const { return m_ShadowSetLayout; }
		VkDescriptorSet GetShadowDescriptorSet(uint32_t frameIndex) { return m_ShadowDescriptorSets[frameIndex]; }

		// --- Shadow pass uniform (set 0 in shadow pass) ---
		//     Same layout as camera uniform, but points at the light's UBO
		VkDescriptorSet AllocateShadowUniformSet(uint32_t frameIndex);
		void UpdateShadowUniformSet(uint32_t frameIndex, VkBuffer buffer, size_t size);
		VkDescriptorSet GetShadowUniformDescriptorSet(uint32_t frameIndex) { return m_ShadowUniformDescriptorSets[frameIndex]; }

	private:
		VulkanDevice* m_Device = nullptr;
		VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;

		// Layouts
		VkDescriptorSetLayout m_TextureSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_UniformSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_LightingSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_ShadowSetLayout = VK_NULL_HANDLE;
		// Shadow uniform reuses m_UniformSetLayout (same binding, different buffer)

		// Per-frame descriptor sets
		std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_TextureDescriptorSets{};
		std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_UniformDescriptorSets{};
		std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_LightingDescriptorSets{};
		std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_ShadowDescriptorSets{};
		std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_ShadowUniformDescriptorSets{};
	};
}