//------------------------------------------------------------------------------
// UIManager.hpp
//
// Manages ImGui integration
// Handles UI rendering and descriptor pool management
//------------------------------------------------------------------------------
#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Nightbloom
{
	// Forward declarations
	class VulkanDevice;

	class UIManager
	{
	public:
		UIManager() = default;
		~UIManager() = default;

		// Lifecycle
		bool Initialize(VulkanDevice* device, void* windowHandle, VkRenderPass renderPass, uint32_t imageCount);
		void Cleanup(VkDevice device);

		// Frame operations
		void BeginFrame();
		void EndFrame();

		// Render ImGui draw data to command buffer
		void Render(VkCommandBuffer commandBuffer);

		// status
		bool IsInitialized() const { return m_Initialized; }
		
	private:
		VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
		bool m_Initialized = false;

		// helper methods
		bool CreateDescriptorPool(VkDevice device);

		// prevent copying
		UIManager(const UIManager&) = delete;
		UIManager& operator=(const UIManager&) = delete;
	};

} // namespace Nightbloom