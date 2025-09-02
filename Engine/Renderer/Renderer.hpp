//------------------------------------------------------------------------------
// Renderer.hpp
//
// Abstraction layer for graphics rendering
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#pragma once
#include "Engine/Renderer/Vulkan/VulkanCommon.hpp"  // This should include all necessary Vulkan headers
#include "Engine/Renderer/PipelineInterface.hpp"  // Interface for pipeline management
#include "Engine/Renderer/DrawCommandSystem.hpp"
#include <memory>
#include <string>

namespace Nightbloom
{
	class RenderDevice;
	class Renderer
	{
	private:
		struct RendererData;
		std::unique_ptr<RendererData> m_Data;

	private:
		bool CreateCommandPool();
		bool CreateCommandBuffers();
		bool CreateSyncObjects();
		void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
		void PreRecordAllCommandBuffers();

		void DestroySyncObjects();
		void DestroyCommandBuffers();

	private:
		bool CreateRenderPass();
		bool CreateFramebuffers();
		void DestroyRenderPass();
		void DestroyFramebuffers();

	private:
		VkShaderModule CreateShaderModule(const std::vector<char>& code);

	private:
		bool CreateVertexBuffer();
		bool CreateIndexBuffer();

		void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

		// Temporary getters for test geometry
		// These return generic Buffer* pointers (not VulkanBuffer*)
	public:
		Buffer* GetTestVertexBuffer() const;
		Buffer* GetTestIndexBuffer() const;
		uint32_t GetTestIndexCount() const;

	private:
		bool CreateGraphicsPipeline();

	private:
		bool CreateImGuiDescriptorPool();
		VkCommandBuffer BeginSingleTimeCommands();
		void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

	public:
		Renderer();
		~Renderer();

		bool Initialize(void* windowHandle, uint32_t width, uint32_t height);
		void Shutdown();

		void BeginFrame();
		void EndFrame();
		void FinalizeFrame();

		void Clear(float r = 1.0f, float g = 0.0f, float b = 1.0f, float a = 1.0f);

		// These will be implemented in steps
		//void DrawTriangle();

		void SubmitDrawList(const DrawList& drawList);
		void DrawMesh(Buffer* vertexBuffer, Buffer* indexBuffer, uint32_t indexCount,
			PipelineType pipeline, const glm::mat4& transform);

		// Camera management (for push constants)
		void SetViewMatrix(const glm::mat4& view);
		void SetProjectionMatrix(const glm::mat4& proj);

		bool IsInitialized() const;

		RenderDevice* GetDevice() const;
		IPipelineManager* GetPipelineManager() const;

		void TogglePipeline();

		void ReloadShaders();

		// Additional rendering methods can be added here
		//const std::string& GetRendererName() const;
	};
}