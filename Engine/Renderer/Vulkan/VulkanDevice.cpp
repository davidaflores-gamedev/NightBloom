//------------------------------------------------------------------------------
// VulkanDevice.cpp
//
// Vulkan device management implementation
// Each function is thoroughly commented to help you understand Vulkan
//------------------------------------------------------------------------------

#include "Core/Platform.hpp"  
#include "VulkanDevice.hpp"
#include "Core/Logger/Logger.hpp"
#include <set>

namespace Nightbloom
{
	VulkanDevice::VulkanDevice()
	{
		LOG_INFO("VulkanDevice created");
	}

	VulkanDevice::~VulkanDevice()
	{
		LOG_INFO("VulkanDevice destroyed");
		// we'll see if Shutdown();
	}

	bool VulkanDevice::Initialize(void* windowHandle, uint32_t width, uint32_t height)
	{
		LOG_INFO("=== Initializing Vulkan Device ===");
		LOG_INFO("Window: {}x{}", width, height);

		// Store window info for later use by swapchain
		m_WindowHandle = windowHandle;
		m_Width = width;
		m_Height = height;

		//Step 1: Create Vulkan instance
		//The instance is the connection between the application and the Vulkan library.
		if (!CreateInstance()) {
			LOG_ERROR("Failed to create Vulkan instance");
			return false;
		}
		LOG_INFO("Vulkan instance created");

		// STEP 2: Setup Debug Messenger (only in debug builds)
		// This lets Vulkan send us detailed error messages
		if (m_EnableValidationLayers && !SetupDebugMessenger()) {
			LOG_ERROR("Failed to setup debug messenger");
			return false;
		}
		if (m_EnableValidationLayers) {
			LOG_INFO("Debug messenger setup");
		}

		// STEP 3: Pick Physical Device (GPU)
		// We need to select which gpu to use
		if (!PickPhysicalDevice()) {
			LOG_ERROR("Failed to pick physical device");
			return false;
		}

		if (!CreateLogicalDevice()) {
			LOG_ERROR("Failed to create logical device");
			return false;
		}
		LOG_INFO("Logical device created");

		LOG_INFO("=== Vulkan Device Initialized Successfully ===");

		return true;
	}

	void VulkanDevice::Shutdown()
	{
		LOG_INFO("Shutting down Vulkan device... ");

		//Destroy in reverse order of creation
		if (m_Device != VK_NULL_HANDLE)
		{
			vkDestroyDevice(m_Device, nullptr);
			m_Device = VK_NULL_HANDLE;
		}

		// Need to load the function to destroy debugger
		if (m_EnableValidationLayers && m_DebugMessenger != VK_NULL_HANDLE)
		{
			auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
				vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");
			if (func != nullptr)
			{
				func(m_Instance, m_DebugMessenger, nullptr);
				m_DebugMessenger = VK_NULL_HANDLE;
			}
		}

		if (m_Instance != VK_NULL_HANDLE)
		{
			vkDestroyInstance(m_Instance, nullptr);
			m_Instance = VK_NULL_HANDLE;
		}

		LOG_INFO("Vulkan device shutdown complete");
	}

	bool VulkanDevice::CreateInstance()
	{
		// First, check if we can use validation layers (only in debug)
		if (m_EnableValidationLayers && !CheckValidationLayerSupport())
		{
			LOG_ERROR("Validation layers requested but not available!");
			return false;
		}

		// Application info - this is optional but good practice
		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Nightbloom Sky Renderer";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "Nightbloom Engine";
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_2;

		//Instance create info
		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;

		// Get required extensions (window system integration + debug)
		auto extensions = GetRequiredExtensions();
		createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
		createInfo.ppEnabledExtensionNames = extensions.data();

		// Enable validation layers in debug builds
		VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
		if (m_EnableValidationLayers) {
			createInfo.enabledLayerCount = static_cast<uint32_t>(m_ValidationLayers.size());
			createInfo.ppEnabledLayerNames = m_ValidationLayers.data();

			// This lets us catch errors during vkCreateInstance/vkDestroyInstance
			PopulateDebugMessengerCreateInfo(debugCreateInfo);
			createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
		}
		else
		{
			createInfo.enabledLayerCount = 0;
			createInfo.pNext = nullptr;
		}

		// Create the instance
		VkResult result = vkCreateInstance(&createInfo, nullptr, &m_Instance);
		if (result != VK_SUCCESS) {
			//LOG_ERROR("vkCreateInstance failed with result: {}", result);
			return false;
		}

		return true;
	}

