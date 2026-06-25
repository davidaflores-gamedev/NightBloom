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
		void FreeDescriptorSet(VkDescriptorSet set);

		// Layout creation
		VkDescriptorSetLayout CreateTextureSetLayout();
		VkDescriptorSetLayout CreateUniformSetLayout();
		VkDescriptorSetLayout CreateLightingSetLayout();
		VkDescriptorSetLayout CreateShadowSetLayout();
		VkDescriptorSetLayout CreateComputeStorageSetLayout();

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

		// --- Compute storage buffers ---
		VkDescriptorSet AllocateComputeStorageSet();
		void UpdateComputeStorageSet(VkDescriptorSet set, VkBuffer inputBuffer, VkDeviceSize inputSize,
			VkBuffer outputBuffer, VkDeviceSize outputSize);
		// Single-buffer variant (for read-write usage)
		void UpdateComputeStorageSet(VkDescriptorSet set, VkBuffer buffer, VkDeviceSize size, uint32_t binding = 0);
		VkDescriptorSetLayout GetComputeStorageSetLayout() const { return m_ComputeStorageSetLayout; }

		// --- Compute storage image (write target for noise generation) ---
		VkDescriptorSetLayout CreateComputeImageSetLayout();
		VkDescriptorSet AllocateComputeImageSet();
		void UpdateComputeImageSet(VkDescriptorSet set, VkImageView imageView);
		VkDescriptorSetLayout GetComputeImageSetLayout() const { return m_ComputeImageSetLayout; }

		// --- Heightmap sampler (set 4 in terrain pass — vertex-stage visible) ---
		VkDescriptorSetLayout CreateHeightmapSetLayout();
		VkDescriptorSet AllocateHeightmapSet();
		void UpdateHeightmapSet(VkDescriptorSet set, VulkanTexture* texture);
		VkDescriptorSetLayout GetHeightmapSetLayout() const { return m_HeightmapSetLayout; }

		// --- Firefly agent storage buffer (vertex+compute visible, single set, not per-frame) ---
		VkDescriptorSetLayout CreateFireflyStorageSetLayout();
		VkDescriptorSet AllocateFireflyStorageSet();
		void UpdateFireflyStorageSet(VkDescriptorSet set, VkBuffer buffer, VkDeviceSize size);
		VkDescriptorSetLayout GetFireflyStorageSetLayout() const { return m_FireflyStorageSetLayout; }

		// --- Firefly params UBO (compute-only, double-buffered like Lighting/Uniform) ---
		VkDescriptorSetLayout CreateFireflyParamsSetLayout();
		VkDescriptorSet AllocateFireflyParamsSet(uint32_t frameIndex);
		void UpdateFireflyParamsSet(uint32_t frameIndex, VkBuffer buffer, VkDeviceSize size);
		VkDescriptorSetLayout GetFireflyParamsSetLayout() const { return m_FireflyParamsSetLayout; }
		VkDescriptorSet GetFireflyParamsDescriptorSet(uint32_t frameIndex) { return m_FireflyParamsDescriptorSets[frameIndex]; }

		// --- Cloud set (compute set 0 in CloudRaymarch.comp): shape sampler
		//     (0), detail sampler (1), params UBO (2). Double-buffered per
		//     frame since binding 2 (the UBO) differs per frame, even though
		//     the two texture bindings don't change frame-to-frame.
		VkDescriptorSetLayout CreateCloudSetLayout();
		VkDescriptorSet AllocateCloudSet(uint32_t frameIndex);
		void UpdateCloudTextureBindings(uint32_t frameIndex, VulkanTexture* shapeTexture, VulkanTexture* detailTexture);
		void UpdateCloudParamsBinding(uint32_t frameIndex, VkBuffer buffer, VkDeviceSize size);
		VkDescriptorSetLayout GetCloudSetLayout() const { return m_CloudSetLayout; }
		VkDescriptorSet GetCloudDescriptorSet(uint32_t frameIndex) { return m_CloudDescriptorSets[frameIndex]; }

		// --- Foliage instance storage buffer (vertex-only visible, single
		//     set, not per-frame — one-shot generated like Terrain's heightmap,
		//     no compute dispatch involved) ---
		VkDescriptorSetLayout CreateFoliageStorageSetLayout();
		VkDescriptorSet AllocateFoliageStorageSet();
		void UpdateFoliageStorageSet(VkDescriptorSet set, VkBuffer buffer, VkDeviceSize size);
		VkDescriptorSetLayout GetFoliageStorageSetLayout() const { return m_FoliageStorageSetLayout; }

		// --- Cloud result sampler (set 1 in the graphics Clouds pass): the
		//     low-res raymarch output, sampled (with hardware bilinear
		//     upscale) by the simplified composite fragment shader. Single
		//     set, not per-frame — recreated whenever the result image is
		//     recreated (resize or resolution-scale change).
		VkDescriptorSetLayout CreateCloudResultSetLayout();
		VkDescriptorSet AllocateCloudResultSet();
		void UpdateCloudResultSet(VkDescriptorSet set, VulkanTexture* resultTexture);
		VkDescriptorSetLayout GetCloudResultSetLayout() const { return m_CloudResultSetLayout; }

	private:
		VulkanDevice* m_Device = nullptr;
		VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;

		// Layouts
		VkDescriptorSetLayout m_TextureSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_UniformSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_LightingSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_ShadowSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_ComputeStorageSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_ComputeImageSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_HeightmapSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_FireflyStorageSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_FireflyParamsSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_CloudSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_CloudResultSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_FoliageStorageSetLayout = VK_NULL_HANDLE;

		// Per-frame descriptor sets
		std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_TextureDescriptorSets{};
		std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_UniformDescriptorSets{};
		std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_LightingDescriptorSets{};
		std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_ShadowDescriptorSets{};
		std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_ShadowUniformDescriptorSets{};
		std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_FireflyParamsDescriptorSets{};
		std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_CloudDescriptorSets{};
	};
}