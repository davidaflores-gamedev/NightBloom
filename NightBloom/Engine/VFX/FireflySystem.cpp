//------------------------------------------------------------------------------
// FireflySystem.cpp
//------------------------------------------------------------------------------

#include "Engine/VFX/FireflySystem.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/Components/ResourceManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanDescriptorManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"
#include "Engine/Renderer/Components/ComputeDispatcher.hpp"
#include "Engine/Renderer/AssetManager.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <random>

namespace Nightbloom
{
	static float RandomFloat(std::mt19937& rng, float min, float max)
	{
		std::uniform_real_distribution<float> dist(min, max);
		return dist(rng);
	}

	bool FireflySystem::Initialize(Renderer* renderer, uint32_t agentCount,
		const glm::vec3& center, const glm::vec3& extents)
	{
		if (!renderer)
		{
			LOG_ERROR("FireflySystem::Initialize — null renderer");
			return false;
		}

		m_Renderer = renderer;
		m_Resources = renderer->GetResourceManager();
		m_DescriptorManager = renderer->GetDescriptorManager();
		m_AgentCount = agentCount;

		if (!m_Resources || !m_DescriptorManager)
		{
			LOG_ERROR("FireflySystem::Initialize — renderer subsystems not ready");
			return false;
		}

		m_Params.boundsCenter = glm::vec4(center, 0.0f);
		m_Params.boundsExtent = glm::vec4(extents, 0.0f);
		m_Params.params4.z = static_cast<float>(agentCount);

		// ---- Generate initial agent data (CPU) -----------------------------
		std::vector<FireflyAgentData> agents(agentCount);
		std::mt19937 rng(1337);

		const glm::vec3 yellowColor(1.00f, 0.95f, 0.30f);
		const glm::vec3 greenColor(0.45f, 1.00f, 0.35f);

		for (uint32_t i = 0; i < agentCount; ++i)
		{
			FireflyAgentData& agent = agents[i];

			agent.position = center + glm::vec3(
				RandomFloat(rng, -extents.x, extents.x),
				RandomFloat(rng, -extents.y, extents.y),
				RandomFloat(rng, -extents.z, extents.z));

			agent.velocity = glm::vec3(
				RandomFloat(rng, -2.0f, 2.0f),
				RandomFloat(rng, -2.0f, 2.0f),
				RandomFloat(rng, -2.0f, 2.0f));

			agent.scale = RandomFloat(rng, 1.0f, 3.5f);
			agent.brightness = RandomFloat(rng, 0.0f, 1.0f);

			agent.blinkPhase = RandomFloat(rng, 0.0f, 6.2831853f);
			agent.blinkSpeed = RandomFloat(rng, 0.8f, 2.2f);
			agent.minBrightness = RandomFloat(rng, 0.02f, 0.20f);
			agent.maxBrightness = RandomFloat(rng, 0.65f, 1.0f);

			float colorT = RandomFloat(rng, 0.0f, 1.0f);
			agent.color = glm::mix(yellowColor, greenColor, colorT);
			agent.personality = RandomFloat(rng, 0.0f, 1.0f);
		}

		// ---- Agent storage buffer (GPU-only, uploaded once) ----------------
		VkDeviceSize agentBufferSize = agentCount * sizeof(FireflyAgentData);
		m_AgentBuffer = m_Resources->CreateStorageBuffer("FireflyAgents", agentBufferSize, false);
		if (!m_AgentBuffer)
		{
			LOG_ERROR("FireflySystem: failed to create agent storage buffer");
			return false;
		}

		if (!m_AgentBuffer->UploadData(agents.data(), agentBufferSize, 0, m_Resources->GetTransferCommandPool()))
		{
			LOG_ERROR("FireflySystem: failed to upload initial agent data");
			return false;
		}

		// ---- Params UBOs (double-buffered, CPU-writable every frame) ------
		for (uint32_t i = 0; i < 2; ++i)
		{
			m_ParamsBuffers[i] = m_Resources->CreateUniformBuffer(
				"FireflyParams_" + std::to_string(i), sizeof(FireflyParamsData));
			if (!m_ParamsBuffers[i])
			{
				LOG_ERROR("FireflySystem: failed to create params UBO {}", i);
				return false;
			}

			m_DescriptorManager->UpdateFireflyParamsSet(i, m_ParamsBuffers[i]->GetBuffer(), sizeof(FireflyParamsData));
		}

		// ---- Storage descriptor set (single, vertex+compute visible) ------
		m_StorageDescriptorSet = m_DescriptorManager->AllocateFireflyStorageSet();
		if (m_StorageDescriptorSet == VK_NULL_HANDLE)
		{
			LOG_ERROR("FireflySystem: failed to allocate storage descriptor set");
			return false;
		}
		m_DescriptorManager->UpdateFireflyStorageSet(m_StorageDescriptorSet, m_AgentBuffer->GetBuffer(), agentBufferSize);

		if (!CreateComputePipeline())
		{
			LOG_ERROR("FireflySystem: failed to create compute pipeline");
			return false;
		}

		m_Ready = true;
		LOG_INFO("FireflySystem initialized ({} agents)", agentCount);
		return true;
	}

