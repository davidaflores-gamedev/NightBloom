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
	class DrawList;
	class IPipelineManager;

	// Component forward declarations
	class FrameSyncManager;
	class RenderPassManager;
	class CommandRecorder;
	class ResourceManager;
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
		IPipelineManager* GetPipelineManager() const;
		ResourceManager* GetResourceManager() const { return m_Resources.get(); }

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
		std::unique_ptr<UIManager> m_UI;

		// Frame state
		DrawList m_FrameDrawList;
		glm::mat4 m_ViewMatrix = glm::mat4(1.0f);
		glm::mat4 m_ProjectionMatrix = glm::mat4(1.0f);
		uint32_t m_CurrentImageIndex = 0;
		glm::vec4 m_ClearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

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
// ------------------------------------------------------------------------------
// Renderer.hpp
// 
// Abstraction layer for graphics rendering
// Copyright (c) 2024 Your Name. All rights reserved.
// ------------------------------------------------------------------------------
// 
// #pragma once
// #include "Engine/Renderer/Vulkan/VulkanCommon.hpp"  // This should include all necessary Vulkan headers
// #include "Engine/Renderer/PipelineInterface.hpp"  // Interface for pipeline management
// #include "Engine/Renderer/DrawCommandSystem.hpp"
// #include <memory>
// #include <string>
// 
// namespace Nightbloom
// {
// 	// Forward Declerations
// 	class RenderDevice;
// 	//class VulkanMemoryManager;
// 	//class VulkanPipelineAdapter;
// 	//class DrawList;
// 
// 	// Component forward Declerations
// 	class FrameSyncManager;
// 	//class RenderPassManager;
// 	//class CommandRecorder;
// 	//class ResourceManager;
// 	//class UIManager;
// 
// 	class Renderer
// 	{
// 	private:
// 		struct RendererData;
// 		std::unique_ptr<RendererData> m_Data;
// 
// 	private:
// 		bool CreateCommandPool();
// 		bool CreateCommandBuffers();
// 		bool CreateSyncObjects();
// 		void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
// 		void PreRecordAllCommandBuffers();
// 
// 		void DestroySyncObjects();
// 		void DestroyCommandBuffers();
// 
// 	private:
// 		bool CreateRenderPass();
// 		bool CreateFramebuffers();
// 		void DestroyRenderPass();
// 		void DestroyFramebuffers();
// 
// 	private:
// 		VkShaderModule CreateShaderModule(const std::vector<char>& code);
// 
// 	private:
// 		bool CreateVertexBuffer();
// 		bool CreateIndexBuffer();
// 
// 		void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
// 
// 		// Temporary getters for test geometry
// 		// These return generic Buffer* pointers (not VulkanBuffer*)
// 	public:
// 		Buffer* GetTestVertexBuffer() const;
// 		Buffer* GetTestIndexBuffer() const;
// 		uint32_t GetTestIndexCount() const;
// 
// 	private:
// 		bool CreateGraphicsPipeline();
// 
// 	private:
// 		bool CreateImGuiDescriptorPool();
// 		VkCommandBuffer BeginSingleTimeCommands();
// 		void EndSingleTimeCommands(VkCommandBuffer commandBuffer);
// 
// 	public:
// 		Renderer();
// 		~Renderer();
// 
// 		bool Initialize(void* windowHandle, uint32_t width, uint32_t height);
// 		void Shutdown();
// 
// 		void BeginFrame();
// 		void EndFrame();
// 		void FinalizeFrame();
// 
// 		void Clear(float r = 1.0f, float g = 0.0f, float b = 1.0f, float a = 1.0f);
// 
// 		// These will be implemented in steps
// 		//void DrawTriangle();
// 
// 		void SubmitDrawList(const DrawList& drawList);
// 		void DrawMesh(Buffer* vertexBuffer, Buffer* indexBuffer, uint32_t indexCount,
// 			PipelineType pipeline, const glm::mat4& transform);
// 
// 		// Camera management (for push constants)
// 		void SetViewMatrix(const glm::mat4& view);
// 		void SetProjectionMatrix(const glm::mat4& proj);
// 
// 		bool IsInitialized() const;
// 
// 		RenderDevice* GetDevice() const;
// 		IPipelineManager* GetPipelineManager() const;
// 
// 		void TogglePipeline();
// 
// 		void ReloadShaders();
// 
// 		// Additional rendering methods can be added here
// 		//const std::string& GetRendererName() const;
// 	};
// }