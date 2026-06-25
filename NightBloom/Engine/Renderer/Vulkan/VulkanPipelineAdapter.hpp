//------------------------------------------------------------------------------
// VulkanPipelineAdapter.hpp
//
// Vulkan implementation of the generic pipeline interface
// This adapts between generic types and Vulkan-specific types
//------------------------------------------------------------------------------

#pragma once

#include "Engine/Renderer/PipelineInterface.hpp"
#include "Engine/Renderer/Vulkan/VulkanPipeline.hpp"
#include "Engine/Renderer/Vulkan/VulkanDescriptorManager.hpp"
#include <vulkan/vulkan.h>
#include "Engine/Core/Logger/logger.hpp"

namespace Nightbloom
{
	class VulkanDescriptorManager;

	// Convert generic enums to Vulkan enums
	class VulkanEnumConverter
	{
	public:
		static VkPrimitiveTopology ToVkTopology(PrimitiveTopology topology)
		{
			switch (topology)
			{
			case PrimitiveTopology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
			case PrimitiveTopology::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			case PrimitiveTopology::LineStrip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
			case PrimitiveTopology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
			default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			}
		}

		static VkPolygonMode ToVkPolygonMode(PolygonMode mode)
		{
			switch (mode)
			{
			case PolygonMode::Fill:  return VK_POLYGON_MODE_FILL;
			case PolygonMode::Line:  return VK_POLYGON_MODE_LINE;
			case PolygonMode::Point: return VK_POLYGON_MODE_POINT;
			default: return VK_POLYGON_MODE_FILL;
			}
		}

		static VkCullModeFlags ToVkCullMode(CullMode mode)
		{
			switch (mode)
			{
			case CullMode::None:         return VK_CULL_MODE_NONE;
			case CullMode::Front:        return VK_CULL_MODE_FRONT_BIT;
			case CullMode::Back:         return VK_CULL_MODE_BACK_BIT;
			case CullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
			default: return VK_CULL_MODE_BACK_BIT;
			}
		}

		static VkFrontFace ToVkFrontFace(FrontFace face)
		{
			switch (face)
			{
			case FrontFace::Clockwise:        return VK_FRONT_FACE_CLOCKWISE;
			case FrontFace::CounterClockwise: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
			default: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
			}
		}

		static VkCompareOp ToVkCompareOp(CompareOp op)
		{
			switch (op)
			{
			case CompareOp::Never:          return VK_COMPARE_OP_NEVER;
			case CompareOp::Less:           return VK_COMPARE_OP_LESS;
			case CompareOp::Equal:          return VK_COMPARE_OP_EQUAL;
			case CompareOp::LessOrEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
			case CompareOp::Greater:        return VK_COMPARE_OP_GREATER;
			case CompareOp::NotEqual:       return VK_COMPARE_OP_NOT_EQUAL;
			case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
			case CompareOp::Always:         return VK_COMPARE_OP_ALWAYS;
			default: return VK_COMPARE_OP_LESS;
			}
		}

		static VkBlendFactor ToVkBlendFactor(BlendFactor factor)
		{
			switch (factor)
			{
			case BlendFactor::Zero:             return VK_BLEND_FACTOR_ZERO;
			case BlendFactor::One:              return VK_BLEND_FACTOR_ONE;
			case BlendFactor::SrcColor:         return VK_BLEND_FACTOR_SRC_COLOR;
			case BlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
			case BlendFactor::DstColor:         return VK_BLEND_FACTOR_DST_COLOR;
			case BlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
			case BlendFactor::SrcAlpha:         return VK_BLEND_FACTOR_SRC_ALPHA;
			case BlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			case BlendFactor::DstAlpha:         return VK_BLEND_FACTOR_DST_ALPHA;
			case BlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
			default: return VK_BLEND_FACTOR_ONE;
			}
		}

