//------------------------------------------------------------------------------
// CommandRecorder.cpp
//
// Implementation of command buffer recording and management
//------------------------------------------------------------------------------

#include "Renderer/Components/CommandRecorder.hpp"
#include "Renderer/Vulkan/VulkanDevice.hpp"
#include "Renderer/Vulkan/VulkanCommandPool.hpp"
#include "Renderer/Vulkan/VulkanPipelineAdapter.hpp"
#include "Renderer/Vulkan/VulkanBuffer.hpp"
#include "Renderer/Vulkan/VulkanTexture.hpp"
#include "Renderer/Vulkan/VulkanDescriptorManager.hpp"
#include "Renderer/DrawCommandSystem.hpp"
#include "Core/Logger/Logger.hpp"

namespace Nightbloom
{
	bool CommandRecorder::Initialize(VulkanDevice* device, VulkanDescriptorManager* descriptorManager, uint32_t commandBufferCount)
	{
		m_Device = device;
		m_DescriptorManager = descriptorManager;

		// Create command pool
		m_CommandPool = std::make_unique<VulkanCommandPool>(device);

		auto queueFamilies = device->GetQueueFamilyIndices();
		if (!m_CommandPool->Initialize(
			queueFamilies.graphicsFamily.value(),
			VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT))
		{
			LOG_ERROR("Failed to create command pool");
			return false;
		}

		// Allocate command buffers
		m_CommandBuffers = m_CommandPool->AllocateCommandBuffers(commandBufferCount);

		if (m_CommandBuffers.empty())
		{
			LOG_ERROR("Failed to allocate command buffers");
			return false;
		}

		LOG_INFO("Command recorder initialized with {} command buffers", commandBufferCount);
		return true;
	}

	void CommandRecorder::Cleanup()
	{
		if (m_CommandPool)
		{
			// Free command buffers
			if (!m_CommandBuffers.empty())
			{
				m_CommandPool->FreeCommandBuffers(m_CommandBuffers);
				m_CommandBuffers.clear();
			}

			// Destroy command pool
			m_CommandPool->Shutdown();
			m_CommandPool.reset();
		}

		m_Device = nullptr;
		LOG_INFO("Command recorder cleaned up");
	}

	void CommandRecorder::BeginCommandBuffer(uint32_t bufferIndex)
	{
		if (bufferIndex >= m_CommandBuffers.size())
		{
			LOG_ERROR("Invalid command buffer index: {}", bufferIndex);
			return;
		}

		m_CurrentBufferIndex = bufferIndex;
		VkCommandBuffer cmd = m_CommandBuffers[bufferIndex];

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = 0;  // Optional flags
		beginInfo.pInheritanceInfo = nullptr;  // Only for secondary command buffers

		if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to begin recording command buffer {}", bufferIndex);
		}