	bool VulkanDevice::SetupDebugMessenger()
	{
		VkDebugUtilsMessengerCreateInfoEXT createInfo{};
		PopulateDebugMessengerCreateInfo(createInfo);

		// We need to load this function manually
		auto func = (PFN_vkCreateDebugUtilsMessengerEXT)
			vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT");

		if (func == nullptr) {
			LOG_ERROR("Failed to load vkCreateDebugUtilsMessengerEXT");
			return false;
		}

		VkResult result = func(m_Instance, &createInfo, nullptr, &m_DebugMessenger);
		if (result != VK_SUCCESS) {
			LOG_ERROR("Failed to create debug messenger");
			return false;
		}

		return true;
	}

	bool VulkanDevice::PickPhysicalDevice()
	{
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);

		if (deviceCount == 0)
		{
			LOG_ERROR("No GPUs with Vulkan support found!");
			return false;
		}

		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

		LOG_INFO("Found {} Vulkan-compatible device(s)", deviceCount);

		// Find the best suitable device
		// for now, we just pick the first one that supports graphics and present queues
		// In the future, we can add more criteria like compute support, memory, etc. //Todo: make a score for gpu's
		for (const auto& device : devices)
		{
			if (IsDeviceSuitable(device))
			{
				m_PhysicalDevice = device;
				break;
			}
		}

		if (m_PhysicalDevice == VK_NULL_HANDLE) {
			LOG_ERROR("Failed to find a suitable GPU!");
			return false;
		}

		// Log selected GPU info
		VkPhysicalDeviceProperties deviceProperties;
		vkGetPhysicalDeviceProperties(m_PhysicalDevice, &deviceProperties);

		LOG_INFO("Selected GPU: {}", deviceProperties.deviceName);

		std::string deviceType;

		switch (deviceProperties.deviceType) {
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: deviceType = "Integrated GPU"; break;
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: deviceType = "Discrete GPU"; break;
		case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: deviceType = "Virtual GPU"; break;
		case VK_PHYSICAL_DEVICE_TYPE_CPU: deviceType = "CPU"; break;
		case VK_PHYSICAL_DEVICE_TYPE_OTHER: deviceType = "Other GPU Type"; break;
		default: deviceType = "Unknown GPU Type"; break;
		}

		LOG_INFO("GPU Type: {}", deviceType);

		LOG_INFO("Vulkan API Version: {}.{}.{}",
			VK_VERSION_MAJOR(deviceProperties.apiVersion),
			VK_VERSION_MINOR(deviceProperties.apiVersion),
			VK_VERSION_PATCH(deviceProperties.apiVersion));

		return true;
	}

	bool VulkanDevice::CreateLogicalDevice()
	{
		// Find queue families (graphics and presentation)
		m_QueueFamilies = FindQueueFamilies(m_PhysicalDevice);

		// Make sure we found the required queue families
		if (!m_QueueFamilies.IsComplete()) {
			LOG_ERROR("Required queue families not found!");
			return false;
		}

		// Specify which Queues to create
		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
		std::set<uint32_t> uniqueQueueFamilies = {
			m_QueueFamilies.graphicsFamily.value(),
			m_QueueFamilies.presentFamily.value()
		};

		float queuePriority = 1.0f; // Priority of the queue, 1.0 is highest
		for (uint32_t queueFamily : uniqueQueueFamilies)
		{
			VkDeviceQueueCreateInfo queueCreateInfo{};
			queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfo.queueFamilyIndex = queueFamily;
			queueCreateInfo.queueCount = 1; // We only need one queue per family for now
			queueCreateInfo.pQueuePriorities = &queuePriority; // Set priority

			queueCreateInfos.push_back(queueCreateInfo);
		}

		// Specify device features we want to use
		VkPhysicalDeviceFeatures deviceFeatures{};
		// For now, we dont need any special features
		// Later we might want: geometryShader, tessellationShader, samplerAnisotropy, etc.

		// Create the logical device
		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
		createInfo.pQueueCreateInfos = queueCreateInfos.data();
		createInfo.pEnabledFeatures = &deviceFeatures;

		// Enable device extensions (swapchain support)
		createInfo.enabledExtensionCount = static_cast<uint32_t>(m_DeviceExtensions.size());
		createInfo.ppEnabledExtensionNames = m_DeviceExtensions.data();

		// Validation layers 
		if (m_EnableValidationLayers) {
			createInfo.enabledLayerCount = static_cast<uint32_t>(m_ValidationLayers.size());
			createInfo.ppEnabledLayerNames = m_ValidationLayers.data();
		}
		else {
			createInfo.enabledLayerCount = 0;
		}

		VkResult result = vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device);
		if (result != VK_SUCCESS) {
			//LOG_ERROR("Failed to create logical device: {}", result);
			return false;
		}

		// Get queue handles
		vkGetDeviceQueue(m_Device, m_QueueFamilies.graphicsFamily.value(), 0, &m_GraphicsQueue);
		vkGetDeviceQueue(m_Device, m_QueueFamilies.presentFamily.value(), 0, &m_PresentQueue);

		LOG_INFO("Logical device created successfully");
		LOG_INFO("Graphics queue family index: {}", m_QueueFamilies.graphicsFamily.value());
		LOG_INFO("Present queue family index: {}", m_QueueFamilies.presentFamily.value());

		return true;
	}

	bool VulkanDevice::CheckValidationLayerSupport() const
	{
		uint32_t layerCount;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

		std::vector<VkLayerProperties> availableLayers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

		// Check if all requested layers are available
		for (const char* layerName : m_ValidationLayers)
		{
			bool layerFound = false;

			for (const auto& layerProperties : availableLayers) {
				if (strcmp(layerName, layerProperties.layerName) == 0) {
					layerFound = true;
					break;
				}
			}

			if (!layerFound) {
				LOG_WARN("Validation layer '{}' not available", layerName);
				return false;
			}
		}

		return true;
	}

	std::vector<const char*> VulkanDevice::GetRequiredExtensions() const
	{
		std::vector<const char*> extensions;

		// We need surface extensions for window system integration
		extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);

		//Platform-specific surface extensions
