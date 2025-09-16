//------------------------------------------------------------------------------
// Renderer.hpp (REFACTORED VERSION)
//
// Orchestrates rendering components
// Much simpler than before - delegates to specialized managers
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Renderer/Vulkan/VulkanCommon.hpp"  // This should include all necessary Vulkan headers
#include "Engine/Renderer/PipelineInterface.hpp"  // Interface for pipeline management
#include "Engine/Renderer/DrawCommandSystem.hpp"
#include <array>
#include <memory>
#include <glm/glm.hpp>
#include <cstdint>

namespace Nightbloom
{
	// Forward declarations
	class RenderDevice;
	class VulkanSwapchain;
	class VulkanMemoryManager;
	class VulkanPipelineAdapter;
	class Buffer;
	class UniformBuffer; // right now only vulkan equivalent exists
	//class Shader;
	class DrawList;
	class IPipelineManager;

	// Component forward declarations
	class FrameSyncManager;
	class RenderPassManager;
	class CommandRecorder;
	class ResourceManager;
	class VulkanDescriptorManager;
	class UIManager;

	class Renderer
	{
	public:
		Renderer();
		~Renderer();

		// Lifecycle
		bool Initialize(void* windowHandle, uint32_t width, uint32_t height);
		void Shutdown();

		// Frame operations
		void BeginFrame();
		void EndFrame();
		void FinalizeFrame();  // Prepare and record commands

		// Drawing interface
		void SubmitDrawList(const DrawList& drawList);
		void SetViewMatrix(const glm::mat4& view) { m_ViewMatrix = view; }
		void SetProjectionMatrix(const glm::mat4& proj) { m_ProjectionMatrix = proj; }

		// Clear screen (for when draw list is empty)
		void Clear(float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 1.0f);

		// Resource access (temporary - for editor compatibility)
		Buffer* GetTestVertexBuffer() const;
		Buffer* GetTestIndexBuffer() const;
		uint32_t GetTestIndexCount() const;

		void TestShaderClass();

		bool LoadShaders();

		// System access
		RenderDevice* GetDevice() const { return m_Device.get(); }
		IPipelineManager* GetPipelineManager() const { return (IPipelineManager*)(m_PipelineAdapter.get()); };
		ResourceManager* GetResourceManager() const { return m_Resources.get(); }
		VulkanDescriptorManager* GetDescriptorManager() { return m_DescriptorManager.get(); }

		// Pipeline operations (temporary - for testing)
		void TogglePipeline();
		void ReloadShaders();

		// Status
		bool IsInitialized() const { return m_Initialized; }

	private:
		// Core systems (owned by Renderer)
		std::unique_ptr<RenderDevice> m_Device;
		std::unique_ptr<VulkanSwapchain> m_Swapchain;
		std::unique_ptr<VulkanMemoryManager> m_MemoryManager;
		std::unique_ptr<VulkanPipelineAdapter> m_PipelineAdapter;

		// Component managers (owned by Renderer)
		std::unique_ptr<FrameSyncManager> m_FrameSync;
		std::unique_ptr<RenderPassManager> m_RenderPasses;
		std::unique_ptr<CommandRecorder> m_Commands;
		std::unique_ptr<ResourceManager> m_Resources;
		std::unique_ptr<VulkanDescriptorManager> m_DescriptorManager;
		std::unique_ptr<UIManager> m_UI;

		// Frame state
		DrawList m_FrameDrawList;
		glm::mat4 m_ViewMatrix = glm::mat4(1.0f);
		glm::mat4 m_ProjectionMatrix = glm::mat4(1.0f);
		uint32_t m_CurrentImageIndex = 0;
		glm::vec4 m_ClearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

		std::array<std::unique_ptr<UniformBuffer>, 2> m_FrameUniforms;  // 2 = MAX_FRAMES_IN_FLIGHT
		FrameUniformData m_CurrentFrameData;
		float m_TotalTime = 0.0f;  // Track time for shaders

		// Testing state (temporary)
		PipelineType m_CurrentPipeline = PipelineType::Mesh;

		// Status
		bool m_Initialized = false;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		void* m_WindowHandle = nullptr;

		// Private initialization helpers
		bool InitializeCore();
		bool InitializeComponents();
		bool InitializePipelines();

		// Helper methods
		void RecordCommandBuffer(uint32_t frameIndex, uint32_t imageIndex);
		bool HandleSwapchainResize();

		// Prevent copying
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;
	};

} // namespace Nightbloom