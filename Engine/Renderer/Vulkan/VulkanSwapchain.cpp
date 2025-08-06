//------------------------------------------------------------------------------
// VulkanSwapchain.cpp
//
// Vulkan swapchain implementation
//------------------------------------------------------------------------------

#include "VulkanSwapchain.hpp"
#include "Core/Logger/Logger.hpp"
#include <algorithm>
#include <limits>

namespace Nightbloom
{
	VulkanSwapchain::VulkanSwapchain(VulkanDevice* device)
		: m_Device(device)
	{
		LOG_INFO("VulkanSwapchain created");
	}

	VulkanSwapchain::~VulkanSwapchain()
	{
		Shutdown();
		LOG_INFO("VulkanSwapchain destroyed");
	}

	bool VulkanSwapchain::Initialize(void* windowHandle, uint32_t width, uint32_t height)
	{
		LOG_INFO("Initializing VulkanSwapchain with window handle: {}, width: {}, height: {}", windowHandle, width, height);

		//if (!m_Device || !windowHandle || width == 0 || height == 0)
		//{
		//	LOG_ERROR("Invalid parameters for VulkanSwapchain initialization");
		//	return false;
		//}

		m_WindowHandle = windowHandle;
		m_SwapchainExtent.width = width;
		m_SwapchainExtent.height = height;

		// Step 1: Create the Vulkan surface
		if (!CreateSurface())
		{
			LOG_ERROR("Failed to create Vulkan surface");
			return false;
		}
		LOG_INFO("Vulkan surface created successfully");

		// Now we need to verify that our selected present queue actually supports the surface
		VkBool32 presentSupport = false;
		auto queueFamilies = m_Device->GetQueueFamilyIndices();
		vkGetPhysicalDeviceSurfaceSupportKHR(
			m_Device->GetPhysicalDevice(), 
			queueFamilies.presentFamily.value(), 
			m_Surface, 
			&presentSupport
		);

		if (!presentSupport)
		{
			LOG_ERROR("Selected present queue does not support the surface");
			return false;
		}

		// Step 2: Create the swapchain
		if (!CreateSwapChain()) {
			LOG_ERROR("Failed to create swapchain");
			return false;
		}
		LOG_INFO("Swapchain created successfully");

		// Step 3: Create image views
		if (!CreateImageViews()) {
			LOG_ERROR("Failed to create image views");
			return false;
		}
		LOG_INFO("Image views created successfully");

		m_Initialized = true;
		LOG_INFO("VulkanSwapchain initialized successfully");
		return true;
	}

	void VulkanSwapchain::Shutdown()
	{
		if (!m_Initialized) return;

		LOG_INFO("Shutting down VulkanSwapchain");

		// wait for device to be idle before destroying resources
		m_Device->WaitForIdle();

		// Cleanup swapchain resources
		CleanupSwapchain();

		// Destroy the surface
		if (m_Surface != VK_NULL_HANDLE) {
			vkDestroySurfaceKHR(m_Device->GetInstance(), m_Surface, nullptr);
			m_Surface = VK_NULL_HANDLE;
			LOG_INFO("Vulkan surface destroyed");
		}

		m_Initialized = false;
	}


	bool VulkanSwapchain::CreateSurface()
	{
		//Platform-specific surface creation
#ifdef NIGHTBLOOM_PLATFORM_WINDOWS
		VkWin32SurfaceCreateInfoKHR createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		createInfo.hinstance = GetModuleHandle(nullptr);
		createInfo.hwnd = static_cast<HWND>(m_WindowHandle);

		VkResult result = vkCreateWin32SurfaceKHR(m_Device->GetInstance(), &createInfo, nullptr, &m_Surface);

		if (result != VK_SUCCESS) {
			//LOG_ERROR("Failed to create Win32 surface: {}", result);
			return false;
		}
#else
#error "Unsupported platform for Vulkan surface creation"
#endif

		LOG_INFO("Vulkan surface created successfully");

		m_Device->m_Surface = m_Surface;
		return true;
	}

