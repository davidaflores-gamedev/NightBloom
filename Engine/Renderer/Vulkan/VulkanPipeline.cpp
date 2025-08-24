#include "Engine/Renderer/Vulkan/VulkanPipeline.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include "Engine/Renderer/AssetManager.hpp"
#include "Engine/Renderer/Vertex.hpp"

namespace Nightbloom
{
	bool VulkanPipelineManager::Initialize(VkDevice device, VkRenderPass defaultRenderPass, VkExtent2D extent) {
		m_Device = device;
		m_DefaultRenderPass = defaultRenderPass;
		m_Extent = extent;

		// Set up pipeline names for debugging
		m_PipelineNames[PipelineType::Triangle] = "Triangle";
		m_PipelineNames[PipelineType::Mesh] = "Mesh";
		m_PipelineNames[PipelineType::Shadow] = "Shadow";
		m_PipelineNames[PipelineType::Skybox] = "Skybox";
		m_PipelineNames[PipelineType::Volumetric] = "Volumetric";
		m_PipelineNames[PipelineType::PostProcess] = "PostProcess";
		m_PipelineNames[PipelineType::Compute] = "Compute";

		LOG_INFO("VulkanPipelineManager initialized");
		return true;
	}

	bool VulkanPipelineManager::CreatePipeline(PipelineType type, const VulkanPipelineConfig& config) {
		size_t index = static_cast<size_t>(type);
		if (index >= m_Pipelines.size()) {
			LOG_ERROR("Invalid pipeline type");
			return false;
		}

		// Destroy existing pipeline if it exists
		if (m_Pipelines[index].isValid) {
			DestroyPipeline(m_Pipelines[index]);
		}

		// Store config for hot reload
		m_Pipelines[index].config = config;

		// Create appropriate pipeline type
		bool success = false;
		if (!config.computeShaderPath.empty()) {
			success = CreateComputePipeline(config, m_Pipelines[index]);
		}
		else {
			success = CreateGraphicsPipeline(config, m_Pipelines[index]);
		}

		if (success) {
			m_Pipelines[index].isValid = true;
			LOG_INFO("Created {} pipeline", m_PipelineNames[type]);
		}
		else {
			LOG_ERROR("Failed to create {} pipeline", m_PipelineNames[type]);
		}

		return success;
	}

