//------------------------------------------------------------------------------
// VulkanPipelineManager.hpp
//
// Manages the creation and destruction of Vulkan graphics pipelines.
//------------------------------------------------------------------------------

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <array>
#include "Engine/Renderer/PipelineInterface.hpp"  // For PipelineType enum

namespace Nightbloom
{
	// Vulkan-specific pipeline configuration
	// This is internal to the Vulkan backend
	struct VulkanPipelineConfig
	{
		std::string vertexShaderPath;
		std::string fragmentShaderPath;
		std::string geometryShaderPath; // Optional
		std::string computeShaderPath; // For compute pipelines

		// Vertex input (define this properly later)
		bool useVertexInput = false; // false for hardcoded vertices

		// Render state 
		VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
		VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
		VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

		bool depthTestEnable = true;
		bool depthWriteEnable = true;
		VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;

		// Blending
		bool blendEnable = false;
		VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

		// Push constants
		uint32_t pushConstantSize = 0;  // 0 means no push constants
		VkShaderStageFlags pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;

		// Descriptor sets (for textures, uniform buffers, etc.)
		std::vector<VkDescriptorSetLayout> descriptorSetLayouts;

		// Optional: render pass override (if different from default)
		VkRenderPass renderPass = VK_NULL_HANDLE;
	};

	class VulkanPipelineManager
	{
	public:
		VulkanPipelineManager() = default;
		~VulkanPipelineManager() { Cleanup(); }

		// Initialize with device and default render pass
		bool Initialize(VkDevice device, VkRenderPass defaultRenderPass, VkExtent2D extent);

		// Create a pipeline with given configuration
		bool CreatePipeline(PipelineType type, const VulkanPipelineConfig& config);

		VkPipeline GetPipeline(PipelineType type) const;
		VkPipelineLayout GetPipelineLayout(PipelineType type) const;

		// Bind pipeline to commandbuffer 
		void BindPipeline(VkCommandBuffer cmd, PipelineType type);

		// Push constants helper
		void PushConstants(VkCommandBuffer cmd, PipelineType type,
			VkShaderStageFlags stages, const void* data, uint32_t size);

		// Hot reload specific pipeline
		bool ReloadPipeline(PipelineType type);

		// Hot reload all pipelines (for window resize, etc.)
		bool ReloadAllPipelines();

		void Cleanup();

	private:
		struct Pipeline
		{
			VkPipeline pipeline = VK_NULL_HANDLE;
			VkPipelineLayout layout = VK_NULL_HANDLE;
			VulkanPipelineConfig config; // Store config for hot reload
			bool isValid = false;
		};

		// Helper functions
		VkShaderModule CreateShaderModule(const std::vector<char>& code);
		bool CreateGraphicsPipeline(const VulkanPipelineConfig& config, Pipeline& outPipeline);
		bool CreateComputePipeline(const VulkanPipelineConfig& config, Pipeline& outPipeline);
		void DestroyPipeline(Pipeline& pipeline);

		// Device references
		VkDevice m_Device = VK_NULL_HANDLE;
		VkRenderPass m_DefaultRenderPass = VK_NULL_HANDLE;
		VkExtent2D m_Extent = {}; // Default extent, can be updated
		
		std::array<Pipeline, static_cast<size_t>(PipelineType::Count)> m_Pipelines;

		// For debugging
		std::unordered_map<PipelineType, std::string> m_PipelineNames;
	};
}