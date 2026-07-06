//------------------------------------------------------------------------------
// UIManager.cpp
//
// Implementation of ImGui integration
//------------------------------------------------------------------------------

#include "Engine/Renderer/Components/UIManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <imgui.h>
#include <imgui_impl_vulkan.h>

// Platform backend: Win32 message hooks on Windows, GLFW callbacks on macOS
// (windowHandle is HWND / GLFWwindow* respectively — see Window::GetNativeHandle).
#ifdef NIGHTBLOOM_PLATFORM_WINDOWS
#include <imgui_impl_win32.h>
#define NB_ImGui_Platform_Shutdown ImGui_ImplWin32_Shutdown
#define NB_ImGui_Platform_NewFrame ImGui_ImplWin32_NewFrame
#elif defined(NIGHTBLOOM_PLATFORM_MACOS)
#include <imgui_impl_glfw.h>
struct GLFWwindow;
#define NB_ImGui_Platform_Shutdown ImGui_ImplGlfw_Shutdown
#define NB_ImGui_Platform_NewFrame ImGui_ImplGlfw_NewFrame
#endif

namespace Nightbloom
{
	bool UIManager::Initialize(VulkanDevice* device, void* windowHandle,
		VkRenderPass renderPass, uint32_t imageCount)
	{
		// Create descriptor pool for ImGui
		if (!CreateDescriptorPool(device->GetDevice()))
		{
			LOG_ERROR("Failed to create ImGui descriptor pool");
			return false;
		}

		// Setup ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable keyboard navigation

		// Setup style
		ImGui::StyleColorsDark();

		// Setup Platform/Renderer bindings
#ifdef NIGHTBLOOM_PLATFORM_WINDOWS
		if (!ImGui_ImplWin32_Init(windowHandle))
		{
			LOG_ERROR("Failed to initialize ImGui Win32 implementation");
			Cleanup(device->GetDevice());
			return false;
		}
#elif defined(NIGHTBLOOM_PLATFORM_MACOS)
		// install_callbacks=true: ImGui saves and chains the GLFWWindow callbacks,
		// so game input keeps flowing underneath the UI.
		if (!ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(windowHandle), true))
		{
			LOG_ERROR("Failed to initialize ImGui GLFW implementation");
			Cleanup(device->GetDevice());
			return false;
		}
#endif

		// Setup Vulkan binding
		ImGui_ImplVulkan_InitInfo init_info = {};
		init_info.Instance = device->GetInstance();
		init_info.PhysicalDevice = device->GetPhysicalDevice();
		init_info.Device = device->GetDevice();
		init_info.QueueFamily = device->GetGraphicsQueueFamily();
		init_info.Queue = device->GetGraphicsQueue();
		init_info.PipelineCache = VK_NULL_HANDLE;
		init_info.DescriptorPool = m_DescriptorPool;
		init_info.RenderPass = renderPass;
		init_info.Subpass = 0;
		init_info.MinImageCount = 2;
		init_info.ImageCount = imageCount;
		init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		init_info.Allocator = nullptr;
		init_info.CheckVkResultFn = nullptr;

		if (!ImGui_ImplVulkan_Init(&init_info))
		{
			LOG_ERROR("Failed to initialize ImGui Vulkan implementation");
			NB_ImGui_Platform_Shutdown();
			Cleanup(device->GetDevice());
			return false;
		}

		m_Initialized = true;
		LOG_INFO("UI manager initialized successfully");
		return true;
	}

	void UIManager::Cleanup(VkDevice device)
	{
		if (m_Initialized)
		{
			ImGui_ImplVulkan_Shutdown();
			NB_ImGui_Platform_Shutdown();
			ImGui::DestroyContext();
			m_Initialized = false;
		}

		if (m_DescriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
			m_DescriptorPool = VK_NULL_HANDLE;
		}

		LOG_INFO("UI manager cleaned up");
	}

	void UIManager::BeginFrame()
	{
		if (!m_Initialized) return;

		ImGui_ImplVulkan_NewFrame();
		NB_ImGui_Platform_NewFrame();
		ImGui::NewFrame();
	}

	void UIManager::EndFrame()
	{
		if (!m_Initialized) return;

		ImGui::Render();
	}

	void UIManager::Render(VkCommandBuffer commandBuffer)
	{
		if (!m_Initialized) return;

		ImDrawData* draw_data = ImGui::GetDrawData();
		if (draw_data)
		{
			ImGui_ImplVulkan_RenderDrawData(draw_data, commandBuffer);
		}
	}

	bool UIManager::CreateDescriptorPool(VkDevice device)
	{
		// Create descriptor pool for ImGui
		// This needs to be large enough to handle all ImGui resources
		VkDescriptorPoolSize pool_sizes[] =
		{
			{ VK_DESCRIPTOR_TYPE_SAMPLER, 100 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100 },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100 }
		};

		VkDescriptorPoolCreateInfo pool_info{};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool_info.maxSets = 100;
		pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
		pool_info.pPoolSizes = pool_sizes;

		if (vkCreateDescriptorPool(device, &pool_info, nullptr, &m_DescriptorPool) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create descriptor pool for ImGui");
			return false;
		}

		return true;
	}

} // namespace Nightbloom