	bool VulkanSwapchain::CreateSwapChain()
	{
		// Query swapchain support details
		auto swapChainSupport = QuerySwapChainSupport(m_Device->GetPhysicalDevice());

		// choose settings
		VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
		VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
		VkExtent2D extent = ChooseSwapExtent(swapChainSupport.capabilities);
		uint32_t imageCount = ChooseImageCount(swapChainSupport.capabilities);

		// print configuration
		LOG_INFO("Swapchain configuration:");
		//LOG_INFO("  Format: {}", surfaceFormat.format);
		//LOG_INFO("  Color Space: {}", surfaceFormat.colorSpace);
		//LOG_INFO("  Present Mode: {}", presentMode);
		LOG_INFO("  Extent: {}x{}", extent.width, extent.height);
		LOG_INFO("  Image Count: {}", imageCount);

		// Create Swapchain
		VkSwapchainCreateInfoKHR createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = m_Surface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1; // Single-layer images, always use 1 unless sterioscopic 3d
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		auto queueFamilies = m_Device->GetQueueFamilyIndices();
		uint32_t queueFamilyIndices[] = { 
			queueFamilies.graphicsFamily.value(), 
			queueFamilies.presentFamily.value() 
		};

		if (queueFamilies.graphicsFamily != queueFamilies.presentFamily)
		{
			// Images shared between queues
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = queueFamilyIndices;
		}
		else
		{
			// Images owned by one queue
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			createInfo.queueFamilyIndexCount = 0; // Optional
			createInfo.pQueueFamilyIndices = nullptr; // Optional
		}

		createInfo.preTransform = swapChainSupport.capabilities.currentTransform; // No transformation
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // Opaque composite alpha
		createInfo.presentMode = presentMode; // Chosen present mode
		createInfo.clipped = VK_TRUE; // Don't render pixels behind other windows
		createInfo.oldSwapchain = VK_NULL_HANDLE; // No previous swapchain/ used when recreating

		VkResult result = vkCreateSwapchainKHR(
			m_Device->GetDevice(),
			&createInfo,
			nullptr,
			&m_Swapchain
		);

		if (result != VK_SUCCESS) {
			//LOG_ERROR("Failed to create swapchain: {}", result);
			return false;
		}

		// Get swapchain images
		vkGetSwapchainImagesKHR(
			m_Device->GetDevice(),
			m_Swapchain,
			&imageCount,
			nullptr
		);
		m_SwapchainImages.resize(imageCount);
		vkGetSwapchainImagesKHR(
			m_Device->GetDevice(),
			m_Swapchain,
			&imageCount,
			m_SwapchainImages.data()
		);

		m_SwapchainImageFormat = surfaceFormat.format;
		m_SwapchainExtent = extent;

		LOG_INFO("Swapchain created successfully with {} images", imageCount);

		return true;
	}

	bool VulkanSwapchain::CreateImageViews()
	{
		m_SwapchainImageViews.resize(m_SwapchainImages.size());

		for (size_t i = 0; i < m_SwapchainImages.size(); ++i)
		{
			VkImageViewCreateInfo createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			createInfo.image = m_SwapchainImages[i];
			createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; // 2D images
			createInfo.format = m_SwapchainImageFormat;

			// Component mapping (can swizzle channels)
			createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

			createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; // Color aspect
			createInfo.subresourceRange.baseMipLevel = 0;
			createInfo.subresourceRange.levelCount = 1; // No mipmaps
			createInfo.subresourceRange.baseArrayLayer = 0;
			createInfo.subresourceRange.layerCount = 1; // Single layer

			VkResult result = vkCreateImageView(
				m_Device->GetDevice(),
				&createInfo,
				nullptr,
				&m_SwapchainImageViews[i]
			);

			if (result != VK_SUCCESS) {
				//LOG_ERROR("Failed to create image view for swapchain image {}: {}", i, result);
				return false;
			}
		}

		LOG_INFO("Created {} swapchain image views", m_SwapchainImageViews.size());
		return true;
	}

	void VulkanSwapchain::CleanupSwapchain()
	{
		// Destroy image views
		for (auto imageView : m_SwapchainImageViews)
		{
			if (imageView != VK_NULL_HANDLE)
			{
				vkDestroyImageView(m_Device->GetDevice(), imageView, nullptr);
			}
		}
		m_SwapchainImageViews.clear();
		LOG_INFO("Destroyed swapchain image views");

		// Destroy swapchain
		if (m_Swapchain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(m_Device->GetDevice(), m_Swapchain, nullptr);
			m_Swapchain = VK_NULL_HANDLE;
			LOG_INFO("Destroyed swapchain");
		}

		// Clear swapchain images
		m_SwapchainImages.clear();
	}

	
	bool VulkanSwapchain::RecreateSwapchain(uint32_t width, uint32_t height)
	{
		LOG_INFO("Recreating swapchain with width: {}, height: {}", width, height);

		m_Width = width;
		m_Height = height;

		// Wait for device to be idle before recreating swapchain
		m_Device->WaitForIdle();

		// Cleanup old swapchain resources
		CleanupSwapchain();

		// Recreate swapchain 
		if (!CreateSwapChain())
		{
			LOG_ERROR("Failed to recreate swapchain");
			return false;
		}

		// Recreate image views
		if (!CreateImageViews())
		{
			LOG_ERROR("Failed to recreate image views");
			return false;
		}

		LOG_INFO("Swapchain recreated successfully with {} images", m_SwapchainImages.size());

		m_OutOfDate = false; // Reset out-of-date flag
		return true;
	}