		static VkShaderStageFlags ToVkShaderStages(ShaderStage stages)
		{
			VkShaderStageFlags vkStages = 0;

			if (static_cast<int>(stages) & static_cast<int>(ShaderStage::Vertex))
				vkStages |= VK_SHADER_STAGE_VERTEX_BIT;
			if (static_cast<int>(stages) & static_cast<int>(ShaderStage::Fragment))
				vkStages |= VK_SHADER_STAGE_FRAGMENT_BIT;
			if (static_cast<int>(stages) & static_cast<int>(ShaderStage::Geometry))
				vkStages |= VK_SHADER_STAGE_GEOMETRY_BIT;
			if (static_cast<int>(stages) & static_cast<int>(ShaderStage::Compute))
				vkStages |= VK_SHADER_STAGE_COMPUTE_BIT;
			if (static_cast<int>(stages) & static_cast<int>(ShaderStage::TessControl))
				vkStages |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
			if (static_cast<int>(stages) & static_cast<int>(ShaderStage::TessEval))
				vkStages |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;

			return vkStages;
		}
	};
	
	// Vulkan pipeline wrapper
	class VulkanPipeline : public IPipeline
	{
	public:
		VulkanPipeline(PipelineType type, VkPipeline pipeline, VkPipelineLayout layout)
			: m_Type(type), m_Pipeline(pipeline), m_Layout(layout) {}

		bool IsValid() const override { return m_Pipeline != VK_NULL_HANDLE; }
		PipelineType GetType() const override { return m_Type; }

		VkPipeline GetVkPipeline() const { return m_Pipeline; }
		VkPipelineLayout GetVkLayout() const { return m_Layout; }

	private:
		PipelineType m_Type;
		VkPipeline m_Pipeline;
		VkPipelineLayout m_Layout;
	};

	// Adaptor class that implements the interface using VulkanPipelineManager
	class VulkanPipelineAdapter : public IPipelineManager
	{
	public:
		VulkanPipelineAdapter() = default;
		~VulkanPipelineAdapter() override = default;

		bool Initialize(VkDevice device, VkRenderPass renderPass, VkExtent2D extent, VulkanDescriptorManager* descriptorManager)
		{
			m_VulkanManager = std::make_unique<VulkanPipelineManager>();
			m_DescriptorManager = descriptorManager;
			m_DefaultRenderPass = renderPass;

			return m_VulkanManager->Initialize(device, renderPass, extent);
		}

		void SetShadowRenderPass(VkRenderPass shadowRenderPass)
		{
			m_ShadowRenderPass = shadowRenderPass;
		}

		void SetPostProcessRenderPass(VkRenderPass postProcessRenderPass)
		{
			m_PostProcessRenderPass = postProcessRenderPass;
		}

		// MSAA sample count of the offscreen scene pass — applied to every
		// scene-pass pipeline so its rasterizationSamples matches the render
		// pass. The shadow and post-process passes are single-sample and are
		// excluded in CreatePipeline below.
		void SetSampleCount(VkSampleCountFlagBits sampleCount)
		{
			m_SampleCount = sampleCount;
		}

