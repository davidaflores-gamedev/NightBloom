//------------------------------------------------------------------------------
// RenderPassManager.hpp
//
// Manages render passes and framebuffers
// Handles creation and recreation when swapchain changes
//
// Two passes: the scene pass renders all normal geometry into an offscreen
// color texture (not the swapchain) so a second post-process pass can
// sample it, run FXAA, and write the result into the actual swapchain
// image. See Renderer::RecordCommandBuffer/RecordPostProcessPass.
//------------------------------------------------------------------------------
#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

namespace Nightbloom
{
	// Forward Declarations
	class VulkanSwapchain;
	class VulkanMemoryManager;

	class RenderPassManager
	{
	public:
		RenderPassManager() = default;
		~RenderPassManager() = default;

		// Lifecycle
		//
		// sampleCount selects MSAA for the offscreen scene pass (color + depth
		// multisampled, resolved to the single-sample scene-color texture the
		// post-process pass samples). VK_SAMPLE_COUNT_1_BIT keeps the original
		// non-MSAA path. The caller is expected to clamp this to what the device
		// supports (see VulkanDevice::GetMaxUsableSampleCount).
		bool Initialize(VkDevice device, VulkanSwapchain* swapchain, VulkanMemoryManager* memoryManager,
			VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT);
		void Cleanup(VkDevice device);

		// Recreate framebuffers when swapchain changes
		bool RecreateFramebuffers(VkDevice device, VulkanSwapchain* swapchain);

		// Scene pass — all normal geometry renders here, into the offscreen
		// scene-color texture (not the swapchain).
		VkRenderPass GetSceneRenderPass() const { return m_SceneRenderPass; }
		VkFramebuffer GetSceneFramebuffer() const { return m_SceneFramebuffer; }
		VkImageView GetSceneColorImageView() const { return m_SceneColorImageView; }
		VkSampler GetSceneColorSampler() const { return m_SceneColorSampler; }

		// Reflection pass — the scene re-rendered from a mirror-flipped camera
		// (across the water plane) into a second offscreen color texture, sampled
		// by the water surface. Single-sample (no MSAA), own depth. Same offscreen
		// pattern as the scene-color target above.
		VkRenderPass GetReflectionRenderPass() const { return m_ReflectionRenderPass; }
		VkFramebuffer GetReflectionFramebuffer() const { return m_ReflectionFramebuffer; }
		VkImageView GetReflectionColorImageView() const { return m_ReflectionColorImageView; }
		VkSampler GetReflectionColorSampler() const { return m_ReflectionColorSampler; }
		VkExtent2D GetReflectionExtent() const { return m_ReflectionExtent; }

		// Post-process pass — samples the scene-color texture and writes the
		// actual swapchain image (one framebuffer per swapchain image, like
		// the scene pass's framebuffers used to be before this offscreen split).
		VkRenderPass GetPostProcessRenderPass() const { return m_PostProcessRenderPass; }
		VkFramebuffer GetPostProcessFramebuffer(uint32_t index) const
		{
			return (index < m_PostProcessFramebuffers.size()) ? m_PostProcessFramebuffers[index] : VK_NULL_HANDLE;
		}

		// Bloom — two half-res HDR ping-pong targets sharing one color-only render pass.
		// Pipeline: bright-extract (scene -> A), blur H (A -> B), blur V (B -> A); the
		// post-process pass then composites A into the tonemap. See Renderer::RecordBloomPass.
		VkRenderPass GetBloomRenderPass() const { return m_BloomRenderPass; }
		VkFramebuffer GetBloomFramebufferA() const { return m_BloomFramebufferA; }
		VkFramebuffer GetBloomFramebufferB() const { return m_BloomFramebufferB; }
		VkImageView GetBloomImageViewA() const { return m_BloomImageViewA; }
		VkImageView GetBloomImageViewB() const { return m_BloomImageViewB; }
		VkSampler GetBloomSampler() const { return m_BloomSampler; }
		VkExtent2D GetBloomExtent() const { return m_BloomExtent; }

		bool HasDepthBuffer() const { return m_HasDepth; }

		// MSAA sample count of the scene pass (1 = no MSAA). Scene-pass
		// pipelines must be created with a matching rasterizationSamples.
		VkSampleCountFlagBits GetSampleCount() const { return m_SampleCount; }

	private:
		// Scene render pass (color into offscreen texture + optional depth)
		VkRenderPass m_SceneRenderPass = VK_NULL_HANDLE;
		VkFramebuffer m_SceneFramebuffer = VK_NULL_HANDLE;

		VkSampleCountFlagBits m_SampleCount = VK_SAMPLE_COUNT_1_BIT;

		// Offscreen scene-color target — owned directly (raw Vulkan calls via
		// VulkanMemoryManager), same style as the depth buffer below, not a
		// VulkanTexture (this file has never depended on that class).
		// When MSAA is on this is the single-sample RESOLVE target (sampled by
		// the post-process pass); the multisampled color the subpass renders
		// into is m_SceneColorMSImage below.
		VkImage m_SceneColorImage = VK_NULL_HANDLE;
		VkImageView m_SceneColorImageView = VK_NULL_HANDLE;
		VkSampler m_SceneColorSampler = VK_NULL_HANDLE;
		VkFormat m_SceneColorFormat = VK_FORMAT_UNDEFINED;
		void* m_SceneColorAllocation = nullptr; // a VulkanMemoryManager::ImageAllocation*

