//------------------------------------------------------------------------------
// Renderer.cpp
//
// Abstraction layer for graphics rendering
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#include "Renderer.hpp"
#include "RenderDevice.hpp"
#include "Vulkan/VulkanDevice.hpp"
#include "Vulkan/VulkanSwapchain.hpp"
#include "Vulkan/VulkanCommandPool.hpp" 
#include "Vulkan/VulkanBuffer.hpp"
#include "Vulkan/VulkanMemoryManager.hpp"
#include "Core/Logger/Logger.hpp"
#include "Core/Assert.hpp"
#include "Core/FileUtils.hpp"
#include "Engine/Renderer/Vertex.hpp"
#include "Engine/Renderer/AssetManager.hpp"   
#include "Engine/Core/PerformanceMetrics.hpp"  
#include <filesystem>  

namespace Nightbloom
{
	struct Renderer::RendererData
	{
		std::unique_ptr<RenderDevice> device; // The rendering device (e.g., VulkanDevice)
		std::unique_ptr<VulkanSwapchain> swapchain; // Swapchain for presenting images
		std::unique_ptr<VulkanCommandPool> commandPool; // Command pool for allocating command buffers
		std::unique_ptr<VulkanBuffer> vertexBuffer; // Vertex buffer for triangle drawing
		std::unique_ptr<VulkanMemoryManager> memoryManager;

		// Graphics pipeline
		VkPipeline graphicsPipeline = VK_NULL_HANDLE; // Graphics pipeline for rendering
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE; // Pipeline layout for graphics pipeline

		// Frame data
		static const uint32_t MAX_FRAMES_IN_FLIGHT = 2;
		uint32_t currentFrame = 0;
		uint32_t currentImageIndex = 0;

		// Command buffers (one per swapchain image)
		std::vector<VkCommandBuffer> commandBuffers;

		// Synchronization objects (per frame in flight)
		std::vector<VkSemaphore> imageAvailableSemaphores;
		std::vector<VkSemaphore> renderFinishedSemaphores;
		std::vector<VkFence> inFlightFences;

		std::vector<VkFence> imagesInFlight;

		// Render pass for clearing
		VkRenderPass renderPass = VK_NULL_HANDLE; // Not used yet, but reserved for future use
		std::vector<VkFramebuffer> framebuffers; // Framebuffers for render pass

		void* windowHandle = nullptr;
		uint32_t width = 0;
		uint32_t height = 0;
		bool initialized = false;
	};

	bool Renderer::CreateCommandPool()
	{
		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());
		auto queueFamilies = vulkanDevice->GetQueueFamilyIndices();

		m_Data->commandPool = std::make_unique<VulkanCommandPool>(vulkanDevice);

		// Create with reset flag so we can reset individual command buffers
		if (!m_Data->commandPool->Initialize(
			queueFamilies.graphicsFamily.value(),
			VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT))
		{
			LOG_ERROR("Failed to create command pool");
			return false;
		}