		bool CreatePipeline(PipelineType type, const PipelineConfig& config) override
		{
			// Convert generic config to vulkan config
			VulkanPipelineConfig vkConfig;

			// If shader objects provided, use them
			if (config.vertexShader)
			{
				vkConfig.vertexShader = dynamic_cast<VulkanShader*>(config.vertexShader);
			}
			if (config.fragmentShader)
			{
				vkConfig.fragmentShader = dynamic_cast<VulkanShader*>(config.fragmentShader);
			}

			vkConfig.vertexShaderPath = config.vertexShaderPath;
			vkConfig.fragmentShaderPath = config.fragmentShaderPath;
			vkConfig.geometryShaderPath = config.geometryShaderPath;
			vkConfig.computeShaderPath = config.computeShaderPath;
			vkConfig.useVertexInput = config.useVertexInput;

			// Convert enums
			vkConfig.topology = VulkanEnumConverter::ToVkTopology(config.topology);
			vkConfig.polygonMode = VulkanEnumConverter::ToVkPolygonMode(config.polygonMode);
			vkConfig.cullMode = VulkanEnumConverter::ToVkCullMode(config.cullMode);
			vkConfig.frontFace = VulkanEnumConverter::ToVkFrontFace(config.frontFace);
			vkConfig.depthTestEnable = config.depthTestEnable;
			vkConfig.depthWriteEnable = config.depthWriteEnable;
			vkConfig.depthCompareOp = VulkanEnumConverter::ToVkCompareOp(config.depthCompareOp);
			vkConfig.blendEnable = config.blendEnable;
			vkConfig.srcColorBlendFactor = VulkanEnumConverter::ToVkBlendFactor(config.srcColorBlendFactor);
			vkConfig.dstColorBlendFactor = VulkanEnumConverter::ToVkBlendFactor(config.dstColorBlendFactor);
			vkConfig.pushConstantSize = config.pushConstantSize;
			vkConfig.pushConstantStages = VulkanEnumConverter::ToVkShaderStages(config.pushConstantStages);

			vkConfig.depthBiasEnable = config.depthBiasEnable;
			vkConfig.depthBiasConstant = config.depthBiasConstant;
			vkConfig.depthBiasSlope = config.depthBiasSlope;
			vkConfig.depthBiasClamp = config.depthBiasClamp;

			vkConfig.hasColorAttachment = config.hasColorAttachment;

			if ((type == PipelineType::Shadow || type == PipelineType::TerrainShadow) && m_ShadowRenderPass != VK_NULL_HANDLE)
			{
				vkConfig.renderPass = m_ShadowRenderPass;
			}

			if (type == PipelineType::PostProcess && m_PostProcessRenderPass != VK_NULL_HANDLE)
			{
				vkConfig.renderPass = m_PostProcessRenderPass;
			}

			// MSAA sample count: scene-pass pipelines match the scene render
			// pass; the shadow and post-process passes are single-sample.
			const bool singleSamplePass =
				type == PipelineType::Shadow ||
				type == PipelineType::TerrainShadow ||
				type == PipelineType::PostProcess ||
				type == PipelineType::Compute;
			vkConfig.rasterizationSamples = singleSamplePass ? VK_SAMPLE_COUNT_1_BIT : m_SampleCount;

			if (config.useUniformBuffer && m_DescriptorManager)
			{
				VkDescriptorSetLayout uniformLayout = m_DescriptorManager->GetUniformSetLayout();
				vkConfig.descriptorSetLayouts.push_back(uniformLayout);
			}

			// NEW: Handle descriptor set layouts based on config flags
			if (config.useTextures && m_DescriptorManager)
			{
				VkDescriptorSetLayout textureLayout = m_DescriptorManager->GetTextureSetLayout();
				vkConfig.descriptorSetLayouts.push_back(textureLayout);
			}

			if (config.useFireflyStorage && m_DescriptorManager)
			{
				VkDescriptorSetLayout fireflyStorageLayout = m_DescriptorManager->GetFireflyStorageSetLayout();
				vkConfig.descriptorSetLayouts.push_back(fireflyStorageLayout);  // becomes set 1 (Firefly has no texture set)
			}

			// Foliage has no texture set either, so this also lands at set 1.
			// NOTE: set index here is positional (depends on which other
			// useX flags are set above) — Grass.vert/.frag's layout(set=N)
			// declarations must match whatever order Foliage's PipelineConfig
			// actually enables. See Renderer.cpp's Foliage pipeline block.
			if (config.useFoliageStorage && m_DescriptorManager)
			{
				VkDescriptorSetLayout foliageStorageLayout = m_DescriptorManager->GetFoliageStorageSetLayout();
				vkConfig.descriptorSetLayouts.push_back(foliageStorageLayout);
			}

			// The graphics Clouds composite pass needs nothing but this one
			// sampler — the raymarch (and the FrameUniforms/lighting data it
			// needs) moved to CloudRaymarch.comp. No useUniformBuffer/
			// useLighting set for this pipeline, so this lands at set 0.
			if (config.useCloudResult && m_DescriptorManager)
			{
				VkDescriptorSetLayout cloudResultLayout = m_DescriptorManager->GetCloudResultSetLayout();
				vkConfig.descriptorSetLayouts.push_back(cloudResultLayout);
			}

			// The PostProcess/FXAA pass needs nothing but this one sampler —
			// no useUniformBuffer/useLighting set for this pipeline, so this
			// lands at set 0, same reasoning as useCloudResult above.
			if (config.usePostProcessInput && m_DescriptorManager)
			{
				VkDescriptorSetLayout postProcessInputLayout = m_DescriptorManager->GetPostProcessInputSetLayout();
				vkConfig.descriptorSetLayouts.push_back(postProcessInputLayout);
			}

			if (config.useLighting && m_DescriptorManager)
			{
				VkDescriptorSetLayout lightingLayout = m_DescriptorManager->GetLightingSetLayout();
				vkConfig.descriptorSetLayouts.push_back(lightingLayout);
			}

			if (config.useShadowMap && m_DescriptorManager)
			{
				VkDescriptorSetLayout shadowLayout = m_DescriptorManager->GetShadowSetLayout();
				vkConfig.descriptorSetLayouts.push_back(shadowLayout);
			}

			if (config.useHeightmap && m_DescriptorManager)
			{
				VkDescriptorSetLayout heightmapLayout = m_DescriptorManager->GetHeightmapSetLayout();
				vkConfig.descriptorSetLayouts.push_back(heightmapLayout);  // becomes set 4
			}

			// Reflection input is pushed last so that for the Water pipeline
			// (uniform + lighting + reflection enabled, nothing else) it lands at
			// set 2 — see Water.frag's layout(set=2) declaration.
			if (config.useReflectionInput && m_DescriptorManager)
			{
				VkDescriptorSetLayout reflectionLayout = m_DescriptorManager->GetReflectionInputSetLayout();
				vkConfig.descriptorSetLayouts.push_back(reflectionLayout);
			}

			LOG_INFO("Creating pipeline with {} descriptor set layouts", vkConfig.descriptorSetLayouts.size());
			for (size_t i = 0; i < vkConfig.descriptorSetLayouts.size(); ++i)
			{
				LOG_INFO("  Set {}: layout = {}", i, (void*)vkConfig.descriptorSetLayouts[i]);
			}

			bool success = m_VulkanManager->CreatePipeline(type, vkConfig);

			if (success)
			{
				// Create wrapper
				VkPipeline vkPipeline = m_VulkanManager->GetPipeline(type);
				VkPipelineLayout vkLayout = m_VulkanManager->GetPipelineLayout(type);
				m_Pipelines[type] = std::make_unique<VulkanPipeline>(type, vkPipeline, vkLayout);
			}

			return success;
		}