		// Multisampled color attachment (only created when m_SampleCount > 1) —
		// transient, never sampled, resolved into m_SceneColorImage each pass.
		VkImage m_SceneColorMSImage = VK_NULL_HANDLE;
		VkImageView m_SceneColorMSImageView = VK_NULL_HANDLE;
		void* m_SceneColorMSAllocation = nullptr;

		// Reflection target — a second offscreen color target rendered with a
		// mirror-flipped camera and sampled by the water surface. Its render
		// pass MUST be format/sample-count compatible with the scene render pass
		// because it reuses the scene's Mesh/Terrain/Foliage pipelines, so it
		// mirrors the scene target's structure exactly (same MSAA count, depth,
		// and an MSAA resolve target when MSAA is on). m_ReflectionColorImageView
		// is the single-sample SAMPLED target (the resolve target under MSAA).
		VkRenderPass m_ReflectionRenderPass = VK_NULL_HANDLE;
		VkFramebuffer m_ReflectionFramebuffer = VK_NULL_HANDLE;
		VkExtent2D m_ReflectionExtent = { 0, 0 };
		VkImage m_ReflectionColorImage = VK_NULL_HANDLE;
		VkImageView m_ReflectionColorImageView = VK_NULL_HANDLE;
		VkSampler m_ReflectionColorSampler = VK_NULL_HANDLE;
		void* m_ReflectionColorAllocation = nullptr;
		VkImage m_ReflectionColorMSImage = VK_NULL_HANDLE;       // multisampled (MSAA only)
		VkImageView m_ReflectionColorMSImageView = VK_NULL_HANDLE;
		void* m_ReflectionColorMSAllocation = nullptr;
		VkImage m_ReflectionDepthImage = VK_NULL_HANDLE;
		VkImageView m_ReflectionDepthImageView = VK_NULL_HANDLE;
		void* m_ReflectionDepthAllocation = nullptr;

		// Post-process render pass (color only, writes the swapchain image)
		VkRenderPass m_PostProcessRenderPass = VK_NULL_HANDLE;
		std::vector<VkFramebuffer> m_PostProcessFramebuffers;

		// Bloom ping-pong targets — half-res, HDR (= m_SceneColorFormat), single-sample.
		// One render pass (color-only, final layout SHADER_READ) reused for all three
		// bloom sub-passes; A and B alternate as render target / sampled input.
		VkRenderPass m_BloomRenderPass = VK_NULL_HANDLE;
		VkExtent2D m_BloomExtent = { 0, 0 };
		VkImage m_BloomImageA = VK_NULL_HANDLE;
		VkImageView m_BloomImageViewA = VK_NULL_HANDLE;
		void* m_BloomAllocationA = nullptr;
		VkImage m_BloomImageB = VK_NULL_HANDLE;
		VkImageView m_BloomImageViewB = VK_NULL_HANDLE;
		void* m_BloomAllocationB = nullptr;
		VkSampler m_BloomSampler = VK_NULL_HANDLE;
		VkFramebuffer m_BloomFramebufferA = VK_NULL_HANDLE;
		VkFramebuffer m_BloomFramebufferB = VK_NULL_HANDLE;

		bool m_HasDepth = false;
		VkImage m_DepthImage = VK_NULL_HANDLE;
		VkImageView m_DepthImageView = VK_NULL_HANDLE;
		VkFormat m_DepthFormat = VK_FORMAT_D32_SFLOAT;

		VulkanMemoryManager* m_MemoryManager = nullptr;
		struct ImageAllocationHandle;
		void* m_DepthAllocation = nullptr; // actually a vulkanmemorymanager image allocation

		// Helper methods
		bool CreateSceneRenderPass(VkDevice device, VkFormat colorFormat, bool hasDepth);
		bool CreateSceneColorResources(VkDevice device, VkFormat colorFormat, VkExtent2D extent);
		void DestroySceneColorResources(VkDevice device);
		bool CreateSceneFramebuffer(VkDevice device, VkExtent2D extent);
		void DestroySceneFramebuffer(VkDevice device);

		bool CreatePostProcessRenderPass(VkDevice device, VkFormat colorFormat);
		bool CreatePostProcessFramebuffers(VkDevice device, VulkanSwapchain* swapchain);
		void DestroyPostProcessFramebuffers(VkDevice device);

		// Bloom helpers. CreateBloomResources builds both images/views, the shared sampler,
		// and both framebuffers at half of `extent`. The render pass is created once.
		bool CreateBloomRenderPass(VkDevice device, VkFormat colorFormat);
		bool CreateBloomResources(VkDevice device, VkFormat colorFormat, VkExtent2D fullExtent);
		void DestroyBloomResources(VkDevice device);

		// Reflection target helpers (single-sample color + depth, sampled by water)
		bool CreateReflectionRenderPass(VkDevice device, VkFormat colorFormat);
		bool CreateReflectionResources(VkDevice device, VkFormat colorFormat, VkExtent2D extent);
		void DestroyReflectionResources(VkDevice device);
		bool CreateReflectionFramebuffer(VkDevice device, VkExtent2D extent);
		void DestroyReflectionFramebuffer(VkDevice device);

		// Depth Buffer Helpers
		bool CreateDepthResources(VkDevice device, VkExtent2D extent);
		void DestroyDepthResources(VkDevice device);

		// Prevent copying
		RenderPassManager(const RenderPassManager&) = delete;
		RenderPassManager& operator=(const RenderPassManager&) = delete;
	};

} // namespace Nightbloom
