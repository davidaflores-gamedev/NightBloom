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

	class RenderPassManager
	{
	public:
		RenderPassManager() = default;
		~RenderPassManager() = default;

		// Lifecycle
		bool Initialize(VkDevice device, VulkanSwapchain* swapchain);
		void Cleanup(VkDevice device);

		// Recreate framebuffers when swapchain changes
		bool RecreateFramebuffers(VkDevice device, VulkanSwapchain* swapchain);

		VkRenderPass GetMainRenderPass() const { return m_MainRenderPass; }
		size_t GetFramebufferCount() const { return m_Framebuffers.size(); }
		VkFramebuffer GetFramebuffer(uint32_t index) const
		{
			return (index < m_Framebuffers.size()) ? m_Framebuffers[index] : VK_NULL_HANDLE;
		}

		// Future: Support for multiple render passes (e.g. shadow pass, post-process pass)
		// VkRenderPass GetShadowRenderPass() const { return m_ShadowRenderPass; }
		// VkRenderPass GetPostProcessRenderPass() const { return m_PostProcessRenderPass; }

	private:
		// Main render pass	(color + optional depth)
		VkRenderPass m_MainRenderPass = VK_NULL_HANDLE;
		std::vector<VkFramebuffer> m_Framebuffers;

		// Future: Additional render passes
		// VkRenderPass m_ShadowRenderPass = VK_NULL_HANDLE;
		// VkRenderPass m_PostProcessRenderPass = VK_NULL_HANDLE;

		// Helper methods
		bool CreateMainRenderPass(VkDevice device, VkFormat colorFormat, bool hasDepth = false);
		bool CreateFramebuffers(VkDevice device, VulkanSwapchain* swapchain);
		void DestroyFramebuffers(VkDevice device);

		// Prevent copying
		RenderPassManager(const RenderPassManager&) = delete;
		RenderPassManager& operator=(const RenderPassManager&) = delete;
	};

} // namespace Nightbloom