		bool DestroyPipeline(PipelineType type) override
		{
			// VulkanPipelineManager handles destruction internally
			m_Pipelines.erase(type);
			return true;
		}

		IPipeline* GetPipeline(PipelineType type) override
		{
			auto it = m_Pipelines.find(type);
			return (it != m_Pipelines.end()) ? it->second.get() : nullptr;
		}

		bool ReloadPipeline(PipelineType type) override
		{
			return m_VulkanManager->ReloadPipeline(type);
		}

		bool ReloadAllPipelines() override
		{
			return m_VulkanManager->ReloadAllPipelines();
		}

		void SetActivePipeline(PipelineType type) override
		{
			m_ActivePipeline = type;
			// Actual binding happens during command buffer recording
		}

		void PushConstants(PipelineType type, ShaderStage stages,
			const void* data, uint32_t size) override
		{
			// Store for later use during command buffer recording
			m_PendingPushConstants = { type, stages, data, size };
		}

		// Vulkan-specific methods for internal use
		void BindPipeline(VkCommandBuffer cmd, PipelineType type)
		{
			m_VulkanManager->BindPipeline(cmd, type);
		}

		void ApplyPushConstants(VkCommandBuffer cmd, PipelineType type,
			ShaderStage stages, const void* data, uint32_t size)
		{
			VkShaderStageFlags vkStages = VulkanEnumConverter::ToVkShaderStages(stages);
			m_VulkanManager->PushConstants(cmd, type, vkStages, data, size);
		}

		VulkanPipelineManager* GetVulkanManager() { return m_VulkanManager.get(); }

	private:
		VulkanDescriptorManager* m_DescriptorManager = nullptr;
		VkRenderPass m_DefaultRenderPass = VK_NULL_HANDLE;
		VkRenderPass m_ShadowRenderPass = VK_NULL_HANDLE;
		VkRenderPass m_PostProcessRenderPass = VK_NULL_HANDLE;
		VkSampleCountFlagBits m_SampleCount = VK_SAMPLE_COUNT_1_BIT;


		std::unique_ptr<VulkanPipelineManager> m_VulkanManager;
		std::unordered_map<PipelineType, std::unique_ptr<VulkanPipeline>> m_Pipelines;
		PipelineType m_ActivePipeline = PipelineType::Triangle;

		struct {
			PipelineType type;
			ShaderStage stages;
			const void* data;
			uint32_t size;
		} m_PendingPushConstants;
	};

} // namespace Nightbloom