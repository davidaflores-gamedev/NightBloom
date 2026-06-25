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
			const VkClearValue* clearValue, uint32_t clearValueCount);
		void EndRenderPass(uint32_t bufferIndex);

		// Draw operations
		void ExecuteDrawList(uint32_t bufferIndex, const DrawList& drawList,
			VulkanPipelineAdapter* pipelineManager,
			const glm::mat4& viewMatrix,
			const glm::mat4& projectionMatrix);

		// Reflection pass: replays the opaque draws (Mesh/Terrain/Foliage only)
		// reusing their normal pipelines, but binds reflectionUniformSet at set 0
		// (the mirror-flipped camera) instead of the scene uniform set. The
		// winding flip from the mirror is cancelled by a negative-height viewport
		// set by the caller before this runs.
		void ExecuteReflectionDrawList(uint32_t bufferIndex, const DrawList& drawList,
			VulkanPipelineAdapter* pipelineManager,
			VkDescriptorSet reflectionUniformSet);

		// Individual draw command execution. overrideUniformSet, when non-null,
		// is bound at set 0 in place of the per-frame scene uniform set (used by
		// the reflection pass to substitute the mirror-flipped camera).
		void ExecuteDrawCommand(uint32_t bufferIndex, const DrawCommand& cmd,
			VulkanPipelineAdapter* pipelineManager,
			const glm::mat4& viewMatrix,
			const glm::mat4& projectionMatrix,
			VkDescriptorSet overrideUniformSet = VK_NULL_HANDLE);

		// The planar-reflection color target's descriptor set (set 2 in the Water
		// pipeline). Set once by the Renderer after allocation; rebound per Water
		// draw. Not owned.
		void SetReflectionInputSet(VkDescriptorSet set) { m_ReflectionInputSet = set; }

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

		// Planar-reflection color target descriptor set (Water set 2). Not owned.
		VkDescriptorSet m_ReflectionInputSet = VK_NULL_HANDLE;

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