		LOG_INFO("Command pool created");
		return true;
	}

	bool Renderer::CreateCommandBuffers()
	{
		// One command buffer per frame in flight, NOT per swapchain image
		m_Data->commandBuffers = m_Data->commandPool->AllocateCommandBuffers(RendererData::MAX_FRAMES_IN_FLIGHT);

		if (m_Data->commandBuffers.empty()) {
			LOG_ERROR("Failed to allocate command buffers");
			return false;
		}

		LOG_INFO("Allocated {} command buffers for {} frames in flight",
			m_Data->commandBuffers.size(), RendererData::MAX_FRAMES_IN_FLIGHT);
		return true;
	}

	bool Renderer::CreateSyncObjects()
	{
		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());
		VkDevice device = vulkanDevice->GetDevice();

		size_t swapchainImageCount = m_Data->swapchain->GetImages().size();

		m_Data->imageAvailableSemaphores.resize(m_Data->MAX_FRAMES_IN_FLIGHT);
		m_Data->renderFinishedSemaphores.resize(swapchainImageCount);
		m_Data->inFlightFences.resize(m_Data->MAX_FRAMES_IN_FLIGHT);

		m_Data->imagesInFlight.resize(swapchainImageCount, VK_NULL_HANDLE); // Initialize to null



		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start signaled

		// Create frame in flight semaphores and fences
		for (size_t i = 0; i < m_Data->MAX_FRAMES_IN_FLIGHT; i++) {
			if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_Data->imageAvailableSemaphores[i]) != VK_SUCCESS ||
				vkCreateFence(device, &fenceInfo, nullptr, &m_Data->inFlightFences[i]) != VK_SUCCESS)
			{
				LOG_ERROR("Failed to create synchronization objects");
				return false;
			}
		}

		// Create per swapchainimage semaphores
		for (size_t i = 0; i < swapchainImageCount; i++) {
			if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_Data->renderFinishedSemaphores[i]) != VK_SUCCESS) {
				LOG_ERROR("Failed to create render finished semaphore for image {}", i);
				return false;
			}
		}

		LOG_INFO("Created synchronization objects for {} frames in flight", m_Data->MAX_FRAMES_IN_FLIGHT);
		return true;
	}

	void Renderer::RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex)
	{
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = 0; // optional
		beginInfo.pInheritanceInfo = nullptr; // Not a secondary command buffer

		if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
			LOG_ERROR("Failed to begin recording command buffer");
			return;
		}

		// Begin render pass with clear
		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = m_Data->renderPass;
		renderPassInfo.framebuffer = m_Data->framebuffers[imageIndex];
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = m_Data->swapchain->GetExtent();

		VkClearValue clearColor = { {{0.0f, 0.0f, 0.0f, 1.0f}} };  // Purple
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues = &clearColor;

		vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Data->graphicsPipeline);

		VkBuffer vertexBuffers[] = { m_Data->vertexBuffer->GetBuffer() };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

		// Draw the triangle
		vkCmdDraw(commandBuffer, 3, 1, 0, 0); // 3 vertices, 1 instance, first vertex at index 0

		vkCmdEndRenderPass(commandBuffer);

		if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
			LOG_ERROR("Failed to record command buffer");
			return;
		}
	}

	void Renderer::PreRecordAllCommandBuffers()
	{
		for (uint32_t i = 0; i < m_Data->swapchain->GetImages().size(); i++) {
			for (uint32_t j = 0; j < m_Data->MAX_FRAMES_IN_FLIGHT; j++) {
				// Record command buffer [j] for swapchain image [i]
				RecordCommandBuffer(m_Data->commandBuffers[j], i);
			}
		}
	}

	void Renderer::DestroySyncObjects()
	{
		if (!m_Data->device) return;

		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());
		VkDevice device = vulkanDevice->GetDevice();

		for (size_t i = 0; i < m_Data->MAX_FRAMES_IN_FLIGHT; i++) {
			if (m_Data->imageAvailableSemaphores[i] != VK_NULL_HANDLE)
				vkDestroySemaphore(device, m_Data->imageAvailableSemaphores[i], nullptr);
			if (m_Data->inFlightFences[i] != VK_NULL_HANDLE)
				vkDestroyFence(device, m_Data->inFlightFences[i], nullptr);
		}

		for (auto& semaphore : m_Data->renderFinishedSemaphores) {
			if (semaphore != VK_NULL_HANDLE)
				vkDestroySemaphore(device, semaphore, nullptr);
		}

		m_Data->imageAvailableSemaphores.clear();
		m_Data->renderFinishedSemaphores.clear();
		m_Data->inFlightFences.clear();
		m_Data->imagesInFlight.clear();
	}

	void Renderer::DestroyCommandBuffers()
	{
		// Try sometime check if there's a better way of doing this or not :p
		if (!m_Data->commandPool) return;

		VulkanCommandPool* commandPool = m_Data->commandPool.get();
		commandPool->FreeCommandBuffers(m_Data->commandBuffers);
		m_Data->commandBuffers.clear();
		LOG_INFO("Command buffers destroyed");
	}

	bool Renderer::CreateRenderPass()
	{
		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());

		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = m_Data->swapchain->GetImageFormat();
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT; // No multisampling
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Clear before use
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // Store after use
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // No stencil
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // No stencil
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Initial layout
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // Layout for presentation

		VkAttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0; // Index of the color attachment
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // Layout for color attachment

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; // Graphics pipeline
		subpass.colorAttachmentCount = 1; // Single color attachment
		subpass.pColorAttachments = &colorAttachmentRef; // Reference to color attachment

		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL; // External subpass
		dependency.dstSubpass = 0; // Our subpass
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // Stage befor
		dependency.srcAccessMask = 0; // No access before
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // Stage after
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; // Write access after

		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = 1; // Single attachment
		renderPassInfo.pAttachments = &colorAttachment; // Attachment description
		renderPassInfo.subpassCount = 1; // Single subpass
		renderPassInfo.pSubpasses = &subpass; // Subpass description
		renderPassInfo.dependencyCount = 1; // Single dependency
		renderPassInfo.pDependencies = &dependency; // Dependency description

		if (vkCreateRenderPass(vulkanDevice->GetDevice(), &renderPassInfo, nullptr, &m_Data->renderPass) != VK_SUCCESS) {
			LOG_ERROR("Failed to create render pass");
			return false;
		}

		LOG_INFO("Render pass created");
		return true;
	}

	bool Renderer::CreateFramebuffers()
	{
		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());
		const auto& imageViews = m_Data->swapchain->GetImageViews();

		m_Data->framebuffers.resize(imageViews.size());

		for (size_t i = 0; i < imageViews.size(); i++) {
			VkImageView attachments[] = { imageViews[i] };

			VkFramebufferCreateInfo framebufferInfo{};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = m_Data->renderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachments;
			framebufferInfo.width = m_Data->swapchain->GetExtent().width;
			framebufferInfo.height = m_Data->swapchain->GetExtent().height;
			framebufferInfo.layers = 1;

			if (vkCreateFramebuffer(vulkanDevice->GetDevice(), &framebufferInfo, nullptr, &m_Data->framebuffers[i]) != VK_SUCCESS) {
				LOG_ERROR("Failed to create framebuffer {}", i);
				return false;
			}
		}

		LOG_INFO("Created {} framebuffers", m_Data->framebuffers.size());
		return true;
	}

	void Renderer::DestroyRenderPass()
	{
		if (!m_Data->device || !m_Data->renderPass) return;

		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());
		vkDestroyRenderPass(vulkanDevice->GetDevice(), m_Data->renderPass, nullptr);
		m_Data->renderPass = VK_NULL_HANDLE;
	}

	void Renderer::DestroyFramebuffers()
	{
		if (!m_Data->device) return;

		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());
		VkDevice device = vulkanDevice->GetDevice();

		for (auto framebuffer : m_Data->framebuffers) {
			vkDestroyFramebuffer(device, framebuffer, nullptr);
		}
		m_Data->framebuffers.clear();
	}

	VkShaderModule Renderer::CreateShaderModule(const std::vector<char>& code)
	{
		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());

		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shaderModule;
		if (vkCreateShaderModule(vulkanDevice->GetDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
			LOG_ERROR("Failed to create shader module");
			return VK_NULL_HANDLE;
		}

		return shaderModule;
	}

	bool Renderer::CreateVertexBuffer()
	{
		// Define triangle vertices (barycentric colors)
		const std::vector<VertexPCU> vertices = {
		{{-0.5f,  0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},  // Bottom left - Red
		{{ 0.5f,  0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},  // Bottom right - Green  
		{{ 0.0f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}   // Top - Blue
		};

		VkDeviceSize bufferSize = sizeof(VertexPCU) * vertices.size();
		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());

		// Create staging buffer (CPU visible)
		m_Data->vertexBuffer = std::make_unique<VulkanBuffer>(vulkanDevice, m_Data->memoryManager.get());

		// Create device-local buffer with transfer dst for staging
		if (!m_Data->vertexBuffer->CreateDeviceLocal(
			bufferSize,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
		{
			LOG_ERROR("Failed to create vertex buffer");
			return false;
		}

		// Upload data using staging buffer (handled internally by VulkanBuffer)
		if (!m_Data->vertexBuffer->UploadData(vertices.data(), bufferSize, m_Data->commandPool.get()))
		{
			LOG_ERROR("Failed to upload vertex data");
			return false;
		}

		LOG_INFO("Vertex buffer created with {} vertices using VMA", vertices.size());
		return true;
	}

	bool Renderer::CreateGraphicsPipeline()
	{
		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*> (m_Data->device.get());

		// Load shaders

		auto vertShaderCode = AssetManager::Get().LoadShaderBinary("triangle.vert");
		auto fragShaderCode = AssetManager::Get().LoadShaderBinary("triangle.frag");

		if (vertShaderCode.empty() || fragShaderCode.empty())
		{
			LOG_ERROR("Failed to load shader files");
			LOG_ERROR("Make sure triangle.vert.spv and triangle.frag.spv are in the Shaders directory");
			return false;
		}

		LOG_INFO("Loaded vertex shader: {} bytes", vertShaderCode.size());
		LOG_INFO("Loaded fragment shader: {} bytes", fragShaderCode.size());

		VkShaderModule vertShaderModule = CreateShaderModule(vertShaderCode);
		VkShaderModule fragShaderModule = CreateShaderModule(fragShaderCode);

		if (vertShaderModule == VK_NULL_HANDLE || fragShaderModule == VK_NULL_HANDLE)
		{
			LOG_ERROR("Failed to create shader modules");
			return false;
		}

		// Shader stages
		VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
		vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertShaderStageInfo.module = vertShaderModule;
		vertShaderStageInfo.pName = "main"; // Entry point in shader

		VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
		fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragShaderStageInfo.module = fragShaderModule;
		fragShaderStageInfo.pName = "main"; // Entry point in shader

		VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

		// Vertex input state
		auto bindingDescription = VertexPCU::GetBindingDescription();
		auto attributeDescriptions = VertexPCU::GetAttributeDescriptions();

		VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.vertexBindingDescriptionCount = 1; // Single binding
		vertexInputInfo.pVertexBindingDescriptions = &bindingDescription; // Binding description
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data(); // Attribute descriptions

		// Input Assembly
		VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // Triangle list
		inputAssembly.primitiveRestartEnable = VK_FALSE; // No primitive restart

		// Viewport and Scissor
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(m_Data->width);
		viewport.height = static_cast<float>(m_Data->height);
		viewport.minDepth = 0.0f; // Min depth
		viewport.maxDepth = 1.0f; // Max depth

		VkRect2D scissor{};
		scissor.offset = { 0, 0 }; // Scissor offset
		scissor.extent = m_Data->swapchain->GetExtent(); // Scissor extent

		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1; // Single viewport
		viewportState.pViewports = &viewport; // Viewport
		viewportState.scissorCount = 1; // Single scissor
		viewportState.pScissors = &scissor; // Scissor

		// Rasterization
		VkPipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE; // No depth clamping
		rasterizer.rasterizerDiscardEnable = VK_FALSE; // No discard
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL; // Fill mode
		rasterizer.lineWidth = 1.0f; // Line width
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT; // Cull back faces
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // Counter-clockwise front face
		rasterizer.depthBiasEnable = VK_FALSE; // No depth bias

		// Multisampling (disable for now)
		VkPipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE; // No sample shading
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // No multisampling

		// Color blending
		VkPipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT; // Write all components
		colorBlendAttachment.blendEnable = VK_FALSE; // No blending

		VkPipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.attachmentCount = 1; // Single attachment
		colorBlending.pAttachments = &colorBlendAttachment; // Color blend attachment

		// Pipeline layout
		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 0; // No descriptor sets
		pipelineLayoutInfo.pushConstantRangeCount = 0; // No push constants

		if (vkCreatePipelineLayout(vulkanDevice->GetDevice(), &pipelineLayoutInfo, nullptr, &m_Data->pipelineLayout) != VK_SUCCESS) {
			LOG_ERROR("Failed to create pipeline layout");
			vkDestroyShaderModule(vulkanDevice->GetDevice(), fragShaderModule, nullptr);
			vkDestroyShaderModule(vulkanDevice->GetDevice(), vertShaderModule, nullptr);
			return false;
		}

		// Create graphics pipeline
		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = 2; // Vertex and fragment shaders
		pipelineInfo.pStages = shaderStages; // Shader stages
		pipelineInfo.pVertexInputState = &vertexInputInfo; // Vertex input state
		pipelineInfo.pInputAssemblyState = &inputAssembly; // Input assembly state
		pipelineInfo.pViewportState = &viewportState; // Viewport and scissor state
		pipelineInfo.pRasterizationState = &rasterizer; // Rasterization state
		pipelineInfo.pMultisampleState = &multisampling; // Multisampling state
		pipelineInfo.pColorBlendState = &colorBlending; // Color blending state
		pipelineInfo.layout = m_Data->pipelineLayout; // Pipeline layout
		pipelineInfo.renderPass = m_Data->renderPass; // Render pass
		pipelineInfo.subpass = 0; // Subpass index

		if (vkCreateGraphicsPipelines(vulkanDevice->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_Data->graphicsPipeline) != VK_SUCCESS) {
			LOG_ERROR("Failed to create graphics pipeline");
			vkDestroyPipelineLayout(vulkanDevice->GetDevice(), m_Data->pipelineLayout, nullptr);
			m_Data->pipelineLayout = VK_NULL_HANDLE;
			vkDestroyShaderModule(vulkanDevice->GetDevice(), fragShaderModule, nullptr);
			vkDestroyShaderModule(vulkanDevice->GetDevice(), vertShaderModule, nullptr);
			return false;
		}

		// Cleanup shader modules
		vkDestroyShaderModule(vulkanDevice->GetDevice(), fragShaderModule, nullptr);
		vkDestroyShaderModule(vulkanDevice->GetDevice(), vertShaderModule, nullptr);

		LOG_INFO("Graphics pipeline created successfully");
		return true;
	}

	Renderer::Renderer() : m_Data(std::make_unique<RendererData>())
	{
		LOG_INFO("Renderer created");
	}

	Renderer::~Renderer()
	{
		if (m_Data->initialized)
		{
			Shutdown();
		}
		LOG_INFO("Renderer destroyed");
	}

	bool Renderer::Initialize(void* windowHandle, uint32_t width, uint32_t height)
	{
		//--------------------------------------------------------//
		LOG_INFO("=== Initializing Renderer ===");
		LOG_INFO("Window: {}x{}", width, height);

		// Start performance tracking
		PerformanceMetrics::Get().Reset();
		PerformanceMetrics::Get().BeginFrame();

		if (m_Data->initialized)
		{
			LOG_WARN("Renderer is already initialized");
			return true;
		}

		if (!windowHandle || width == 0 || height == 0)
		{
			LOG_ERROR("Invalid parameters for Renderer initialization");
			return false;
		}

		m_Data->windowHandle = windowHandle;
		m_Data->width = width;
		m_Data->height = height;

/////////////////////////////////////////////////////////////////
//-------------------------------------------------------------//
		#pragma region "AssetManager Initialization"

		LOG_INFO("=== Initializing Asset Manager ===");

		// Get executable path (you'll need to pass this from main.cpp)
		// For now, use current directory
		std::filesystem::path execPath = std::filesystem::current_path();

		if (!AssetManager::Get().Initialize(execPath.string()))
		{
			LOG_ERROR("Failed to initialize AssetManager");
			return false;
		}

		LOG_INFO("Asset paths configured:");
		LOG_INFO("  Shaders: {}", AssetManager::Get().GetShadersPath());
		LOG_INFO("  Textures: {}", AssetManager::Get().GetTexturesPath());
		LOG_INFO("  Models: {}", AssetManager::Get().GetModelsPath());
				 
#pragma endregion

//-------------------------------------------------------------//
		#pragma region "Renderer Initialization"

		try
		{
			LOG_INFO("=== Creating VulkanDevice ===");
			m_Data->device = std::make_unique<VulkanDevice>();

			// Initialize the device
			if (!m_Data->device->Initialize(windowHandle, width, height))
			{
				LOG_ERROR("Failed to initialize VulkanDevice");
				return false;
			}

			// Display device capabilities
			LOG_INFO("=== Device Capabilities ===");
            LOG_INFO("Min Uniform Buffer Alignment: {} bytes", 
                     m_Data->device->GetMinUniformBufferAlignment());
            LOG_INFO("Supports Geometry Shaders: {}", 
                     m_Data->device->SupportsFeature("geometry_shader"));
            LOG_INFO("Supports Tessellation: {}", 
                     m_Data->device->SupportsFeature("tessellation"));
            LOG_INFO("Supports Compute: {}", 
                     m_Data->device->SupportsFeature("compute"));
			
			LOG_INFO("Renderer Device initialized successfully");
		}
		catch (const std::exception& e)
		{
			LOG_ERROR("Renderer Device initialization failed: {}", e.what());
			return false;
		}

		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());

#pragma endregion

//-------------------------------------------------------------//
		#pragma region "Memory Manager Initialization"
		LOG_INFO("=== Creating VulkanMemoryManager ===");

		m_Data->memoryManager = std::make_unique<VulkanMemoryManager>(vulkanDevice);
		if (!m_Data->memoryManager->Initialize())
		{
			LOG_ERROR("Failed to initialize memory manager");
			return false;
		}

		// Log initial memory stats
		m_Data->memoryManager->LogMemoryStats();

#pragma endregion

//-------------------------------------------------------------//
		#pragma region "Swapchain Initialization"

		LOG_INFO("=== Creating VulkanSwapchain ===");
	
		m_Data->swapchain = std::make_unique<VulkanSwapchain>(vulkanDevice);

		if (!m_Data->swapchain->Initialize(windowHandle, width, height))
		{
			LOG_ERROR("Failed to initialize VulkanSwapchain");
			return false;
		}

		LOG_INFO("VulkanSwapchain initialized successfully");

#pragma endregion

//-------------------------------------------------------------//
		#pragma region "Render Pass Initialization"

		LOG_INFO("=== Creating Render Pass ===");

		if (!CreateRenderPass())
		{
			LOG_ERROR("Failed to create render pass");
			return false;
		}

		LOG_INFO("Render pass created successfully");

		// Create framebuffers (after render pass)
		if (!CreateFramebuffers()) {
			LOG_ERROR("Failed to create framebuffers");
			return false;
		}

		LOG_INFO("Framebuffers created successfully");

#pragma endregion

//-------------------------------------------------------------//
		#pragma region "Command Pool, Buffers and sync objects Initialization"
		if (!CreateCommandPool())
		{
			LOG_ERROR("Failed to create command pool");
			return false;
		}

		if (!CreateCommandBuffers())
		{
			LOG_ERROR("Failed to create command buffers");
			return false;
		}

		if (!CreateSyncObjects())
		{
			LOG_ERROR("Failed to create synchronization objects");
			return false;
		}

#pragma endregion

//-------------------------------------------------------------//
		#pragma region "Resources and Pipeline Initialization"
		if (!CreateVertexBuffer())
		{
			LOG_ERROR("Failed to create vertex buffer");
			return false;
		}

		if (!CreateGraphicsPipeline())
		{
			LOG_ERROR("Failed to create graphics pipeline");
			return false;
		}

#pragma endregion

//-------------------------------------------------------------//
		#pragma region "Final Setup"

		m_Data->initialized = true;

		// End initialization frame timing
		PerformanceMetrics::Get().EndFrame();

		// Log final memory stats after all resources created
		m_Data->memoryManager->LogMemoryStats();

		// Update memory metrics
		auto memStats = m_Data->memoryManager->GetMemoryStats();
		PerformanceMetrics::Get().UpdateMemoryStats(
			memStats.totalAllocatedBytes,
			memStats.totalUsedBytes
		);

		LOG_INFO("=== Renderer Initialization Complete ===");
		PerformanceMetrics::Get().LogMetrics();

		return true;

#pragma endregion

/////////////////////////////////////////////////////////////////
	}

	void Renderer::Shutdown()
	{
		if (!m_Data->initialized)
		{
			LOG_WARN("Renderer is not initialized, cannot shutdown");
			return;
		}

		LOG_INFO("=== Shutting down Renderer ===");

		if (m_Data->device)
		{
			// try this
			m_Data->device->WaitForIdle();
		}

		// Log final performance metrics
		PerformanceMetrics::Get().LogMetrics();

		// 1. Destroy pipeline first (uses render pass)
		if (m_Data->graphicsPipeline != VK_NULL_HANDLE)
		{
			VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());
			vkDestroyPipeline(vulkanDevice->GetDevice(), m_Data->graphicsPipeline, nullptr);
			m_Data->graphicsPipeline = VK_NULL_HANDLE;
			LOG_INFO("Graphics pipeline destroyed");
		}

		if (m_Data->pipelineLayout != VK_NULL_HANDLE)
		{
			VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());
			vkDestroyPipelineLayout(vulkanDevice->GetDevice(), m_Data->pipelineLayout, nullptr);
			m_Data->pipelineLayout = VK_NULL_HANDLE;
			LOG_INFO("Pipeline layout destroyed");
		}

		// 2. Destroy vertex buffer (uses VMA)
		if (m_Data->vertexBuffer)
		{
			m_Data->vertexBuffer.reset();
			LOG_INFO("Vertex buffer destroyed");
		}

		// 3. Destroy sync objects
		DestroySyncObjects();

		// 4. Destroy command buffers
		DestroyCommandBuffers();

		// 5. Destroy command pool
		if (m_Data->commandPool)
		{
			m_Data->commandPool->Shutdown();
			m_Data->commandPool.reset();
			LOG_INFO("Command pool destroyed");
		}

		// 6. Destroy framebuffers (depends on render pass and swapchain)
		DestroyFramebuffers();

		// 7. Destroy render pass
		DestroyRenderPass();

		// 8. Destroy swapchain
		if (m_Data->swapchain)
		{
			m_Data->swapchain->Shutdown();
			m_Data->swapchain.reset();
			LOG_INFO("Swapchain destroyed");
		}

		// 9. Log final memory stats before destroying VMA
		if (m_Data->memoryManager)
		{
			LOG_INFO("=== Final Memory Statistics ===");
			m_Data->memoryManager->LogMemoryStats();

			// Check for leaks
			auto stats = m_Data->memoryManager->GetMemoryStats();
			if (stats.allocationCount > 0)
			{
				LOG_WARN("Warning: {} allocations still active at shutdown!", stats.allocationCount);
			}
		}

		// 10. Destroy VMA (after all buffers/images are destroyed)
		if (m_Data->memoryManager)
		{
			m_Data->memoryManager->Shutdown();
			m_Data->memoryManager.reset();
			LOG_INFO("Memory manager destroyed");
		}


		// 11. Finally, destroy the device
		if (m_Data->device)
		{
			m_Data->device->Shutdown();
			m_Data->device.reset();
			LOG_INFO("Device destroyed");
		}

		// 12. Clean up AssetManager
		AssetManager::Get().Shutdown();

		// Reset state
		m_Data->initialized = false;
		m_Data->windowHandle = nullptr;
		//m_Data->width = 0;
		//m_Data->height = 0;

		LOG_INFO("=== Renderer Shutdown Complete ===");
	}

	void Renderer::BeginFrame()
	{
		if (!m_Data->initialized)
		{
			LOG_ERROR("Renderer not initialized, cannot begin frame");
			return;
		}

		// Start frame timing
		PerformanceMetrics::Get().BeginFrame();

		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());
		VkDevice device = vulkanDevice->GetDevice();

		// Wait for previous frame
		vkWaitForFences(
			device,
			1,
			&m_Data->inFlightFences[m_Data->currentFrame],
			VK_TRUE, // Wait for all
			UINT64_MAX // Wait indefinitely
		);

		// Acquire next image from swapchain
		bool result = m_Data->swapchain->AcquireNextImage(
			m_Data->currentImageIndex,
			m_Data->imageAvailableSemaphores[m_Data->currentFrame]
		);

		// Check if another frame is using this image
		if (m_Data->imagesInFlight[m_Data->currentImageIndex] != VK_NULL_HANDLE)
		{
			vkWaitForFences(device, 1, &m_Data->imagesInFlight[m_Data->currentImageIndex], VK_TRUE, UINT64_MAX);
		}

		m_Data->imagesInFlight[m_Data->currentImageIndex] = m_Data->inFlightFences[m_Data->currentFrame];

		if (!result || m_Data->swapchain->IsOutOfDate())
		{
			// Need to recreate swapchain
			LOG_WARN("Swapchain out of date, recreation needed");
			// TODO: Implement swapchain recreation
			return;
		}

		// Reset fence only if we're submitting work
		vkResetFences(device, 1, &m_Data->inFlightFences[m_Data->currentFrame]);

		// Start GPU timing
		PerformanceMetrics::Get().BeginGPUWork();

		// Record command buffer
		VkCommandBuffer commandBuffer = m_Data->commandBuffers[m_Data->currentFrame];
		vkResetCommandBuffer(commandBuffer, 0);
		RecordCommandBuffer(commandBuffer, m_Data->currentImageIndex);

		//LOG_TRACE("Beginning frame");
	}

	void Renderer::EndFrame()
	{
		if (!m_Data->initialized)
		{
			LOG_ERROR("Renderer not initialized, cannot end frame");
			return;
		}

		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());

		// Submit command buffer
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore waitSemaphores[] = { m_Data->imageAvailableSemaphores[m_Data->currentFrame] };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;

		VkCommandBuffer commandBuffer = m_Data->commandBuffers[m_Data->currentFrame];
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		VkSemaphore signalSemaphores[] = { m_Data->renderFinishedSemaphores[m_Data->currentImageIndex] };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		auto submitResult = vkQueueSubmit(
			vulkanDevice->GetGraphicsQueue(),
			1, // Single submit
			&submitInfo,
			m_Data->inFlightFences[m_Data->currentFrame]
		);

		if (submitResult != VK_SUCCESS)
		{
			LOG_ERROR("Failed to submit command buffer");
			return;
		}

		// End GPU timing
		PerformanceMetrics::Get().EndGPUWork();

		// Present 
		bool result = m_Data->swapchain->Present(
			m_Data->currentImageIndex,
			m_Data->renderFinishedSemaphores[m_Data->currentImageIndex]
		);

		if (!result || m_Data->swapchain->IsOutOfDate()) {
			// Need to recreate swapchain
			LOG_WARN("Swapchain out of date after present");
			// TODO: Implement swapchain recreation
		}

		// Update memory stats periodically (every 60 frames)
		static int frameCounter = 0;
		if (++frameCounter % 60 == 0)
		{
			auto memStats = m_Data->memoryManager->GetMemoryStats();
			PerformanceMetrics::Get().UpdateMemoryStats(
				memStats.totalAllocatedBytes,
				memStats.totalUsedBytes
			);
		}

		// advance to the next frame
		m_Data->currentFrame = (m_Data->currentFrame + 1) % RendererData::MAX_FRAMES_IN_FLIGHT;

		// End frame timing
		PerformanceMetrics::Get().EndFrame();

		// Log metrics every second (assuming 60 FPS)
		static int logCounter = 0;
		if (++logCounter >= 60)
		{
			PerformanceMetrics::Get().LogMetrics();

			// Check frame time variance requirement (< 0.5ms)
			float variance = PerformanceMetrics::Get().GetFrameTimeVariance();
			if (variance > 0.5f)
			{
				LOG_WARN("Frame time variance ({:.2f}ms) exceeds target (0.5ms)", variance);
			}

			logCounter = 0;
		}
	}

	void Renderer::Clear(float r, float g, float b, float a)
	{
		UNUSED(r);
		UNUSED(g);
		UNUSED(b);
		UNUSED(a);


		if (!m_Data->initialized)
		{
			LOG_ERROR("Renderer not initialized, cannot clear");
			return;
		}

		//LOG_TRACE("Clearing screen with color: ({}, {}, {}, {})", r, g, b, a);
	}

	void Renderer::DrawTriangle()
	{
		if (!m_Data->initialized)
		{
			LOG_ERROR("Renderer not initialized, cannot draw triangle");
			return;
		}

		LOG_INFO("Draw triangle");
	}

	bool Renderer::IsInitialized() const
	{
		return m_Data->initialized;
	}

	RenderDevice* Renderer::GetDevice() const
	{
		return m_Data->device.get();
	}
}



