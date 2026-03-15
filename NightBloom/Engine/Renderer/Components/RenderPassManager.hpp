//------------------------------------------------------------------------------
// RenderPassManager.hpp
//
// Manages render passes and framebuffers
// Handles creation and recreation when swapchain changes
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
		bool Initialize(VkDevice device, VulkanSwapchain* swapchain, VulkanMemoryManager* memoryManager);
		void Cleanup(VkDevice device);

		// Recreate framebuffers when swapchain changes
		bool RecreateFramebuffers(VkDevice device, VulkanSwapchain* swapchain);

		VkRenderPass GetMainRenderPass() const { return m_MainRenderPass; }
		size_t GetFramebufferCount() const { return m_Framebuffers.size(); }
		VkFramebuffer GetFramebuffer(uint32_t index) const
		{
			return (index < m_Framebuffers.size()) ? m_Framebuffers[index] : VK_NULL_HANDLE;
		}

		bool HasDepthBuffer() const { return m_HasDepth; }

		// Future: Support for multiple render passes (e.g. shadow pass, post-process pass)
		// VkRenderPass GetShadowRenderPass() const { return m_ShadowRenderPass; }
		// VkRenderPass GetPostProcessRenderPass() const { return m_PostProcessRenderPass; }

	private:
		// Main render pass	(color + optional depth)
		VkRenderPass m_MainRenderPass = VK_NULL_HANDLE;
		std::vector<VkFramebuffer> m_Framebuffers;

		// REVIEW: (Depth) new additions check to see if this is write later.
		bool m_HasDepth = false;
		VkImage m_DepthImage = VK_NULL_HANDLE;
		VkImageView m_DepthImageView = VK_NULL_HANDLE;
		VkFormat m_DepthFormat = VK_FORMAT_D32_SFLOAT;

		VulkanMemoryManager* m_MemoryManager = nullptr;
		struct ImageAllocationHandle;
		void* m_DepthAllocation = nullptr; // actually a vulkanmemorymanager image allocation

		// Future: Additional render passes
		// VkRenderPass m_ShadowRenderPass = VK_NULL_HANDLE;
		// VkRenderPass m_PostProcessRenderPass = VK_NULL_HANDLE;

		// Helper methods
		bool CreateMainRenderPass(VkDevice device, VkFormat colorFormat, bool hasDepth = false);
		bool CreateFramebuffers(VkDevice device, VulkanSwapchain* swapchain);
		void DestroyFramebuffers(VkDevice device);

		// Depth Buffer Helpers
		bool CreateDepthResources(VkDevice device, VkExtent2D extent);
		void DestroyDepthResources(VkDevice device);

		// Prevent copying
		RenderPassManager(const RenderPassManager&) = delete;
		RenderPassManager& operator=(const RenderPassManager&) = delete;
	};

} // namespace Nightbloom