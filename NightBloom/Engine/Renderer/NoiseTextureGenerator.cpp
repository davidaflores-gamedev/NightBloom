//------------------------------------------------------------------------------
// NoiseTextureGenerator.cpp
//------------------------------------------------------------------------------

#include "Engine/Renderer/NoiseTextureGenerator.hpp"

#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanMemoryManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanCommandPool.hpp"
#include "Engine/Renderer/Vulkan/VulkanDescriptorManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanTexture.hpp"
#include "Engine/Renderer/Components/ComputeDispatcher.hpp"
#include "Engine/Renderer/RenderDevice.hpp"       // TextureDesc, TextureFormat, TextureUsage
#include "Engine/Renderer/AssetManager.hpp"
#include "Engine/Core/Logger/Logger.hpp"

namespace Nightbloom
{
	NoiseTextureGenerator::NoiseTextureGenerator() = default;
	NoiseTextureGenerator::~NoiseTextureGenerator() { Cleanup(); }

	// =========================================================================
	// Initialize
	// =========================================================================

	bool NoiseTextureGenerator::Initialize(
		VulkanDevice* device,
		VulkanMemoryManager* memoryManager,
		VulkanCommandPool* commandPool,
		VulkanDescriptorManager* descriptorManager)
	{
		LOG_INFO("Initializing NoiseTextureGenerator");

		m_Device = device;
		m_MemoryManager = memoryManager;
		m_CommandPool = commandPool;
		m_DescriptorManager = descriptorManager;

		if (!CreateNoisePipeline())
		{
			LOG_ERROR("NoiseTextureGenerator: failed to create compute pipeline");
			return false;
		}
		if (!CreateNoisePipeline2D())
		{
			LOG_ERROR("NoiseTextureGenerator: failed to create compute pipeline");
			return false;
		}

		m_Initialized = true;
		LOG_INFO("NoiseTextureGenerator initialized");
		return true;
	}

	// =========================================================================
	// Cleanup
	// =========================================================================