	void FireflySystem::Shutdown()
	{
		if (m_Renderer)
		{
			m_Renderer->GetDevice()->WaitForIdle();
			m_Renderer->SetFireflySystem(nullptr); // avoid a dangling pointer once we destroy our resources below
		}

		VkDevice device = m_Renderer ? m_Renderer->GetVkDevice() : VK_NULL_HANDLE;
		if (device != VK_NULL_HANDLE)
		{
			if (m_ComputePipeline != VK_NULL_HANDLE)
			{
				vkDestroyPipeline(device, m_ComputePipeline, nullptr);
				m_ComputePipeline = VK_NULL_HANDLE;
			}
			if (m_ComputePipelineLayout != VK_NULL_HANDLE)
			{
				vkDestroyPipelineLayout(device, m_ComputePipelineLayout, nullptr);
				m_ComputePipelineLayout = VK_NULL_HANDLE;
			}
		}

		m_Ready = false;

		LOG_INFO("FireflySystem shut down");
	}

	VkBuffer FireflySystem::GetAgentBuffer() const
	{
		return m_AgentBuffer ? m_AgentBuffer->GetBuffer() : VK_NULL_HANDLE;
	}

	void FireflySystem::DispatchCompute(VkCommandBuffer cmd, ComputeDispatcher* dispatcher,
		uint32_t frameIndex, float deltaTime)
	{
		if (!m_Ready || !dispatcher) return;

		m_TotalTime += deltaTime;
		m_Params.params1.w = deltaTime;
		m_Params.params3.x = m_TotalTime;

		void* mapped = m_ParamsBuffers[frameIndex]->GetPersistentMappedPtr();
		if (mapped)
		{
			memcpy(mapped, &m_Params, sizeof(FireflyParamsData));
			m_ParamsBuffers[frameIndex]->Flush();
		}

		dispatcher->BindPipeline(cmd, m_ComputePipeline);
		dispatcher->BindDescriptorSet(cmd, m_ComputePipelineLayout, 0, m_StorageDescriptorSet);
		dispatcher->BindDescriptorSet(cmd, m_ComputePipelineLayout, 1,
			m_DescriptorManager->GetFireflyParamsDescriptorSet(frameIndex));

		uint32_t groupCount = ComputeDispatcher::CalculateGroupCount(m_AgentCount, 64);
		dispatcher->Dispatch(cmd, groupCount, 1, 1);
	}

	void FireflySystem::SubmitDraw(DrawList& drawList) const
	{
		if (!m_Ready) return;

		// No vertex/index buffer — Firefly.vert generates the billboard quad
		// procedurally from gl_VertexIndex (6 verts per instance, 2 triangles).
		DrawCommand cmd;
		cmd.pipeline = PipelineType::Firefly;
		cmd.vertexBuffer = nullptr;
		cmd.indexBuffer = nullptr;
		cmd.vertexCount = 6;
		cmd.instanceCount = m_AgentCount;
		cmd.hasPushConstants = false;
		cmd.textureDescriptorSet = m_StorageDescriptorSet;

		drawList.AddCommand(cmd);
	}

	bool FireflySystem::CreateComputePipeline()
	{
		VkDevice device = m_Renderer->GetVkDevice();

		auto& assetManager = AssetManager::Get();
		auto shaderCode = assetManager.LoadShaderBinary("FireflyUpdate.comp.spv");
		if (shaderCode.empty())
		{
			LOG_ERROR("FireflySystem: failed to load FireflyUpdate.comp.spv");
			return false;
		}

		VkShaderModuleCreateInfo moduleInfo{};
		moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		moduleInfo.codeSize = shaderCode.size();
		moduleInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

		VkShaderModule shaderModule = VK_NULL_HANDLE;
		if (vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS)
		{
			LOG_ERROR("FireflySystem: failed to create compute shader module");
			return false;
		}

		VkPipelineShaderStageCreateInfo stageInfo{};
		stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stageInfo.module = shaderModule;
		stageInfo.pName = "main";

		VkDescriptorSetLayout setLayouts[2] = {
			m_DescriptorManager->GetFireflyStorageSetLayout(),
			m_DescriptorManager->GetFireflyParamsSetLayout()
		};

		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 2;
		layoutInfo.pSetLayouts = setLayouts;

		if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_ComputePipelineLayout) != VK_SUCCESS)
		{
			LOG_ERROR("FireflySystem: failed to create compute pipeline layout");
			vkDestroyShaderModule(device, shaderModule, nullptr);
			return false;
		}

		VkComputePipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineInfo.stage = stageInfo;
		pipelineInfo.layout = m_ComputePipelineLayout;

		VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_ComputePipeline);

		vkDestroyShaderModule(device, shaderModule, nullptr);

		if (result != VK_SUCCESS)
		{
			LOG_ERROR("FireflySystem: failed to create compute pipeline");
			vkDestroyPipelineLayout(device, m_ComputePipelineLayout, nullptr);
			m_ComputePipelineLayout = VK_NULL_HANDLE;
			return false;
		}

		LOG_INFO("FireflySystem: compute pipeline created");
		return true;
	}

} // namespace Nightbloom
