//------------------------------------------------------------------------------
// VulkanTest.cpp - Standalone test (create this in a temp folder)
//------------------------------------------------------------------------------

#include <iostream>
#include <vector>
#include <windows.h>

// Disable video extensions that cause header issues
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR

// Include only what we need
#include "C:/VulkanSDK/1.4.321.1/Include/vulkan/vulkan_core.h"

// Manually declare the functions we need (avoids video header issues)
typedef VkResult(VKAPI_PTR* PFN_vkCreateInstance)(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance);
typedef void (VKAPI_PTR* PFN_vkDestroyInstance)(VkInstance instance, const VkAllocationCallbacks* pAllocator);
typedef VkResult(VKAPI_PTR* PFN_vkEnumeratePhysicalDevices)(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices);
typedef void (VKAPI_PTR* PFN_vkGetPhysicalDeviceProperties)(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties);

#pragma comment(lib, "C:/VulkanSDK/1.4.321.1/Lib/vulkan-1.lib")

int main() {
	std::cout << "Testing Vulkan SDK..." << std::endl;

	// Load Vulkan functions manually
	HMODULE vulkanLib = LoadLibraryA("vulkan-1.dll");
	if (!vulkanLib) {
		std::cout << "? Failed to load vulkan-1.dll" << std::endl;
		return -1;
	}

	auto vkCreateInstance = (PFN_vkCreateInstance)GetProcAddress(vulkanLib, "vkCreateInstance");
	auto vkDestroyInstance = (PFN_vkDestroyInstance)GetProcAddress(vulkanLib, "vkDestroyInstance");
	auto vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)GetProcAddress(vulkanLib, "vkEnumeratePhysicalDevices");
	auto vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)GetProcAddress(vulkanLib, "vkGetPhysicalDeviceProperties");

	if (!vkCreateInstance || !vkDestroyInstance || !vkEnumeratePhysicalDevices || !vkGetPhysicalDeviceProperties) {
		std::cout << "? Failed to load Vulkan functions" << std::endl;
		FreeLibrary(vulkanLib);
		return -1;
	}

	// Test 1: Check if we can create a Vulkan instance
	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Vulkan Test";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "Test";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_0;

	VkInstanceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	VkInstance instance;
	VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);

	if (result == VK_SUCCESS) {
		std::cout << "? Vulkan instance created successfully!" << std::endl;

		// Test 2: Enumerate physical devices
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

		if (deviceCount > 0) {
			std::cout << "? Found " << deviceCount << " Vulkan-compatible device(s)" << std::endl;

			std::vector<VkPhysicalDevice> devices(deviceCount);
			vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

			// Get info about first device
			VkPhysicalDeviceProperties deviceProperties;
			vkGetPhysicalDeviceProperties(devices[0], &deviceProperties);

			std::cout << "Primary GPU: " << deviceProperties.deviceName << std::endl;
			std::cout << "Vulkan API Version: "
				<< VK_VERSION_MAJOR(deviceProperties.apiVersion) << "."
				<< VK_VERSION_MINOR(deviceProperties.apiVersion) << "."
				<< VK_VERSION_PATCH(deviceProperties.apiVersion) << std::endl;
		}
		else {
			std::cout << "?? No Vulkan-compatible devices found!" << std::endl;
		}

		vkDestroyInstance(instance, nullptr);
		std::cout << "? Vulkan test completed successfully!" << std::endl;
	}
	else {
		std::cout << "? Failed to create Vulkan instance! Error code: " << result << std::endl;
		std::cout << "Common causes:" << std::endl;
		std::cout << "- Vulkan SDK not properly installed" << std::endl;
		std::cout << "- Graphics drivers don't support Vulkan" << std::endl;
		std::cout << "- Running on virtual machine without GPU passthrough" << std::endl;
	}

	FreeLibrary(vulkanLib);
	std::cout << "\nPress Enter to exit..." << std::endl;
	std::cin.get();
	return 0;
}