	void NoiseTextureGenerator::Cleanup()
	{
		if (!m_Device) return;

		VkDevice device = m_Device->GetDevice();

		if (m_Pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device, m_Pipeline, nullptr);
			m_Pipeline = VK_NULL_HANDLE;
		}
		if (m_PipelineLayout != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
			m_PipelineLayout = VK_NULL_HANDLE;
		}
		if (m_Pipeline2D != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device, m_Pipeline2D, nullptr);
			m_Pipeline2D = VK_NULL_HANDLE;
		}
		if (m_PipelineLayout2D != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(device, m_PipelineLayout2D, nullptr);
			m_PipelineLayout2D = VK_NULL_HANDLE;
		}

		m_Initialized = false;
		LOG_INFO("NoiseTextureGenerator cleaned up");
	}

	// =========================================================================
	// Generate
	// =========================================================================

	VulkanTexture* NoiseTextureGenerator::Generate(const NoiseTextureDesc& desc, ComputeDispatcher* dispatcher)
	{
		if (!m_Initialized)
		{
			LOG_ERROR("NoiseTextureGenerator::Generate called before Initialize()");
			return nullptr;
		}

		if (desc.width == 0 || desc.height == 0 || desc.depth == 0)
		{
			LOG_ERROR("NoiseTextureGenerator: invalid dimensions {}x{}x{}",
				desc.width, desc.height, desc.depth);
			return nullptr;
		}

		LOG_INFO("Generating {} noise texture: {}x{}x{}, octaves={}, freq={:.2f}",
			desc.debugName, desc.width, desc.height, desc.depth,
			desc.octaves, desc.frequency);

		// ------------------------------------------------------------------
		// 1. Create the output texture (Storage | Sampled, R32F, 3D)
		//    The generator always produces 3D textures.
		//    depth = 1 results in a "flat" 3D texture (sample at z = 0.5).
		// ------------------------------------------------------------------
		auto* texture = new VulkanTexture(m_Device, m_MemoryManager);

		TextureDesc texDesc{};
		texDesc.width = desc.width;
		texDesc.height = desc.height;
		texDesc.depth = desc.depth;
		texDesc.mipLevels = 1;
		texDesc.arrayLayers = 1;
		texDesc.format = TextureFormat::RGBA32F;
		texDesc.usage = TextureUsage::Storage | TextureUsage::Sampled;
		texDesc.generateMips = false;

		bool is2D = (desc.depth == 1);

		texDesc.force3D = !is2D;  // Only force 3D for actual 3D textures

		if (!texture->Initialize(texDesc))
		{
			LOG_ERROR("NoiseTextureGenerator: failed to initialize texture");
			delete texture;
			return nullptr;
		}

		// ------------------------------------------------------------------
		// 2. Allocate a storage image descriptor set for the compute write.
		//    This is a temporary descriptor used only during generation.
		//    We update it with the image view in GENERAL layout (storage write).
		// ------------------------------------------------------------------
		VkDescriptorSet storageSet = m_DescriptorManager->AllocateComputeImageSet();
		if (storageSet == VK_NULL_HANDLE)
		{
			LOG_ERROR("NoiseTextureGenerator: failed to allocate storage image descriptor set");
			delete texture;
			return nullptr;
		}

		m_DescriptorManager->UpdateComputeImageSet(storageSet, texture->GetStorageImageView());

		// ------------------------------------------------------------------
		// 3. Build push constants
		// ------------------------------------------------------------------
		NoisePushConstants pc{};
		pc.width = desc.width;
		pc.height = desc.height;
		pc.depth = desc.depth;
		pc.seed = desc.seed;
		pc.octaves = desc.octaves;
		pc.frequency = desc.frequency;
		pc.persistence = desc.persistence;
		pc.lacunarity = desc.lacunarity;
		pc.noiseType = static_cast<uint32_t>(desc.noiseType);

		// ------------------------------------------------------------------
		// 4. Record and submit the compute pass via a single-time command
		// ------------------------------------------------------------------
		{
			VulkanSingleTimeCommand cmd(m_Device, m_CommandPool);
			VkCommandBuffer commandBuffer = cmd.Begin();

			// UNDEFINED → GENERAL (layout required for storage image writes)
			dispatcher->TransitionImageForComputeWrite(commandBuffer, texture->GetImage(), VK_IMAGE_LAYOUT_UNDEFINED);

			VkPipeline activePipeline = is2D ? m_Pipeline2D : m_Pipeline;
			VkPipelineLayout activeLayout = is2D ? m_PipelineLayout2D : m_PipelineLayout;

			// Bind our noise compute pipeline
			dispatcher->BindPipeline(commandBuffer, activePipeline);

			// Bind the storage image at set 0
			dispatcher->BindDescriptorSet(commandBuffer, activeLayout, 0, storageSet);

			// Push generation parameters
			dispatcher->PushConstants(commandBuffer, activeLayout, &pc, sizeof(NoisePushConstants));

			// Dispatch: local workgroup size is 8x8x8 (matches noise.comp)
			uint32_t gx = ComputeDispatcher::CalculateGroupCount(desc.width, 8);
			uint32_t gy = ComputeDispatcher::CalculateGroupCount(desc.height, 8);
			uint32_t gz = is2D ? 1 : ComputeDispatcher::CalculateGroupCount(desc.depth, 8);
			dispatcher->Dispatch(commandBuffer, gx, gy, gz);

			// GENERAL → SHADER_READ_ONLY_OPTIMAL so the texture is ready to sample
			dispatcher->ComputeWriteToFragmentSampleBarrier(
				commandBuffer,
				texture->GetImage());

			cmd.End(); // Submits and waits for the GPU to finish
		}

		// ------------------------------------------------------------------
	   // 5. Update the texture's tracked layout.
	   //    The compute barriers above bypassed VulkanTexture::TransitionLayout,
	   //    so we update the tracked layout manually to avoid stale state.
	   // ------------------------------------------------------------------
		texture->SetCurrentLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		// ------------------------------------------------------------------
		// 6. Create the COMBINED_IMAGE_SAMPLER descriptor set so the texture
		//    can be bound for sampling in the main pass.
		// ------------------------------------------------------------------
		if (!texture->CreateDescriptorSet(m_DescriptorManager))
		{
			LOG_WARN("NoiseTextureGenerator: CreateDescriptorSet failed for '{}'",
				desc.debugName);
			// Not fatal — callers can still bind the texture manually
		}

		// Note: storageSet is "orphaned" in the pool after this point.
		// The pool was created with VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		// so it could be freed via vkFreeDescriptorSets if we expose the pool.
		// For now it will be reclaimed when the descriptor pool resets on shutdown.
		// TODO: Add VulkanDescriptorManager::FreeDescriptorSet() to reclaim it explicitly.

		LOG_INFO("Noise texture '{}' generated successfully ({}x{}x{})",
			desc.debugName, desc.width, desc.height, desc.depth);

		return texture;
	}

	// =========================================================================
	// CreateNoisePipeline
	// =========================================================================

	bool NoiseTextureGenerator::CreateNoisePipeline()
	{
		VkDevice device = m_Device->GetDevice();

		// Load compiled SPIR-V
		auto& assetManager = AssetManager::Get();
		auto shaderCode = assetManager.LoadShaderBinary("noise.comp.spv");
		if (shaderCode.empty())
		{
			LOG_ERROR("NoiseTextureGenerator: failed to load noise.comp.spv");
			return false;
		}

		// Create shader module
		VkShaderModuleCreateInfo moduleInfo{};
		moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		moduleInfo.codeSize = shaderCode.size();
		moduleInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

		VkShaderModule shaderModule = VK_NULL_HANDLE;
		if (vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS)
		{
			LOG_ERROR("NoiseTextureGenerator: failed to create shader module");
			return false;
		}

		VkPipelineShaderStageCreateInfo stageInfo{};
		stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stageInfo.module = shaderModule;
		stageInfo.pName = "main";

		// Push constant range for NoisePushConstants
		VkPushConstantRange pushRange{};
		pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushRange.offset = 0;
		pushRange.size = sizeof(NoisePushConstants);

		// Set 0: storage image (write target)
		VkDescriptorSetLayout imageSetLayout = m_DescriptorManager->GetComputeImageSetLayout();

		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &imageSetLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushRange;

		if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
		{
			LOG_ERROR("NoiseTextureGenerator: failed to create pipeline layout");
			vkDestroyShaderModule(device, shaderModule, nullptr);
			return false;
		}

		VkComputePipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineInfo.stage = stageInfo;
		pipelineInfo.layout = m_PipelineLayout;

		VkResult result = vkCreateComputePipelines(
			device,
			VK_NULL_HANDLE,
			1,
			&pipelineInfo,
			nullptr,
			&m_Pipeline);

		// Shader module is no longer needed after pipeline creation
		vkDestroyShaderModule(device, shaderModule, nullptr);

		if (result != VK_SUCCESS)
		{
			LOG_ERROR("NoiseTextureGenerator: failed to create compute pipeline");
			vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
			m_PipelineLayout = VK_NULL_HANDLE;
			return false;
		}

		LOG_INFO("NoiseTextureGenerator: noise compute pipeline created");
		return true;
	}

	bool NoiseTextureGenerator::CreateNoisePipeline2D()
	{
		VkDevice device = m_Device->GetDevice();

		// Load compiled SPIR-V
		auto& assetManager = AssetManager::Get();
		auto shaderCode = assetManager.LoadShaderBinary("noise2d.comp.spv");
		if (shaderCode.empty())
		{
			LOG_ERROR("NoiseTextureGenerator: failed to load noise.comp.spv");
			return false;
		}

		// Create shader module
		VkShaderModuleCreateInfo moduleInfo{};
		moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		moduleInfo.codeSize = shaderCode.size();
		moduleInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

		VkShaderModule shaderModule = VK_NULL_HANDLE;
		if (vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS)
		{
			LOG_ERROR("NoiseTextureGenerator: failed to create shader module");
			return false;
		}

		VkPipelineShaderStageCreateInfo stageInfo{};
		stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stageInfo.module = shaderModule;
		stageInfo.pName = "main";

		// Push constant range for NoisePushConstants
		VkPushConstantRange pushRange{};
		pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushRange.offset = 0;
		pushRange.size = sizeof(NoisePushConstants);

		// Set 0: storage image (write target)
		VkDescriptorSetLayout imageSetLayout = m_DescriptorManager->GetComputeImageSetLayout();

		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &imageSetLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushRange;

		if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_PipelineLayout2D) != VK_SUCCESS)
		{
			LOG_ERROR("NoiseTextureGenerator: failed to create pipeline layout");
			vkDestroyShaderModule(device, shaderModule, nullptr);
			return false;
		}

		VkComputePipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineInfo.stage = stageInfo;
		pipelineInfo.layout = m_PipelineLayout2D;

		VkResult result = vkCreateComputePipelines(
			device,
			VK_NULL_HANDLE,
			1,
			&pipelineInfo,
			nullptr,
			&m_Pipeline2D);

		// Shader module is no longer needed after pipeline creation
		vkDestroyShaderModule(device, shaderModule, nullptr);

		if (result != VK_SUCCESS)
		{
			LOG_ERROR("NoiseTextureGenerator: failed to create compute pipeline");
			vkDestroyPipelineLayout(device, m_PipelineLayout2D, nullptr);
			m_PipelineLayout2D = VK_NULL_HANDLE;
			return false;
		}

		LOG_INFO("NoiseTextureGenerator: noise compute pipeline created");
		return true;
	}

} // namepsace Nightbloom