		// Reset tracking state
		m_CurrentPipeline = VK_NULL_HANDLE;
		m_CurrentPipelineLayout = VK_NULL_HANDLE;
	}

	void CommandRecorder::EndCommandBuffer(uint32_t bufferIndex)
	{
		if (bufferIndex >= m_CommandBuffers.size())
		{
			LOG_ERROR("Invalid command buffer index: {}", bufferIndex);
			return;
		}

		VkCommandBuffer cmd = m_CommandBuffers[bufferIndex];

		if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to record command buffer {}", bufferIndex);
		}
	}

	void CommandRecorder::ResetCommandBuffer(uint32_t bufferIndex)
	{
		if (bufferIndex >= m_CommandBuffers.size())
		{
			LOG_ERROR("Invalid command buffer index: {}", bufferIndex);
			return;
		}

		vkResetCommandBuffer(m_CommandBuffers[bufferIndex], 0);
	}

	void CommandRecorder::BeginRenderPass(uint32_t bufferIndex, VkRenderPass renderPass,
		VkFramebuffer framebuffer, VkExtent2D extent,
		const VkClearValue* clearValues, uint32_t clearValueCount)
	{
		if (bufferIndex >= m_CommandBuffers.size())
		{
			LOG_ERROR("Invalid command buffer index: {}", bufferIndex);
			return;
		}

		VkCommandBuffer cmd = m_CommandBuffers[bufferIndex];

		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = renderPass;
		renderPassInfo.framebuffer = framebuffer;
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = extent;

		// Use provided clear value or default black
		VkClearValue defaultClear = { {{0.0f, 0.0f, 0.0f, 1.0f}} };
		if (clearValues && clearValueCount > 0)
		{
			renderPassInfo.clearValueCount = clearValueCount;
			renderPassInfo.pClearValues = clearValues;
		}
		else
		{
			renderPassInfo.clearValueCount = 1;
			renderPassInfo.pClearValues = &defaultClear;
		}

		vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Set viewport to FULL extent
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(extent.width);
		viewport.height = static_cast<float>(extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);

		// Set scissor to FULL extent
		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = extent;
		vkCmdSetScissor(cmd, 0, 1, &scissor);
	}

	void CommandRecorder::EndRenderPass(uint32_t bufferIndex)
	{
		if (bufferIndex >= m_CommandBuffers.size())
		{
			LOG_ERROR("Invalid command buffer index: {}", bufferIndex);
			return;
		}

		vkCmdEndRenderPass(m_CommandBuffers[bufferIndex]);
	}

	void CommandRecorder::ExecuteDrawList(uint32_t bufferIndex, const DrawList& drawList,
		VulkanPipelineAdapter* pipelineManager,
		const glm::mat4& viewMatrix,
		const glm::mat4& projectionMatrix)
	{
		if (bufferIndex >= m_CommandBuffers.size())
		{
			LOG_ERROR("Invalid command buffer index: {}", bufferIndex);
			return;
		}

		if (!pipelineManager)
		{
			LOG_ERROR("No pipeline manager provided");
			return;
		}

		// Process each draw command
		for (const auto& cmd : drawList.GetCommands())
		{
			// Skip objects culled against the camera frustum. They remain in the list for the
			// shadow/reflection passes (which ignore this flag); only the visible color pass
			// honors it, so camera-frustum culling still saves main-pass work.
			if (!cmd.cameraVisible)
				continue;

			ExecuteDrawCommand(bufferIndex, cmd, pipelineManager, viewMatrix, projectionMatrix);
		}
	}

	void CommandRecorder::ExecuteReflectionDrawList(uint32_t bufferIndex, const DrawList& drawList,
		VulkanPipelineAdapter* pipelineManager, VkDescriptorSet reflectionUniformSet,
		VkDescriptorSet cloudReflectionResultSet)
	{
		if (bufferIndex >= m_CommandBuffers.size() || !pipelineManager)
		{
			return;
		}

		for (const auto& cmd : drawList.GetCommands())
		{
			// Clouds are composited into the reflection too, but only if the caller
			// supplied the mirror-camera raymarch result set. Clouds' set-0 is its
			// raymarch-result sampler; overriding set 0 with the reflection result
			// makes the composite sample the reflected clouds, not the main-view ones.
			// The draw list is pipeline-sorted, so this Clouds command already runs
			// after the opaque geometry that writes the reflection depth buffer.
			if (cmd.pipeline == PipelineType::Clouds)
			{
				if (cloudReflectionResultSet == VK_NULL_HANDLE)
				{
					continue;
				}
				ExecuteDrawCommand(bufferIndex, cmd, pipelineManager, glm::mat4(1.0f), glm::mat4(1.0f),
					cloudReflectionResultSet);
				continue;
			}

			// Only opaque world geometry is otherwise reflected. Transparent/Water/
			// Firefly are skipped (v1) — water can't reflect itself, and the rest
			// are deferred follow-ups.
			if (cmd.pipeline != PipelineType::Mesh &&
				cmd.pipeline != PipelineType::Terrain &&
				cmd.pipeline != PipelineType::Foliage)
			{
				continue;
			}

			ExecuteDrawCommand(bufferIndex, cmd, pipelineManager, glm::mat4(1.0f), glm::mat4(1.0f),
				reflectionUniformSet);
		}
	}

	void CommandRecorder::ExecuteDrawCommand(uint32_t bufferIndex, const DrawCommand& cmd,
		VulkanPipelineAdapter* pipelineManager,
		const glm::mat4& viewMatrix,
		const glm::mat4& projectionMatrix,
		VkDescriptorSet overrideUniformSet)
	{
		VkCommandBuffer commandBuffer = m_CommandBuffers[bufferIndex];

		// Bind pipeline if needed ToDo: check if this can be a function
		VkPipeline pipeline = pipelineManager->GetVulkanManager()->GetPipeline(cmd.pipeline);
		VkPipelineLayout layout = pipelineManager->GetVulkanManager()->GetPipelineLayout(cmd.pipeline);

		if (pipeline != m_CurrentPipeline)
		{
			pipelineManager->GetVulkanManager()->BindPipeline(commandBuffer, cmd.pipeline);
			m_CurrentPipeline = pipeline;
			m_CurrentPipelineLayout = layout;
		}

		// --- Bind descriptor sets generically ---
		// The recorder is pipeline-agnostic: each DrawCommand carries its own binding
		// list (filled by the owning system, or AppendCoreBindings for the built-in
		// pipelines). Frame-global sets are resolved here rather than at submit time,
		// because their per-frame double-buffered handle isn't known to the submitter.
		if (m_CurrentPipelineLayout != VK_NULL_HANDLE)
		{
			for (const auto& binding : cmd.descriptorBindings)
			{
				VkDescriptorSet set = VK_NULL_HANDLE;
				switch (binding.source)
				{
				case BindingSource::Explicit:
					set = binding.set;
					break;
				case BindingSource::FrameUniform:
					if (m_DescriptorManager) set = m_DescriptorManager->GetUniformDescriptorSet(bufferIndex);
					break;
				case BindingSource::FrameLighting:
					if (m_DescriptorManager) set = m_DescriptorManager->GetLightingDescriptorSet(bufferIndex);
					break;
				case BindingSource::FrameShadow:
					if (m_DescriptorManager) set = m_DescriptorManager->GetShadowDescriptorSet(bufferIndex);
					break;
				case BindingSource::ReflectionInput:
					set = m_ReflectionInputSet;
					break;
				case BindingSource::CommandTexture:
					if (!cmd.textures.empty())
					{
						// Bind the texture's pre-allocated descriptor set — NO update during recording.
						VulkanTexture* vkTexture = static_cast<VulkanTexture*>(cmd.textures[0]);
						if (vkTexture && vkTexture->HasDescriptorSet())
							set = vkTexture->GetDescriptorSet();
						else
							LOG_WARN("Texture has no descriptor set - texture may not have been properly initialized");
					}
					break;
				}

				// The reflection pass substitutes the set-0 source: the mirror-flipped
				// camera for opaque geometry, or the mirror-view raymarch result for the
				// Clouds composite. overrideUniformSet is null in every other pass.
				if (binding.setIndex == 0 && overrideUniformSet != VK_NULL_HANDLE)
					set = overrideUniformSet;

				if (set != VK_NULL_HANDLE)
				{
					vkCmdBindDescriptorSets(
						commandBuffer,
						VK_PIPELINE_BIND_POINT_GRAPHICS,
						m_CurrentPipelineLayout,
						binding.setIndex,
						1,
						&set,
						0,
						nullptr
					);
				}
			}
		}

		// Set push constants if needed
		if (cmd.hasPushConstants && m_CurrentPipelineLayout != VK_NULL_HANDLE)
		{
			// Push to both vertex and fragment stages
			pipelineManager->GetVulkanManager()->PushConstants(
				commandBuffer,
				cmd.pipeline,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				&cmd.pushConstants,  // Just use directly - no modification needed
				sizeof(PushConstantData)
			);
		}

		// Bind vertex buffer
		if (cmd.vertexBuffer)
		{
			VulkanBuffer* vkBuffer = static_cast<VulkanBuffer*>(cmd.vertexBuffer);
			VkBuffer vertexBuffers[] = { vkBuffer->GetBuffer() };
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
		}

		// Execute pre-draw callback if provided
		if (cmd.preDrawCallback)
		{
			cmd.preDrawCallback();
		}

		// Draw
		if (cmd.indexBuffer && cmd.indexCount > 0)
		{
			// Indexed draw
			VulkanBuffer* vkIndexBuffer = static_cast<VulkanBuffer*>(cmd.indexBuffer);
			vkCmdBindIndexBuffer(commandBuffer, vkIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(commandBuffer, cmd.indexCount, cmd.instanceCount, 0, 0, cmd.firstInstance);
		}
		else if (cmd.vertexCount > 0)
		{
			// Non-indexed draw
			vkCmdDraw(commandBuffer, cmd.vertexCount, cmd.instanceCount, 0, cmd.firstInstance);
		}

		// Execute post-draw callback if provided
		if (cmd.postDrawCallback)
		{
			cmd.postDrawCallback();
		}
	}

	void CommandRecorder::BindPipelineIfChanged(uint32_t bufferIndex, VkPipeline pipeline)
	{
		if (pipeline != m_CurrentPipeline)
		{
			vkCmdBindPipeline(m_CommandBuffers[bufferIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			m_CurrentPipeline = pipeline;
		}
	}

	void CommandRecorder::SetPushConstants(uint32_t bufferIndex, VkPipelineLayout layout,
		const void* data, uint32_t size, VkShaderStageFlags stages)
	{
		vkCmdPushConstants(m_CommandBuffers[bufferIndex], layout, stages, 0, size, data);
	}

	void CommandRecorder::BindTextureDescriptorSet(uint32_t frameIndex, VkDescriptorSet set, VkPipelineLayout layout)
	{
		vkCmdBindDescriptorSets(
			m_CommandBuffers[frameIndex],
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			layout,
			0,  // first set
			1,  // set count
			&set,
			0,  // dynamic offset count
			nullptr
		);
	}
}