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
			// supplied the mirror-camera raymarch result set. We swap the command's
			// set-0 source (textureDescriptorSet) to that reflection result so the
			// composite samples the reflected clouds, not the main-view ones. The
			// draw list is pipeline-sorted, so this Clouds command already runs
			// after the opaque geometry that writes the reflection depth buffer.
			if (cmd.pipeline == PipelineType::Clouds)
			{
				if (cloudReflectionResultSet == VK_NULL_HANDLE)
				{
					continue;
				}
				DrawCommand cloudCmd = cmd;
				cloudCmd.textureDescriptorSet = cloudReflectionResultSet;
				ExecuteDrawCommand(bufferIndex, cloudCmd, pipelineManager, glm::mat4(1.0f), glm::mat4(1.0f),
					reflectionUniformSet);
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

		bool pipelineUsesUniforms = (
			cmd.pipeline == PipelineType::Mesh			||
			cmd.pipeline == PipelineType::Transparent	||
			cmd.pipeline == PipelineType::NodeGenerated ||
			cmd.pipeline == PipelineType::Triangle		||
			cmd.pipeline == PipelineType::Terrain		||
			cmd.pipeline == PipelineType::Foliage		||
			cmd.pipeline == PipelineType::Firefly		);
			// Clouds excluded: the graphics composite pass only samples the
			// low-res raymarch result (see below) - it needs no FrameUniforms
			// at all, since the raymarch (and the camera math it needed)
			// moved to CloudRaymarch.comp.

		// Water also uses a set-0 uniform (the scene camera) — handled in the
		// dedicated Water block below, not here, since its other sets differ.
		if (cmd.pipeline == PipelineType::Water)
		{
			pipelineUsesUniforms = false;
		}

		if (pipelineUsesUniforms && m_DescriptorManager && m_CurrentPipelineLayout != VK_NULL_HANDLE)
		{
			// The reflection pass substitutes the mirror-flipped camera at set 0.
			VkDescriptorSet uniformSet = (overrideUniformSet != VK_NULL_HANDLE)
				? overrideUniformSet
				: m_DescriptorManager->GetUniformDescriptorSet(bufferIndex);
			vkCmdBindDescriptorSets(
				commandBuffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				m_CurrentPipelineLayout,
				0,  // set 0 for uniforms
				1,
				&uniformSet,
				0,
				nullptr
			);
		}

		// --- Bind cloud raymarch result (set 0 - Clouds' only descriptor set) ---
		// Distinct from pipelineUsesTextures below: this is set 0 (the Clouds
		// composite pipeline has no uniform set ahead of it), and the source
		// is cmd.textureDescriptorSet (set by CloudSystem::SubmitDraw to its
		// single, non-per-frame result descriptor set).
		if (cmd.pipeline == PipelineType::Clouds && m_CurrentPipelineLayout != VK_NULL_HANDLE
			&& cmd.textureDescriptorSet != VK_NULL_HANDLE)
		{
			vkCmdBindDescriptorSets(
				commandBuffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				m_CurrentPipelineLayout,
				0,  // set 0 - Clouds' only set
				1,
				&cmd.textureDescriptorSet,
				0,
				nullptr
			);
		}

		// --- Bind texture descriptor set (set 1) ---
		bool pipelineUsesTextures = (
			cmd.pipeline == PipelineType::Mesh			||
			cmd.pipeline == PipelineType::Transparent	||
			cmd.pipeline == PipelineType::NodeGenerated ||
			cmd.pipeline == PipelineType::Terrain		||
			cmd.pipeline == PipelineType::Foliage		||
			cmd.pipeline == PipelineType::Firefly		);

		if (pipelineUsesTextures && m_DescriptorManager && m_CurrentPipelineLayout != VK_NULL_HANDLE )
		{
			if (cmd.textureDescriptorSet != VK_NULL_HANDLE)
			{
				vkCmdBindDescriptorSets(
					commandBuffer,
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					m_CurrentPipelineLayout,
					1,  // set 1 for textures
					1,
					&cmd.textureDescriptorSet,
					0,
					nullptr
				);
			}
			else if (!cmd.textures.empty())
			{
				// Use the texture's own descriptor set instead of updating a shared one
				VulkanTexture* vkTexture = static_cast<VulkanTexture*>(cmd.textures[0]);

				if (vkTexture && vkTexture->HasDescriptorSet())
				{
					// Bind the texture's pre-allocated descriptor set - NO UPDATE during rendering!
					VkDescriptorSet textureSet = vkTexture->GetDescriptorSet();
					vkCmdBindDescriptorSets(
						commandBuffer,
						VK_PIPELINE_BIND_POINT_GRAPHICS,
						m_CurrentPipelineLayout,
						1,  // set 1 for textures
						1,  // set count
						&textureSet,
						0,  // dynamic offset count
						nullptr
					);
				}
				else
				{
					LOG_WARN("Texture has no descriptor set - texture may not have been properly initialized");
				}
			}
		}

		// --- Bind lighting descriptor set (set 2) ---
		// Clouds excluded: the sun/lighting read now happens in
		// CloudRaymarch.comp, not the graphics composite pass.
		bool pipelineUsesLighting = (
			cmd.pipeline == PipelineType::Mesh			||
			cmd.pipeline == PipelineType::Transparent	||
			cmd.pipeline == PipelineType::Terrain		||
			cmd.pipeline == PipelineType::Foliage		);

		if (pipelineUsesLighting && m_DescriptorManager && m_CurrentPipelineLayout != VK_NULL_HANDLE)
		{
			VkDescriptorSet lightingSet = m_DescriptorManager->GetLightingDescriptorSet(bufferIndex);
			vkCmdBindDescriptorSets(
				commandBuffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				m_CurrentPipelineLayout,
				2,  // set 2 for lighting
				1,
				&lightingSet,
				0,
				nullptr
			);
		}

		bool pipelineUsesShadowMap = (
			cmd.pipeline == PipelineType::Mesh		||
			cmd.pipeline == PipelineType::Terrain	||
			cmd.pipeline == PipelineType::Foliage	);

		if (pipelineUsesShadowMap && m_DescriptorManager && m_CurrentPipelineLayout != VK_NULL_HANDLE)
		{
			VkDescriptorSet shadowSet = m_DescriptorManager->GetShadowDescriptorSet(bufferIndex);
			if (shadowSet != VK_NULL_HANDLE)
			{
				vkCmdBindDescriptorSets(
					commandBuffer,
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					m_CurrentPipelineLayout,
					3,  // set 3 for shadow map
					1,
					&shadowSet,
					0,
					nullptr
				);
			}
		}

		bool pipelineUsesHeightmap = (
			cmd.pipeline == PipelineType::Terrain	||
			cmd.pipeline == PipelineType::Foliage	);

		if (pipelineUsesHeightmap && cmd.heightmapDescriptorSet != VK_NULL_HANDLE
			&& m_CurrentPipelineLayout != VK_NULL_HANDLE)
		{
			vkCmdBindDescriptorSets(
				commandBuffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				m_CurrentPipelineLayout,
				4,   // set 4
				1,
				&cmd.heightmapDescriptorSet,
				0,
				nullptr
			);
		}

		// --- Water descriptor sets (set 0 = scene uniform, set 1 = lighting,
		//     set 2 = reflection target). Water's set layout differs from the
		//     Mesh/Terrain convention (no texture set, lighting at set 1), so it
		//     gets its own block rather than reusing the generic chain above. ---
		if (cmd.pipeline == PipelineType::Water && m_DescriptorManager && m_CurrentPipelineLayout != VK_NULL_HANDLE)
		{
			VkDescriptorSet uniformSet = m_DescriptorManager->GetUniformDescriptorSet(bufferIndex);
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				m_CurrentPipelineLayout, 0, 1, &uniformSet, 0, nullptr);

			VkDescriptorSet lightingSet = m_DescriptorManager->GetLightingDescriptorSet(bufferIndex);
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				m_CurrentPipelineLayout, 1, 1, &lightingSet, 0, nullptr);

			if (m_ReflectionInputSet != VK_NULL_HANDLE)
			{
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
					m_CurrentPipelineLayout, 2, 1, &m_ReflectionInputSet, 0, nullptr);
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