	bool VulkanPipelineManager::CreateGraphicsPipeline(const VulkanPipelineConfig& config, Pipeline& pipeline) {
		// Load shaders
		auto& assetManager = AssetManager::Get();
		auto vertShaderCode = assetManager.LoadShaderBinary(config.vertexShaderPath);
		auto fragShaderCode = assetManager.LoadShaderBinary(config.fragmentShaderPath);

		if (vertShaderCode.empty() || fragShaderCode.empty()) {
			LOG_ERROR("Failed to load shaders");
			return false;
		}

		VkShaderModule vertShaderModule = CreateShaderModule(vertShaderCode);
		VkShaderModule fragShaderModule = CreateShaderModule(fragShaderCode);

		// Shader stages
		std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

		VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
		vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertShaderStageInfo.module = vertShaderModule;
		vertShaderStageInfo.pName = "main";
		shaderStages.push_back(vertShaderStageInfo);

		VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
		fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragShaderStageInfo.module = fragShaderModule;
		fragShaderStageInfo.pName = "main";
		shaderStages.push_back(fragShaderStageInfo);

		// Vertex input
		VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

		// Set up vertex bindings and attributes for vertex buffer input
		VkVertexInputBindingDescription bindingDescription{};
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;

		if (config.useVertexInput) {
			// Vertex binding description (one vertex buffer binding at index 0)
			bindingDescription.binding = 0;
			bindingDescription.stride = sizeof(VertexPCU);  // Assuming you have a Vertex struct
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			// Vertex attribute descriptions
			// Position attribute (location 0)
			VkVertexInputAttributeDescription posAttr{};
			posAttr.binding = 0;
			posAttr.location = 0;
			posAttr.format = VK_FORMAT_R32G32B32_SFLOAT;
			posAttr.offset = offsetof(VertexPCU, position);
			attributeDescriptions.push_back(posAttr);

			// Color attribute (location 1)
			VkVertexInputAttributeDescription colorAttr{};
			colorAttr.binding = 0;
			colorAttr.location = 1;
			colorAttr.format = VK_FORMAT_R32G32B32_SFLOAT;
			colorAttr.offset = offsetof(VertexPCU, color);
			attributeDescriptions.push_back(colorAttr);

			// Texture coordinate attribute (location 2)
			VkVertexInputAttributeDescription texCoordAttr{};
			texCoordAttr.binding = 0;
			texCoordAttr.location = 2;
			texCoordAttr.format = VK_FORMAT_R32G32_SFLOAT;
			texCoordAttr.offset = offsetof(VertexPCU, uv);
			attributeDescriptions.push_back(texCoordAttr);

			vertexInputInfo.vertexBindingDescriptionCount = 1;
			vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
			vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
			vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
		}
		else {
			// No vertex input (hardcoded vertices in shader)
			vertexInputInfo.vertexBindingDescriptionCount = 0;
			vertexInputInfo.pVertexBindingDescriptions = nullptr;
			vertexInputInfo.vertexAttributeDescriptionCount = 0;
			vertexInputInfo.pVertexAttributeDescriptions = nullptr;
		}

		// Input assembly
		VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = config.topology;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		// Viewport state
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(m_Extent.width);
		viewport.height = static_cast<float>(m_Extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = m_Extent;

		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.pViewports = &viewport;
		viewportState.scissorCount = 1;
		viewportState.pScissors = &scissor;

		// Rasterizer
		VkPipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = config.polygonMode;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = config.cullMode;
		rasterizer.frontFace = config.frontFace;
		rasterizer.depthBiasEnable = VK_FALSE;

		// Multisampling
		VkPipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// Depth stencil
		VkPipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable = config.depthTestEnable;
		depthStencil.depthWriteEnable = config.depthWriteEnable;
		depthStencil.depthCompareOp = config.depthCompareOp;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		// Color blending
		VkPipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		colorBlendAttachment.blendEnable = config.blendEnable;
		if (config.blendEnable) {
			colorBlendAttachment.srcColorBlendFactor = config.srcColorBlendFactor;
			colorBlendAttachment.dstColorBlendFactor = config.dstColorBlendFactor;
			colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
			colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
		}

		VkPipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		// Push constants
		VkPushConstantRange pushConstantRange{};
		if (config.pushConstantSize > 0) {
			pushConstantRange.stageFlags = config.pushConstantStages;
			pushConstantRange.offset = 0;
			pushConstantRange.size = config.pushConstantSize;
		}

		// Pipeline layout
		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(config.descriptorSetLayouts.size());
		pipelineLayoutInfo.pSetLayouts = config.descriptorSetLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = config.pushConstantSize > 0 ? 1 : 0;
		pipelineLayoutInfo.pPushConstantRanges = config.pushConstantSize > 0 ? &pushConstantRange : nullptr;

		if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &pipeline.layout) != VK_SUCCESS) {
			LOG_ERROR("Failed to create pipeline layout");
			vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
			vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
			return false;
		}

		// Create the graphics pipeline
		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineInfo.pStages = shaderStages.data();
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.layout = pipeline.layout;
		pipelineInfo.renderPass = config.renderPass ? config.renderPass : m_DefaultRenderPass;
		pipelineInfo.subpass = 0;

