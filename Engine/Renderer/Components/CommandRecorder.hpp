//------------------------------------------------------------------------------
// CommandRecorder.hpp
//
// Manages command buffer allocation and recording
// Executes draw lists and handles render state
//------------------------------------------------------------------------------
#pragma once

#include <vulkan/vulkan.h>
#include "Engine/Renderer/Vulkan/VulkanCommandPool.hpp"  // ADD THIS - Need full definition
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <cstdint>

namespace Nightbloom
{
	// Forward Declarations
	class VulkanDevice;
	class VulkanPipelineAdapter;
	class VulkanDescriptorManager;
	class DrawList;
	struct DrawCommand;

	class CommandRecorder
	{
	public:
		CommandRecorder() = default;
		~CommandRecorder() = default;

		// Lifecycle
		bool Initialize(VulkanDevice* device, VulkanDescriptorManager* descriptorManager, uint32_t commandBufferCount);
		void Cleanup();

		// Command Buffer Management
		void BeginCommandBuffer(uint32_t bufferIndex);
		void EndCommandBuffer(uint32_t bufferIndex);
		void ResetCommandBuffer(uint32_t bufferIndex);

		// Render pass operations
		void BeginRenderPass(uint32_t bufferIndex, VkRenderPass renderPass,
			VkFramebuffer framebuffer, VkExtent2D extent,
			VkClearValue* clearValue = nullptr);
		void EndRenderPass(uint32_t bufferIndex);

		// Draw operations
		void ExecuteDrawList(uint32_t bufferIndex, const DrawList& drawList,
			VulkanPipelineAdapter* pipelineManager,
			const glm::mat4& viewMatrix,
			const glm::mat4& projectionMatrix);

		// Individual draw command execution
		void ExecuteDrawCommand(uint32_t bufferIndex, const DrawCommand& cmd,
			VulkanPipelineAdapter* pipelineManager,
			const glm::mat4& viewMatrix,
			const glm::mat4& projectionMatrix);

		// Getters
		VkCommandBuffer GetCommandBuffer(uint32_t index) const
		{
			return (index < m_CommandBuffers.size()) ? m_CommandBuffers[index] : VK_NULL_HANDLE;
		}

	private:
		VulkanDevice* m_Device = nullptr;
		VulkanDescriptorManager* m_DescriptorManager = nullptr;
		std::unique_ptr<VulkanCommandPool> m_CommandPool;
		std::vector<VkCommandBuffer> m_CommandBuffers;

		// Track current state to minimize redundant binds
		uint32_t m_CurrentBufferIndex = 0;
		VkPipeline m_CurrentPipeline = VK_NULL_HANDLE;
		VkPipelineLayout m_CurrentPipelineLayout = VK_NULL_HANDLE;

		// Helper methods
		void BindPipelineIfChanged(uint32_t bufferIndex, VkPipeline pipeline);
		void SetPushConstants(uint32_t bufferIndex, VkPipelineLayout layout,
			const void* data, uint32_t size, VkShaderStageFlags stages);

		void BindTextureDescriptorSet(uint32_t frameIndex, VkDescriptorSet set, VkPipelineLayout layout);

		// Prevent copying
		CommandRecorder(const CommandRecorder&) = delete;
		CommandRecorder& operator=(const CommandRecorder&) = delete;
	};

} // namespace Nightbloom