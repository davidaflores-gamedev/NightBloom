//------------------------------------------------------------------------------
// VulkanSwapchain.hpp
//
// Manages the Vulkan swapchain for presenting rendered images to the window
//------------------------------------------------------------------------------

#pragma once

#include "VulkanCommon.hpp"
#include <vector>

namespace Nightbloom
{
	class VulkanSwapchain
	{
	public:
		VulkanSwapchain(VulkanDevice* device);
		~VulkanSwapchain();

		// Initializes the swapchain with the given window handle and dimensions
		bool Initialize(void* windowHandle, uint32_t width, uint32_t height);
		void Shutdown();

		// Recreate swapchain (for window resize)
		bool RecreateSwapchain(uint32_t width, uint32_t height);

		// Frame operations
		bool AcquireNextImage(uint32_t& imageIndex, VkSemaphore signalSemaphore = VK_NULL_HANDLE);
		bool Present(uint32_t imageIndex, VkSemaphore waitSemaphore = VK_NULL_HANDLE);

		// Getters
		VkSwapchainKHR GetSwapchain() const { return m_Swapchain; }
		VkFormat GetImageFormat() const { return m_SwapchainImageFormat; }
		VkExtent2D GetExtent() const { return m_SwapchainExtent; }
		uint32_t GetImageCount() const { return static_cast<uint32_t>(m_SwapchainImages.size()); }

		const std::vector<VkImage>& GetImages() const { return m_SwapchainImages; }
		const std::vector<VkImageView>& GetImageViews() const { return m_SwapchainImageViews; }

		// Check if swapchain needs to be recreated
		bool IsOutOfDate() const { return m_OutOfDate; }

	private:
		// Helper structs
		struct SwapChainSupportDetails
		{
			VkSurfaceCapabilitiesKHR capabilities;
			std::vector<VkSurfaceFormatKHR> formats;
			std::vector<VkPresentModeKHR> presentModes;
		};

		// Creation steps
		bool CreateSurface();
		bool CreateSwapChain();
		bool CreateImageViews();
		void CleanupSwapchain();

		// Helper functions
		SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);
		VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
		VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
		VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
		uint32_t ChooseImageCount(const VkSurfaceCapabilitiesKHR& capabilities);

	private:
		// Device reference
		VulkanDevice* m_Device = nullptr;

		// window info
		void* m_WindowHandle = nullptr;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		// surface 
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;

		// swapchain
		VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
		std::vector<VkImage> m_SwapchainImages;
		std::vector<VkImageView> m_SwapchainImageViews;
		VkFormat m_SwapchainImageFormat = VK_FORMAT_UNDEFINED;
		VkExtent2D m_SwapchainExtent = {0, 0};

		// state
		bool m_Initialized = false;
		bool m_OutOfDate = false;

		// Configuation
		bool m_EnableVSync = false; // Toggle V-Sync
		uint32_t m_DesiredImageCount = 3;  // Triple buffering preferred
	};
}

