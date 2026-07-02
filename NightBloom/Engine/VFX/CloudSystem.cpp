//------------------------------------------------------------------------------
// CloudSystem.cpp
//------------------------------------------------------------------------------

#include "Engine/VFX/CloudSystem.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/Components/ResourceManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanDescriptorManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanTexture.hpp"
#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Components/ComputeDispatcher.hpp"
#include "Engine/Renderer/AssetManager.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace Nightbloom
{
	bool CloudSystem::Initialize(Renderer* renderer)
	{
		if (!renderer)
		{
			LOG_ERROR("CloudSystem::Initialize — null renderer");
			return false;
		}

		m_Renderer = renderer;
		m_Resources = renderer->GetResourceManager();
		m_DescriptorManager = renderer->GetDescriptorManager();

		if (!m_Resources || !m_DescriptorManager)
		{
			LOG_ERROR("CloudSystem::Initialize — renderer subsystems not ready");
			return false;
		}

		for (uint32_t i = 0; i < 2; ++i)
		{
			m_ParamsBuffers[i] = m_Resources->CreateUniformBuffer(
				"CloudParams_" + std::to_string(i), sizeof(CloudParamsData));
			if (!m_ParamsBuffers[i])
			{
				LOG_ERROR("CloudSystem: failed to create params UBO {}", i);
				return false;
			}

			m_DescriptorManager->UpdateCloudParamsBinding(i, m_ParamsBuffers[i]->GetBuffer(), sizeof(CloudParamsData));
		}

		m_ResultDescriptorSet = m_DescriptorManager->AllocateCloudResultSet();
		if (m_ResultDescriptorSet == VK_NULL_HANDLE)
		{
			LOG_ERROR("CloudSystem: failed to allocate cloud result descriptor set");
			return false;
		}

		m_OutputImageSet = m_DescriptorManager->AllocateComputeImageSet();
		if (m_OutputImageSet == VK_NULL_HANDLE)
		{
			LOG_ERROR("CloudSystem: failed to allocate compute output image set");
			return false;
		}

		// Reflection result: a second sampled/storage set pair, written by the
		// mirror-camera raymarch and composited into the water reflection target.
		m_ReflectionResultSet = m_DescriptorManager->AllocateCloudResultSet();
		m_ReflectionOutputImageSet = m_DescriptorManager->AllocateComputeImageSet();
		if (m_ReflectionResultSet == VK_NULL_HANDLE || m_ReflectionOutputImageSet == VK_NULL_HANDLE)
		{
			LOG_ERROR("CloudSystem: failed to allocate reflection cloud descriptor sets");
			return false;
		}

		if (!CreateComputePipeline())
		{
			LOG_ERROR("CloudSystem: failed to create raymarch compute pipeline");
			return false;
		}

		LOG_INFO("CloudSystem initialized");
		return true;
	}

	void CloudSystem::Shutdown()
	{
		if (m_Renderer)
		{
			m_Renderer->GetDevice()->WaitForIdle();
			m_Renderer->SetCloudSystem(nullptr); // avoid a dangling pointer once we destroy our resources below
		}

		VkDevice device = m_Renderer ? m_Renderer->GetVkDevice() : VK_NULL_HANDLE;
		if (device != VK_NULL_HANDLE)
		{
			if (m_RaymarchPipeline != VK_NULL_HANDLE)
			{
				vkDestroyPipeline(device, m_RaymarchPipeline, nullptr);
				m_RaymarchPipeline = VK_NULL_HANDLE;
			}
			if (m_RaymarchPipelineLayout != VK_NULL_HANDLE)
			{
				vkDestroyPipelineLayout(device, m_RaymarchPipelineLayout, nullptr);
				m_RaymarchPipelineLayout = VK_NULL_HANDLE;
			}
		}

		DestroyNoiseTextures();
		DestroyResultImage();
		m_Ready = false;

		LOG_INFO("CloudSystem shut down");
	}

	void CloudSystem::DestroyNoiseTextures()
	{
		if (m_ShapeTexture)
		{
			delete m_ShapeTexture;
			m_ShapeTexture = nullptr;
		}
		if (m_DetailTexture)
		{
			delete m_DetailTexture;
			m_DetailTexture = nullptr;
		}
	}

	void CloudSystem::DestroyResultImage()
	{
		if (m_RaymarchResult)
		{
			delete m_RaymarchResult;
			m_RaymarchResult = nullptr;
		}
		if (m_ReflectionResult)
		{
			delete m_ReflectionResult;
			m_ReflectionResult = nullptr;
		}
		m_ResultWidth = 0;
		m_ResultHeight = 0;
		m_ResultImageEverWritten = false;
		m_ReflectionResultEverWritten = false;
	}

	bool CloudSystem::Regenerate(const CloudDesc& desc)
	{
		m_Ready = false;

		m_Renderer->WaitForIdle();

		NoiseTextureGenerator* noiseGen = m_Renderer->GetNoiseGenerator();
		ComputeDispatcher* dispatch = m_Renderer->GetComputeDispatcher();

		if (!noiseGen || !dispatch)
		{
			LOG_ERROR("CloudSystem::Regenerate — noise generator not available");
			return false;
		}

		DestroyNoiseTextures();

		NoiseTextureDesc shapeDesc = desc.shapeNoise;
		shapeDesc.debugName = "CloudShape";
		m_ShapeTexture = noiseGen->Generate(shapeDesc, dispatch);
		if (!m_ShapeTexture)
		{
			LOG_ERROR("CloudSystem::Regenerate — shape noise generation failed");
			return false;
		}

		NoiseTextureDesc detailDesc = desc.detailNoise;
		detailDesc.debugName = "CloudDetail";
		m_DetailTexture = noiseGen->Generate(detailDesc, dispatch);
		if (!m_DetailTexture)
		{
			LOG_ERROR("CloudSystem::Regenerate — detail noise generation failed");
			DestroyNoiseTextures();
			return false;
		}

		for (uint32_t i = 0; i < 2; ++i)
		{
			m_DescriptorManager->UpdateCloudTextureBindings(i, m_ShapeTexture, m_DetailTexture);
		}

		m_CurrentDesc = desc;

		if (!ResizeResultImage(m_Renderer->GetWidth(), m_Renderer->GetHeight()))
		{
			LOG_ERROR("CloudSystem::Regenerate — failed to create raymarch result image");
			return false;
		}

		m_Ready = true;

		LOG_INFO("CloudSystem: noise textures regenerated (shape {}x{}x{}, detail {}x{}x{})",
			shapeDesc.width, shapeDesc.height, shapeDesc.depth,
			detailDesc.width, detailDesc.height, detailDesc.depth);

		return true;
	}

	bool CloudSystem::ResizeResultImage(uint32_t viewportWidth, uint32_t viewportHeight)
	{
		if (viewportWidth == 0 || viewportHeight == 0) return true; // minimized window etc - nothing to do yet

		uint32_t newWidth = std::max(1u, static_cast<uint32_t>(viewportWidth * m_CurrentDesc.resolutionScale));
		uint32_t newHeight = std::max(1u, static_cast<uint32_t>(viewportHeight * m_CurrentDesc.resolutionScale));

		if (m_RaymarchResult && newWidth == m_ResultWidth && newHeight == m_ResultHeight)
			return true; // already the right size

		m_Renderer->WaitForIdle();
		DestroyResultImage();

		auto* vkDevice = static_cast<VulkanDevice*>(m_Renderer->GetDevice());

		TextureDesc texDesc{};
		texDesc.width = newWidth;
		texDesc.height = newHeight;
		texDesc.depth = 1;
		texDesc.mipLevels = 1;
		texDesc.arrayLayers = 1;
		texDesc.format = TextureFormat::RGBA16F;
		texDesc.usage = TextureUsage::Storage | TextureUsage::Sampled;
		texDesc.generateMips = false;
		texDesc.force3D = false;

		// Helper: allocate + initialize one low-res result image.
		auto createResult = [&](const char* label) -> VulkanTexture*
		{
			auto* tex = new VulkanTexture(vkDevice, m_Renderer->GetMemoryManager());
			if (!tex->Initialize(texDesc))
			{
				LOG_ERROR("CloudSystem: failed to initialize {} result image ({}x{})", label, newWidth, newHeight);
				delete tex;
				return nullptr;
			}
			return tex;
		};

		m_RaymarchResult = createResult("raymarch");
		m_ReflectionResult = createResult("reflection");
		if (!m_RaymarchResult || !m_ReflectionResult)
		{
			DestroyResultImage();
			return false;
		}

		m_ResultWidth = newWidth;
		m_ResultHeight = newHeight;

		m_DescriptorManager->UpdateCloudResultSet(m_ResultDescriptorSet, m_RaymarchResult);
		m_DescriptorManager->UpdateComputeImageSet(m_OutputImageSet, m_RaymarchResult->GetStorageImageView());
		m_DescriptorManager->UpdateCloudResultSet(m_ReflectionResultSet, m_ReflectionResult);
		m_DescriptorManager->UpdateComputeImageSet(m_ReflectionOutputImageSet, m_ReflectionResult->GetStorageImageView());

		LOG_INFO("CloudSystem: raymarch result image (re)created ({}x{}, scale={:.2f})",
			newWidth, newHeight, m_CurrentDesc.resolutionScale);

		return true;
	}

	VkImage CloudSystem::GetRaymarchResultImage() const
	{
		return m_RaymarchResult ? m_RaymarchResult->GetImage() : VK_NULL_HANDLE;
	}

	VkImage CloudSystem::GetReflectionResultImage() const
	{
		return m_ReflectionResult ? m_ReflectionResult->GetImage() : VK_NULL_HANDLE;
	}

	void CloudSystem::UpdateParams(uint32_t frameIndex, float deltaTime)
	{
		if (!m_Ready) return;

		m_TotalTime += deltaTime;

		CloudParamsData params;
		params.layerBounds = glm::vec4(m_CurrentDesc.layerMinY, m_CurrentDesc.layerMaxY, 0.0f, 0.0f);

		glm::vec3 wind = glm::normalize(m_CurrentDesc.windDirection) * m_CurrentDesc.windSpeed;
		params.wind = glm::vec4(wind, m_TotalTime);

		params.shape = glm::vec4(
			m_CurrentDesc.shapeScale, m_CurrentDesc.detailScale,
			m_CurrentDesc.detailStrength, m_CurrentDesc.coverage);

		params.density = glm::vec4(
			m_CurrentDesc.densityMultiplier, m_CurrentDesc.extinctionCoefficient,
			m_CurrentDesc.hgAnisotropy, static_cast<float>(m_CurrentDesc.stepCount));

		void* mapped = m_ParamsBuffers[frameIndex]->GetPersistentMappedPtr();
		if (mapped)
		{
			memcpy(mapped, &params, sizeof(CloudParamsData));
			m_ParamsBuffers[frameIndex]->Flush();
		}
	}

	void CloudSystem::DispatchRaymarch(VkCommandBuffer cmd, ComputeDispatcher* dispatcher, uint32_t frameIndex)
	{
		if (!m_Ready || !dispatcher || !m_RaymarchResult) return;

		VkImageLayout oldLayout = m_ResultImageEverWritten
			? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			: VK_IMAGE_LAYOUT_UNDEFINED;
		dispatcher->TransitionImageForComputeWrite(cmd, m_RaymarchResult->GetImage(), oldLayout);
		m_ResultImageEverWritten = true;

		dispatcher->BindPipeline(cmd, m_RaymarchPipeline);
		dispatcher->BindDescriptorSet(cmd, m_RaymarchPipelineLayout, 0, m_DescriptorManager->GetUniformDescriptorSet(frameIndex));
		dispatcher->BindDescriptorSet(cmd, m_RaymarchPipelineLayout, 1, m_DescriptorManager->GetCloudDescriptorSet(frameIndex));
		dispatcher->BindDescriptorSet(cmd, m_RaymarchPipelineLayout, 2, m_DescriptorManager->GetLightingDescriptorSet(frameIndex));
		dispatcher->BindDescriptorSet(cmd, m_RaymarchPipelineLayout, 3, m_OutputImageSet);

		uint32_t groupsX = ComputeDispatcher::CalculateGroupCount(m_ResultWidth, 8);
		uint32_t groupsY = ComputeDispatcher::CalculateGroupCount(m_ResultHeight, 8);
		dispatcher->Dispatch(cmd, groupsX, groupsY, 1);
	}

	void CloudSystem::DispatchReflectionRaymarch(VkCommandBuffer cmd, ComputeDispatcher* dispatcher,
		uint32_t frameIndex, VkDescriptorSet reflectionUniformSet)
	{
		if (!m_Ready || !dispatcher || !m_ReflectionResult || reflectionUniformSet == VK_NULL_HANDLE) return;

		VkImageLayout oldLayout = m_ReflectionResultEverWritten
			? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			: VK_IMAGE_LAYOUT_UNDEFINED;
		dispatcher->TransitionImageForComputeWrite(cmd, m_ReflectionResult->GetImage(), oldLayout);
		m_ReflectionResultEverWritten = true;

		dispatcher->BindPipeline(cmd, m_RaymarchPipeline);
		// set 0 = the mirror-flipped reflection camera (invView/invProj/cameraPos
		// the raymarch reconstructs rays from) — the ONLY difference from the main
		// dispatch. Params/noise (set 1) and lighting (set 2) are identical.
		dispatcher->BindDescriptorSet(cmd, m_RaymarchPipelineLayout, 0, reflectionUniformSet);
		dispatcher->BindDescriptorSet(cmd, m_RaymarchPipelineLayout, 1, m_DescriptorManager->GetCloudDescriptorSet(frameIndex));
		dispatcher->BindDescriptorSet(cmd, m_RaymarchPipelineLayout, 2, m_DescriptorManager->GetLightingDescriptorSet(frameIndex));
		dispatcher->BindDescriptorSet(cmd, m_RaymarchPipelineLayout, 3, m_ReflectionOutputImageSet);

		uint32_t groupsX = ComputeDispatcher::CalculateGroupCount(m_ResultWidth, 8);
		uint32_t groupsY = ComputeDispatcher::CalculateGroupCount(m_ResultHeight, 8);
		dispatcher->Dispatch(cmd, groupsX, groupsY, 1);
	}

	void CloudSystem::SubmitDraw(DrawList& drawList) const
	{
		if (!m_Ready) return;

		// No vertex/index buffer — Clouds.vert generates a full-screen
		// triangle procedurally from gl_VertexIndex. The composite fragment
		// shader samples m_RaymarchResult via m_ResultDescriptorSet (bound
		// by CommandRecorder using PipelineType::Clouds - see CommandRecorder.cpp).
		DrawCommand cmd;
		cmd.pipeline = PipelineType::Clouds;
		cmd.vertexBuffer = nullptr;
		cmd.indexBuffer = nullptr;
		cmd.vertexCount = 3;
		cmd.instanceCount = 1;
		cmd.hasPushConstants = false;
		cmd.textureDescriptorSet = m_ResultDescriptorSet;

		drawList.AddCommand(cmd);
	}

	bool CloudSystem::CreateComputePipeline()
	{
		VkDevice device = m_Renderer->GetVkDevice();

		auto& assetManager = AssetManager::Get();
		auto shaderCode = assetManager.LoadShaderBinary("CloudRaymarch.comp.spv");
		if (shaderCode.empty())
		{
			LOG_ERROR("CloudSystem: failed to load CloudRaymarch.comp.spv");
			return false;
		}

		VkShaderModuleCreateInfo moduleInfo{};
		moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		moduleInfo.codeSize = shaderCode.size();
		moduleInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

		VkShaderModule shaderModule = VK_NULL_HANDLE;
		if (vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS)
		{
			LOG_ERROR("CloudSystem: failed to create compute shader module");
			return false;
		}

		VkPipelineShaderStageCreateInfo stageInfo{};
		stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stageInfo.module = shaderModule;
		stageInfo.pName = "main";

		// Matches CloudRaymarch.comp's set layout: 0=FrameUniforms,
		// 1=cloud shape/detail/params, 2=SceneLighting, 3=output image.
		VkDescriptorSetLayout setLayouts[4] = {
			m_DescriptorManager->GetUniformSetLayout(),
			m_DescriptorManager->GetCloudSetLayout(),
			m_DescriptorManager->GetLightingSetLayout(),
			m_DescriptorManager->GetComputeImageSetLayout()
		};

		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 4;
		layoutInfo.pSetLayouts = setLayouts;

		if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_RaymarchPipelineLayout) != VK_SUCCESS)
		{
			LOG_ERROR("CloudSystem: failed to create raymarch pipeline layout");
			vkDestroyShaderModule(device, shaderModule, nullptr);
			return false;
		}

		VkComputePipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineInfo.stage = stageInfo;
		pipelineInfo.layout = m_RaymarchPipelineLayout;

		VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_RaymarchPipeline);

		vkDestroyShaderModule(device, shaderModule, nullptr);

		if (result != VK_SUCCESS)
		{
			LOG_ERROR("CloudSystem: failed to create raymarch compute pipeline");
			vkDestroyPipelineLayout(device, m_RaymarchPipelineLayout, nullptr);
			m_RaymarchPipelineLayout = VK_NULL_HANDLE;
			return false;
		}

		LOG_INFO("CloudSystem: raymarch compute pipeline created");
		return true;
	}

} // namespace Nightbloom