	bool VulkanSwapchain::AcquireNextImage(uint32_t& imageIndex, VkSemaphore signalSemaphore)
	{
		VkResult result = vkAcquireNextImageKHR(
			m_Device->GetDevice(),
			m_Swapchain,
			(std::numeric_limits<uint64_t>::max)(), // Wait indefinitely
			signalSemaphore, // Optional semaphore to signal when image is ready
			VK_NULL_HANDLE, // Optional fence
			&imageIndex
		);

		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			m_OutOfDate = true;
			return false;
		}
		else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			//LOG_ERROR("Failed to acquire swapchain image: {}", result);
			return false;
		}

		return true;
	}

	bool VulkanSwapchain::Present(uint32_t imageIndex, VkSemaphore waitSemaphore)
	{
		VkPresentInfoKHR presentInfo = {};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

		if (waitSemaphore != VK_NULL_HANDLE) {
			presentInfo.waitSemaphoreCount = 1;
			presentInfo.pWaitSemaphores = &waitSemaphore;
		}
		
		VkSwapchainKHR swapChains[] = { m_Swapchain };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapChains;
		presentInfo.pImageIndices = &imageIndex;
		presentInfo.pResults = nullptr; // Optional

		VkResult result = vkQueuePresentKHR(m_Device->GetPresentQueue(), &presentInfo);

		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
		{
			m_OutOfDate = true;
			// trysometime check if return false is better here 
		}
		else if (result != VK_SUCCESS) {
			//LOG_ERROR("Failed to present swapchain image: {}", result);
			return false;
		}

		return true;
	}

	VulkanSwapchain::SwapChainSupportDetails VulkanSwapchain::QuerySwapChainSupport(VkPhysicalDevice device)
	{
		SwapChainSupportDetails details;

		// Get surface capabilities
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_Surface, &details.capabilities);

		// Get surface formats
		uint32_t formatCount;
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, nullptr);
		if (formatCount != 0) {
			details.formats.resize(formatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, details.formats.data());
		}

		// Get present modes
		uint32_t presentModeCount;
		vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, nullptr);
		if (presentModeCount != 0) {
			details.presentModes.resize(presentModeCount);
			vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, details.presentModes.data());
		}

		return details;
	}

	VkSurfaceFormatKHR VulkanSwapchain::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
	{
		// Prefer SRGB color space for proper gamma correction
		for (const auto& availableFormat : availableFormats)
		{
			if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
				availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
				return availableFormat;
		}

		// otherwise, return the first available format
		return availableFormats[0];
	}

	VkPresentModeKHR VulkanSwapchain::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
	{
		if (!m_EnableVSync)
		{
			// Prefer triple buffering without V-Sync
			for (const auto& availablePresentMode : availablePresentModes)
			{
				
				if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
				{
					LOG_INFO("Using Mailbox present mode (triple buffering, no vsync)");
					return availablePresentMode;
				}
			}

			// Try immediate mode (no vsync)
			for (const auto& availablePresentMode : availablePresentModes)
			{
				if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR)
				{
					LOG_INFO("Using Immediate present mode (no vsync)");
					return availablePresentMode;
				}
			}
		}

		// Fallback to FIFO mode (V-Sync)
		LOG_INFO("Using FIFO present mode (V-Sync)");
		return VK_PRESENT_MODE_FIFO_KHR;
	}

	VkExtent2D VulkanSwapchain::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities)
	{
		// If vulkan tells us the extent, use it
		if (capabilities.currentExtent.width != (std::numeric_limits<uint32_t>::max)())
		{
			return capabilities.currentExtent;
		}
		else
		{
			// Otherwise use the window size
			VkExtent2D actualExtent = { m_Width, m_Height };

			// Clamp to min/max values
			actualExtent.width = std::clamp(
				actualExtent.width,
				capabilities.minImageExtent.width,
				capabilities.maxImageExtent.width
			);
			actualExtent.height = std::clamp(
				actualExtent.height,
				capabilities.minImageExtent.height,
				capabilities.maxImageExtent.height
			);

			LOG_INFO("Using custom swap extent: {}x{}", actualExtent.width, actualExtent.height);
			return actualExtent;
		}
	}

	uint32_t VulkanSwapchain::ChooseImageCount(const VkSurfaceCapabilitiesKHR& capabilities)
	{
		uint32_t imageCount = (std::max)(m_DesiredImageCount, capabilities.minImageCount);
		// Make sure we don't exceed maximum (0 means no limit)
		if (capabilities.maxImageCount > 0) {
			imageCount = (std::min)(imageCount, capabilities.maxImageCount);
		}

		LOG_INFO("Choosing image count: {}", imageCount);
		return imageCount;
	}
}
