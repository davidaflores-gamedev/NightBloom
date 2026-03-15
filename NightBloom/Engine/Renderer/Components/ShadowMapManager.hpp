//------------------------------------------------------------------------------
// ShadowMapManager.hpp
//
// Manages shadow map resources including:
// - Shadow map texture (depth-only)
// - Shadow render pass
// - Shadow framebuffer
// - Shadow sampler with comparison
//------------------------------------------------------------------------------
#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <cstdint>

namespace Nightbloom
{
	class VulkanDevice;
	class VulkanMemoryManager;
	class VulkanDescriptorManager;

	struct ShadowMapConfig
	{
		uint32_t resolution = 2048;           // Shadow map resolution (square)
		VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
		float depthBiasConstant = 1.25f;      // Constant depth bias
		float depthBiasSlope = 1.75f;         // Slope-scaled depth bias
		bool enablePCF = true;                // Enable PCF filtering in sampler
	};

	class ShadowMapManager
	{
	public:
		ShadowMapManager() = default;
		~ShadowMapManager() = default;

		// Lifecycle
		bool Initialize(VulkanDevice* device, VulkanMemoryManager* memoryManager,
			VulkanDescriptorManager* descriptorManager, const ShadowMapConfig& config = {});
		void Cleanup();

		// Accessors
		VkRenderPass GetShadowRenderPass() const { return m_ShadowRenderPass; }
		VkFramebuffer GetShadowFramebuffer() const { return m_ShadowFramebuffer; }
		VkExtent2D GetShadowExtent() const { return { m_Config.resolution, m_Config.resolution }; }

		VkImage GetShadowMapImage() const { return m_ShadowMapImage; }
		VkImageView GetShadowMapView() const { return m_ShadowMapView; }
		VkSampler GetShadowSampler() const { return m_ShadowSampler; }

		VkDescriptorSet GetShadowMapDescriptorSet(uint32_t frameIndex) const;

		// Configuration
		const ShadowMapConfig& GetConfig() const { return m_Config; }
		float GetDepthBiasConstant() const { return m_Config.depthBiasConstant; }
		float GetDepthBiasSlope() const { return m_Config.depthBiasSlope; }

		// Resize shadow map (e.g., quality settings change)
		bool Resize(uint32_t newResolution);

	private:
		bool CreateShadowMapTexture();
		bool CreateShadowRenderPass();
		bool CreateShadowFramebuffer();
		bool CreateShadowSampler();
		bool CreateDescriptorSets();

		void DestroyResources();

		// Vulkan resources
		VulkanDevice* m_Device = nullptr;
		VulkanMemoryManager* m_MemoryManager = nullptr;
		VulkanDescriptorManager* m_DescriptorManager = nullptr;

		ShadowMapConfig m_Config;

		// Shadow map texture
		VkImage m_ShadowMapImage = VK_NULL_HANDLE;
		VkImageView m_ShadowMapView = VK_NULL_HANDLE;
		void* m_ShadowMapAllocation = nullptr;  // VMA allocation handle

		// Shadow render pass and framebuffer
		VkRenderPass m_ShadowRenderPass = VK_NULL_HANDLE;
		VkFramebuffer m_ShadowFramebuffer = VK_NULL_HANDLE;

		// Shadow sampler (with depth comparison for PCF)
		VkSampler m_ShadowSampler = VK_NULL_HANDLE;

		// Descriptor sets for shadow map sampling (one per frame in flight)
		static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
		VkDescriptorSet m_ShadowDescriptorSets[MAX_FRAMES_IN_FLIGHT] = {};

		// Prevent copying
		ShadowMapManager(const ShadowMapManager&) = delete;
		ShadowMapManager& operator=(const ShadowMapManager&) = delete;
	};
} // namespace Nightbloom