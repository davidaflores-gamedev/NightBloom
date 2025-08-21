//------------------------------------------------------------------------------
// VulkanDevice.hpp
//
// Vulkan device management - handles instance, physical and logical devices
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#pragma once

#include "Engine/Renderer/RenderDevice.hpp"
#include "VulkanCommon.hpp"  // Replaces all the platform-specific Vulkan includes


namespace Nightbloom
{
	class VulkanSwapchain;

	class VulkanDevice : public RenderDevice
	{
	public:
		VulkanDevice();
		~VulkanDevice() override;

		// Main nitialization
		bool Initialize(void* windowHandle, uint32_t width, uint32_t height) override;
		void Shutdown() override;

		// Resource Creation, implemntation will be added later
		Buffer* CreateBuffer(const BufferDesc& desc) override;
		Texture* CreateTexture(const TextureDesc& desc) override;
		Shader* CreateShader(const ShaderDesc& desc) override;
		Pipeline* CreatePipeline(const PipelineDesc& desc) override;
		CommandBuffer* CreateCommandBuffer() override;

		// Resource destruction, implementation will be added later
		void DestroyBuffer(Buffer* buffer) override;
		void DestroyTexture(Texture* texture) override;
		void DestroyShader(Shader* shader) override;
		void DestroyPipeline(Pipeline* pipeline) override;
		void DestroyCommandBuffer(CommandBuffer* commandBuffer) override;

		// Frame operations, implementation will be added later
		void BeginFrame() override {}
		void EndFrame() override {}
		void Present() override {}

		// Command submission, implementation will be added later
		void SubmitCommandBuffer(CommandBuffer* commandBuffer) override;

		// Sync operations, implementation will be added later
		void WaitForIdle() override;

		// Capabilities query
		size_t GetMinUniformBufferAlignment() const override;

		// Getters for Vulkan-specific properties
		VkInstance GetInstance() const { return m_Instance; }
		VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
		VkDevice GetDevice() const { return m_Device; }
		VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
		VkQueue GetPresentQueue() const { return m_PresentQueue; }
		uint32_t GetGraphicsQueueFamily() const { return m_QueueFamilies.graphicsFamily.value_or(0); }
		uint32_t GetPresentQueueFamily() const { return m_QueueFamilies.presentFamily.value_or(0); }
		// Queue family indices, needed by swapchain and other components
		struct QueueFamilyIndices
		{
			std::optional<uint32_t> graphicsFamily;
			std::optional<uint32_t> presentFamily;

			bool IsComplete() const
			{
				return graphicsFamily.has_value() && presentFamily.has_value();
			}
		};

		QueueFamilyIndices GetQueueFamilyIndices() const { return m_QueueFamilies; }

	private:
		// Step 1: Create Vulkan instance
		bool CreateInstance();

		// Step 2: Setup debug messenger (only in debug builds)
		bool SetupDebugMessenger();

		// Step 3: Select physical device
		bool PickPhysicalDevice();

		// Step 4: Create logical device and queues
		bool CreateLogicalDevice();

		// Helper functions
		bool CheckValidationLayerSupport() const;
		std::vector<const char*> GetRequiredExtensions() const;
		bool IsDeviceSuitable(VkPhysicalDevice device) const;
		QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
		bool CheckDeviceExtensionSupport(VkPhysicalDevice device) const;


		// Debug messenger callback
		static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT messageType, 
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, 
			void* pUserData);

		void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) const;

		bool SupportsFeature(const std::string& feature) const;

	private:
		// Core Vulkan objects
		VkInstance m_Instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_Device = VK_NULL_HANDLE;

		// Queues
		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		VkQueue m_PresentQueue = VK_NULL_HANDLE;
		QueueFamilyIndices m_QueueFamilies;

		// Surface (created by the swapchain, but device needs to know about it)
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
		friend class VulkanSwapchain;


		// Configuration
		const std::vector<const char*> m_ValidationLayers = {
			"VK_LAYER_KHRONOS_validation"
		};

		const std::vector<const char*> m_DeviceExtensions = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME
		};

#ifdef NDEBUG
		const bool m_EnableValidationLayers = false; // Disable validation layers in release builds
#else
		const bool m_EnableValidationLayers = true; // Enable validation layers in debug builds
#endif

		void* m_WindowHandle = nullptr; // Handle to the window for presentation
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
	};

}