		VkResult result = vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1,
			&pipelineInfo, nullptr, &pipeline.pipeline);

		// Cleanup shader modules
		vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
		vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);

		return result == VK_SUCCESS;
	}

	bool VulkanPipelineManager::CreateComputePipeline(const VulkanPipelineConfig& config, Pipeline& outPipeline)
	{
		return false;
	}

	void VulkanPipelineManager::BindPipeline(VkCommandBuffer cmd, PipelineType type) {
		size_t index = static_cast<size_t>(type);
		if (index >= m_Pipelines.size() || !m_Pipelines[index].isValid) {
			LOG_ERROR("Attempting to bind invalid pipeline: {}", m_PipelineNames[type]);
			return;
		}

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipelines[index].pipeline);
	}

	void VulkanPipelineManager::PushConstants(VkCommandBuffer cmd, PipelineType type,
		VkShaderStageFlags stages, const void* data, uint32_t size) {
		size_t index = static_cast<size_t>(type);
		if (index >= m_Pipelines.size() || !m_Pipelines[index].isValid) {
			LOG_ERROR("Attempting to push constants to invalid pipeline");
			return;
		}

		vkCmdPushConstants(cmd, m_Pipelines[index].layout, stages, 0, size, data);
	}

	void VulkanPipelineManager::Cleanup() {
		for (auto& pipeline : m_Pipelines) {
			DestroyPipeline(pipeline);
		}
		m_Device = VK_NULL_HANDLE;
		m_DefaultRenderPass = VK_NULL_HANDLE;
		LOG_INFO("VulkanPipelineManager cleaned up");
	}

	VkShaderModule VulkanPipelineManager::CreateShaderModule(const std::vector<char>& code) {
		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shaderModule;
		if (vkCreateShaderModule(m_Device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
			LOG_ERROR("Failed to create shader module");
			return VK_NULL_HANDLE;
		}

		return shaderModule;
	}

	bool VulkanPipelineManager::ReloadPipeline(PipelineType type) {
		size_t index = static_cast<size_t>(type);
		if (index >= m_Pipelines.size() || !m_Pipelines[index].isValid) {
			LOG_ERROR("Attempting to reload invalid pipeline: {}", m_PipelineNames[type]);
			return false;
		}

		LOG_INFO("Reloading {} pipeline", m_PipelineNames[type]);
		return CreatePipeline(type, m_Pipelines[index].config);
	}

	bool VulkanPipelineManager::ReloadAllPipelines() {
		bool success = true;
		for (size_t i = 0; i < m_Pipelines.size(); ++i) {
			if (m_Pipelines[i].isValid) {
				LOG_INFO("Reloading {} pipeline", m_PipelineNames[static_cast<PipelineType>(i)]);
				success &= ReloadPipeline(static_cast<PipelineType>(i));
			}
		}
		return success;
	}

	void VulkanPipelineManager::DestroyPipeline(Pipeline& pipeline) {
		if (pipeline.pipeline != VK_NULL_HANDLE) {
			vkDestroyPipeline(m_Device, pipeline.pipeline, nullptr);
			pipeline.pipeline = VK_NULL_HANDLE;
		}
		if (pipeline.layout != VK_NULL_HANDLE) {
			vkDestroyPipelineLayout(m_Device, pipeline.layout, nullptr);
			pipeline.layout = VK_NULL_HANDLE;
		}
		pipeline.isValid = false;
		LOG_INFO("Destroyed pipeline");
	}

	VkPipeline VulkanPipelineManager::GetPipeline(PipelineType type) const {
		size_t index = static_cast<size_t>(type);
		if (index >= m_Pipelines.size() || !m_Pipelines[index].isValid) {
			//LOG_ERROR("Attempting to get invalid pipeline: {}", m_PipelineNames[type]);
			return VK_NULL_HANDLE;
		}
		return m_Pipelines[index].pipeline;
	}

	VkPipelineLayout VulkanPipelineManager::GetPipelineLayout(PipelineType type) const {
		size_t index = static_cast<size_t>(type);
		if (index >= m_Pipelines.size() || !m_Pipelines[index].isValid) {
			//LOG_ERROR("Attempting to get layout of invalid pipeline: {}", m_PipelineNames[type]);
			return VK_NULL_HANDLE;
		}
		return m_Pipelines[index].layout;
	}
}