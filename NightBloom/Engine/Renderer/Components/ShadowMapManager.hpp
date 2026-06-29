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
#include "Engine/Renderer/Light.hpp"  // canonical NUM_CASCADES

namespace Nightbloom
{
	class VulkanDevice;
	class VulkanMemoryManager;
	class VulkanDescriptorManager;

	struct ShadowMapConfig
	{
		uint32_t resolution = 2048;           // Shadow map resolution (square)
		VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
		float depthBiasConstant = 0.0f;      // Constant depth bias
		float depthBiasSlope = 1.75f;         // Slope-scaled depth bias
		bool enablePCF = true;                // Enable PCF filtering in sampler

		// Terrain has much higher local slope variance than the test meshes
		// these defaults were tuned for, so it needs a larger bias to avoid
		// self-shadowing acne shaped like the underlying heightmap noise.
		float terrainDepthBiasConstant = 1.5f;
		float terrainDepthBiasSlope = 4.0f;
	};

	class ShadowMapManager
	{
	public:
		// Cascaded shadow maps: the shadow map is a depth 2D array, one layer per cascade
		// (NUM_CASCADES from Light.hpp). See .claude/CSM_DESIGN.md.
		ShadowMapManager() = default;
		~ShadowMapManager() = default;

		// Lifecycle
		bool Initialize(VulkanDevice* device, VulkanMemoryManager* memoryManager,
			VulkanDescriptorManager* descriptorManager, const ShadowMapConfig& config = {});
		void Cleanup();

		// Accessors
		VkRenderPass GetShadowRenderPass() const { return m_ShadowRenderPass; }
		// Per-cascade framebuffer (each targets one array layer). Defaults to cascade 0
		// so existing single-cascade call sites are unaffected.
		VkFramebuffer GetShadowFramebuffer(uint32_t cascade = 0) const { return m_ShadowFramebuffers[cascade]; }
		VkExtent2D GetShadowExtent() const { return { m_Config.resolution, m_Config.resolution }; }

		VkImage GetShadowMapImage() const { return m_ShadowMapImage; }
		// Sampling view spanning all cascade layers (sampler2DArrayShadow). Bound to set 3.
		VkImageView GetShadowMapView() const { return m_ShadowMapArrayView; }
		VkSampler GetShadowSampler() const { return m_ShadowSampler; }

		VkDescriptorSet GetShadowMapDescriptorSet(uint32_t frameIndex) const;

		// Configuration
		const ShadowMapConfig& GetConfig() const { return m_Config; }
		float GetDepthBiasConstant() const { return m_Config.depthBiasConstant; }
		float GetDepthBiasSlope() const { return m_Config.depthBiasSlope; }
		float GetTerrainDepthBiasConstant() const { return m_Config.terrainDepthBiasConstant; }
		float GetTerrainDepthBiasSlope() const { return m_Config.terrainDepthBiasSlope; }

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

		// Shadow map texture (depth 2D array, NUM_CASCADES layers)
		VkImage m_ShadowMapImage = VK_NULL_HANDLE;
		VkImageView m_ShadowMapArrayView = VK_NULL_HANDLE;            // 2D array view, all layers — for sampling (set 3)
		VkImageView m_ShadowMapLayerViews[NUM_CASCADES] = {};         // single-layer views — for framebuffer attachments
		void* m_ShadowMapAllocation = nullptr;  // VMA allocation handle

		// Shadow render pass and per-cascade framebuffers
		VkRenderPass m_ShadowRenderPass = VK_NULL_HANDLE;
		VkFramebuffer m_ShadowFramebuffers[NUM_CASCADES] = {};

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