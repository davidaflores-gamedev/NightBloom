//------------------------------------------------------------------------------
// RendererTests.cpp
//
// Vulkan environment smoke tests: can we load the loader, create an instance,
// and see a physical device. These catch "the machine/SDK/driver is wrong"
// before any renderer test bothers to run.
//
// HISTORY — worth knowing, because it hid every other test in this target:
// this file used to be a standalone scratch program with its own `int main()`.
// Because a .obj's `main` beats the one inside gtest_main.lib, that main
// silently won the link and NightbloomTests.exe ran ONLY this Vulkan probe —
// every TEST() in LoggerTests/MathTests was compiled, linked, and never
// executed. It also ended in std::cin.get(), so the "suite" hung forever on
// any automated runner. Don't reintroduce a main() here; gtest_main provides it.
//
// Everything is resolved dynamically (VK_NO_PROTOTYPES + LoadLibraryA) so this
// links without the Vulkan import library.
//------------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <vector>
#include <windows.h>

// Disable video extensions that cause header issues
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR

// Include only what we need
#include <vulkan/vulkan_core.h>

// Manually declare the functions we need (avoids video header issues)
typedef VkResult(VKAPI_PTR* PFN_vkCreateInstance)(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance);
typedef void (VKAPI_PTR* PFN_vkDestroyInstance)(VkInstance instance, const VkAllocationCallbacks* pAllocator);
typedef VkResult(VKAPI_PTR* PFN_vkEnumeratePhysicalDevices)(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices);
typedef void (VKAPI_PTR* PFN_vkGetPhysicalDeviceProperties)(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties);

namespace
{
	// Loads vulkan-1.dll and the handful of entry points these tests need.
	// Frees the module on destruction so a failing test doesn't leak it.
	struct VulkanLoader
	{
		HMODULE lib = nullptr;
		PFN_vkCreateInstance              createInstance = nullptr;
		PFN_vkDestroyInstance             destroyInstance = nullptr;
		PFN_vkEnumeratePhysicalDevices    enumeratePhysicalDevices = nullptr;
		PFN_vkGetPhysicalDeviceProperties getPhysicalDeviceProperties = nullptr;

		VulkanLoader()
		{
			lib = LoadLibraryA("vulkan-1.dll");
			if (!lib) return;
			createInstance = (PFN_vkCreateInstance)GetProcAddress(lib, "vkCreateInstance");
			destroyInstance = (PFN_vkDestroyInstance)GetProcAddress(lib, "vkDestroyInstance");
			enumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)GetProcAddress(lib, "vkEnumeratePhysicalDevices");
			getPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)GetProcAddress(lib, "vkGetPhysicalDeviceProperties");
		}

		~VulkanLoader() { if (lib) FreeLibrary(lib); }

		bool IsValid() const
		{
			return lib && createInstance && destroyInstance
				&& enumeratePhysicalDevices && getPhysicalDeviceProperties;
		}

		VulkanLoader(const VulkanLoader&) = delete;
		VulkanLoader& operator=(const VulkanLoader&) = delete;
	};

	// Creates a minimal instance for a test and destroys it on scope exit.
	struct ScopedInstance
	{
		const VulkanLoader& loader;
		VkInstance handle = VK_NULL_HANDLE;
		VkResult   result = VK_ERROR_INITIALIZATION_FAILED;

		explicit ScopedInstance(const VulkanLoader& l) : loader(l)
		{
			VkApplicationInfo appInfo{};
			appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
			appInfo.pApplicationName = "Nightbloom Tests";
			appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
			appInfo.pEngineName = "Nightbloom";
			appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
			appInfo.apiVersion = VK_API_VERSION_1_0;

			VkInstanceCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
			createInfo.pApplicationInfo = &appInfo;

			result = loader.createInstance(&createInfo, nullptr, &handle);
		}

		~ScopedInstance()
		{
			if (result == VK_SUCCESS && handle != VK_NULL_HANDLE)
				loader.destroyInstance(handle, nullptr);
		}

		ScopedInstance(const ScopedInstance&) = delete;
		ScopedInstance& operator=(const ScopedInstance&) = delete;
	};
}

TEST(VulkanEnvironment, LoaderAndEntryPointsResolve)
{
	VulkanLoader loader;
	ASSERT_NE(loader.lib, nullptr)
		<< "vulkan-1.dll not found — Vulkan runtime/driver missing?";
	EXPECT_NE(loader.createInstance, nullptr);
	EXPECT_NE(loader.destroyInstance, nullptr);
	EXPECT_NE(loader.enumeratePhysicalDevices, nullptr);
	EXPECT_NE(loader.getPhysicalDeviceProperties, nullptr);
}

TEST(VulkanEnvironment, CanCreateInstance)
{
	VulkanLoader loader;
	ASSERT_TRUE(loader.IsValid()) << "Vulkan loader unavailable";

	ScopedInstance instance(loader);
	EXPECT_EQ(instance.result, VK_SUCCESS)
		<< "vkCreateInstance failed (code " << instance.result << "). "
		<< "Usual causes: SDK not installed, drivers without Vulkan support, "
		<< "or a VM with no GPU passthrough.";
}

TEST(VulkanEnvironment, ReportsAtLeastOnePhysicalDevice)
{
	VulkanLoader loader;
	ASSERT_TRUE(loader.IsValid()) << "Vulkan loader unavailable";

	ScopedInstance instance(loader);
	ASSERT_EQ(instance.result, VK_SUCCESS);

	uint32_t deviceCount = 0;
	loader.enumeratePhysicalDevices(instance.handle, &deviceCount, nullptr);
	ASSERT_GT(deviceCount, 0u) << "No Vulkan-compatible devices found";

	std::vector<VkPhysicalDevice> devices(deviceCount);
	loader.enumeratePhysicalDevices(instance.handle, &deviceCount, devices.data());

	VkPhysicalDeviceProperties props{};
	loader.getPhysicalDeviceProperties(devices[0], &props);

	// Surfaced as test output so a failing run says which GPU/driver it saw.
	std::cout << "  Primary GPU: " << props.deviceName
		<< " (Vulkan " << VK_VERSION_MAJOR(props.apiVersion)
		<< "." << VK_VERSION_MINOR(props.apiVersion)
		<< "." << VK_VERSION_PATCH(props.apiVersion) << ")\n";

	EXPECT_GT(props.apiVersion, 0u);
}