#ifdef NIGHTBLOOM_PLATFORM_WINDOWS
        extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(NIGHTBLOOM_PLATFORM_LINUX)
        extensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#elif defined(NIGHTBLOOM_PLATFORM_MACOS)
        extensions.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
#endif

		//Debug extensions
		if (m_EnableValidationLayers) {
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		return extensions;
	}

	bool VulkanDevice::IsDeviceSuitable(VkPhysicalDevice device) const
	{
		// Check queue families
		QueueFamilyIndices indices = FindQueueFamilies(device);
		if (!indices.IsComplete()) {
			return false;
		}

		// Check device extension support (need swapchain)
		bool extensionsSupported = CheckDeviceExtensionSupport(device);
		if (!extensionsSupported) {
			return false;
		}

		// For now, that's enough. Later we might check:
		// - Device features (geometry shaders, etc.)
		// - Device limits (max texture size, etc.)
		// - Device type preference (discrete > integrated)

		return true;
	}

	VulkanDevice::QueueFamilyIndices VulkanDevice::FindQueueFamilies(VkPhysicalDevice device) const
	{
		QueueFamilyIndices indices;

		// Get queue family properties
		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

		// Find queue families we need
		int i = 0;
		for (const auto& queueFamily : queueFamilies)
		{
			//Graphics queue
			if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				indices.graphicsFamily = i;
			}

			// Present queue (we need surface for this, but we'll check later)
			// For now, assume any graphics queue can present
			// VulkanSwapchain will verify this when it creates the surface
			if (!indices.presentFamily.has_value()) {
				indices.presentFamily = i;
			}

			if (indices.IsComplete()) {
				break;
			}

			i++;
		}

		return indices;
	}

	bool VulkanDevice::CheckDeviceExtensionSupport(VkPhysicalDevice device) const
	{
		uint32_t extensionCount;
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

		std::set<std::string> requiredExtensions(m_DeviceExtensions.begin(), m_DeviceExtensions.end());

		for (const auto& extension : availableExtensions)
		{
			requiredExtensions.erase(extension.extensionName);
		}

		return requiredExtensions.empty();
 	}

	void VulkanDevice::PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) const
	{
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		createInfo.pfnUserCallback = DebugCallback;
		createInfo.pUserData = nullptr;
	}

	VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDevice::DebugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* pUserData)
	{
		// Map Vulkan severity to our logging levels
		if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
			LOG_ERROR("Vulkan: {}", pCallbackData->pMessage);
		}
		else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
			LOG_WARN("Vulkan: {}", pCallbackData->pMessage);
		}
		else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
			LOG_INFO("Vulkan: {}", pCallbackData->pMessage);
		}
		else {
			LOG_TRACE("Vulkan: {}", pCallbackData->pMessage);
		}

		return VK_FALSE; // Don't abort
	}

	void VulkanDevice::WaitForIdle()
	{
		if (m_Device != VK_NULL_HANDLE) {
			vkDeviceWaitIdle(m_Device);
		}
	}

	bool VulkanDevice::SupportsFeature(const std::string& feature) const
	{
		// Query supported features
		if (feature == "geometry_shader") {
			VkPhysicalDeviceFeatures features;
			vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &features);
			return features.geometryShader;
		}
		else if (feature == "tessellation") {
			VkPhysicalDeviceFeatures features;
			vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &features);
			return features.tessellationShader;
		}
		else if (feature == "compute") {
			// Vulkan always supports compute if it has a graphics queue
			return true;
		}

		return false;
	}

	size_t VulkanDevice::GetMinUniformBufferAlignment() const
	{
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(m_PhysicalDevice, &properties);
		return properties.limits.minUniformBufferOffsetAlignment;
	}
}