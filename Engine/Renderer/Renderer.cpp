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
#include "Vulkan/VulkanPipelineAdapter.hpp"
#include "PushConstants.hpp"
#include "Core/Logger/Logger.hpp"
#include "Core/Assert.hpp"
#include "Core/FileUtils.hpp"
#include "Engine/Renderer/Vertex.hpp"
#include "Engine/Renderer/AssetManager.hpp"   
#include "Engine/Core/PerformanceMetrics.hpp"  
#include <filesystem>  

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_win32.h>

namespace Nightbloom
{
	struct Renderer::RendererData
	{
		std::unique_ptr<RenderDevice> device; // The rendering device (e.g., VulkanDevice)
		std::unique_ptr<VulkanSwapchain> swapchain; // Swapchain for presenting images
		std::unique_ptr<VulkanCommandPool> commandPool; // Command pool for allocating command buffers
		std::unique_ptr<VulkanBuffer> vertexBuffer; // Vertex buffer for triangle drawing
		std::unique_ptr<VulkanBuffer> indexBuffer; // Index buffer for triangle drawing
		uint32_t indexCount = 0; // Number of indices in the index buffer
		std::unique_ptr<VulkanMemoryManager> memoryManager;

		// ImGui
		VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;
		bool imguiInitialized = false;

		// UI controllable values
		float rotationSpeed = 0.01f;  // Move this from being hardcoded
		bool showImGuiDemo = false;

		std::unique_ptr<VulkanPipelineAdapter> pipelineAdapter;
		PipelineType currentPipeline = PipelineType::Triangle; // Current pipeline type

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

		m_Data->pipelineAdapter->BindPipeline(commandBuffer, m_Data->currentPipeline);

		// Handle push constants for mesh pipeline
		if (m_Data->currentPipeline == PipelineType::Mesh)
		{
			// Set up matrices
			PushConstantData pushConstants{};

			// Model matric - for not just identity
			static float rotation = 0.0f;
			rotation += m_Data->rotationSpeed * .001f;
			pushConstants.model = glm::rotate(glm::mat4(1.0f), rotation, glm::vec3(0.f, 1.f, 0.f));

			// View matrix - camera looking at origin froma  distance
			pushConstants.view = glm::lookAt(
				glm::vec3(3.0f, 1.0f, 3.0f),  // Move camera around
				glm::vec3(0.0f, 0.0f, 0.0f),
				glm::vec3(0.0f, 1.0f, 0.0f)
			);

			// Projection matrix - perspective projection
			VkExtent2D swapChainExtent = m_Data->swapchain->GetExtent();
			float aspectRatio = swapChainExtent.width / (float)swapChainExtent.height;
			pushConstants.proj = glm::perspective(
				glm::radians(45.0f),  // Field of view
				aspectRatio,
				0.1f,   // Near plane
				100.0f  // Far plane
			);

			// Vulkan has inverted y and half z
			pushConstants.proj[1][1] *= -1;

			// push constants to gpu
			m_Data->pipelineAdapter->GetVulkanManager()->PushConstants(
				commandBuffer,
				m_Data->currentPipeline,
				VK_SHADER_STAGE_VERTEX_BIT, // Vertex shader stage
				&pushConstants,            // Pointer to data
				sizeof(PushConstantData)   // Size of push constants
			);
		}

		VkBuffer vertexBuffers[] = { m_Data->vertexBuffer->GetBuffer() };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

		// Use indexed drawing for the cube
		if (m_Data->indexBuffer && m_Data->indexCount > 0) {
			vkCmdBindIndexBuffer(commandBuffer, m_Data->indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(commandBuffer, m_Data->indexCount, 1, 0, 0, 0);
		}
		else {
			// Fallback for triangle
			vkCmdDraw(commandBuffer, 3, 1, 0, 0);
		}

		// RENDER IMGUI HERE - INSIDE THE RENDER PASS!
		if (m_Data->imguiInitialized) {
			ImDrawData* draw_data = ImGui::GetDrawData();
			if (draw_data) {
				ImGui_ImplVulkan_RenderDrawData(draw_data, commandBuffer);
			}
		}

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
		// Define cube vertices (8 unique vertices)
		std::vector<VertexPCU> vertices = {
			// Front face (red tones)
			{{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
			{{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.5f, 0.0f}, {1.0f, 0.0f}},
			{{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.5f}, {1.0f, 1.0f}},
			{{-0.5f,  0.5f,  0.5f}, {1.0f, 0.5f, 0.5f}, {0.0f, 1.0f}},

			// Back face (blue tones)
			{{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
			{{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.5f, 1.0f}, {1.0f, 0.0f}},
			{{ 0.5f,  0.5f, -0.5f}, {0.5f, 0.0f, 1.0f}, {1.0f, 1.0f}},
			{{-0.5f,  0.5f, -0.5f}, {0.5f, 0.5f, 1.0f}, {0.0f, 1.0f}}
		};

		// Define cube indices (12 triangles, 36 indices)
		std::vector<uint32_t> indices = {
			// Front face
			0, 1, 2,  2, 3, 0,
			// Back face
			4, 6, 5,  6, 4, 7,
			// Left face
			4, 0, 3,  3, 7, 4,
			// Right face
			1, 5, 6,  6, 2, 1,
			// Top face
			3, 2, 6,  6, 7, 3,
			// Bottom face
			4, 5, 1,  1, 0, 4
		};

		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());

		// Create vertex buffer
		VkDeviceSize vertexBufferSize = sizeof(vertices[0]) * vertices.size();

		m_Data->vertexBuffer = std::make_unique<VulkanBuffer>(vulkanDevice, m_Data->memoryManager.get());

		// Create as device local (GPU only) for best performance
		if (!m_Data->vertexBuffer->CreateDeviceLocal(
			vertexBufferSize,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
		{
			LOG_ERROR("Failed to create vertex buffer");
			return false;
		}

		// Upload vertex data using the built-in staging buffer functionality
		if (!m_Data->vertexBuffer->UploadData(
			vertices.data(),
			vertexBufferSize,
			m_Data->commandPool.get()))
		{
			LOG_ERROR("Failed to upload vertex data");
			return false;
		}

		// Create index buffer using VulkanBuffer class
		VkDeviceSize indexBufferSize = sizeof(indices[0]) * indices.size();

		m_Data->indexBuffer = std::make_unique<VulkanBuffer>(vulkanDevice, m_Data->memoryManager.get());

		// Create as device local (GPU only) for best performance
		if (!m_Data->indexBuffer->CreateDeviceLocal(
			indexBufferSize,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
		{
			LOG_ERROR("Failed to create index buffer");
			return false;
		}

		// Upload index data using the built-in staging buffer functionality
		if (!m_Data->indexBuffer->UploadData(
			indices.data(),
			indexBufferSize,
			m_Data->commandPool.get()))
		{
			LOG_ERROR("Failed to upload index data");
			return false;
		}

		m_Data->indexCount = static_cast<uint32_t>(indices.size());

		LOG_INFO("Created cube with {} vertices and {} indices", vertices.size(), indices.size());
		return true;
	}

	void Renderer::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
	{
		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());

		// Allocate a temporary command buffer
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = m_Data->commandPool->GetPool();
		allocInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer;
		vkAllocateCommandBuffers(vulkanDevice->GetDevice(), &allocInfo, &commandBuffer);

		// Begin recording
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		vkBeginCommandBuffer(commandBuffer, &beginInfo);

		// Copy command
		VkBufferCopy copyRegion{};
		copyRegion.size = size;
		vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

		vkEndCommandBuffer(commandBuffer);

		// Submit and wait
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		vkQueueSubmit(vulkanDevice->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(vulkanDevice->GetGraphicsQueue());

		// Clean up
		vkFreeCommandBuffers(vulkanDevice->GetDevice(), m_Data->commandPool->GetPool(), 1, &commandBuffer);
	}

	bool Renderer::CreateGraphicsPipeline()
	{
		LOG_INFO("=== Creating Pipeline Manager ===");

		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*> (m_Data->device.get());
		VkDevice device = vulkanDevice->GetDevice();
		VkExtent2D extent = m_Data->swapchain->GetExtent();

		m_Data->pipelineAdapter = std::make_unique<VulkanPipelineAdapter>();

		if (!m_Data->pipelineAdapter->Initialize(device, m_Data->renderPass, extent))
		{
			LOG_ERROR("Failed to initialize pipeline manager");
			return false;
		}

		#pragma region "Create Triangle Pipeline"

		PipelineConfig  triangleConfig{};
		triangleConfig.vertexShaderPath = "triangle.vert";
		triangleConfig.fragmentShaderPath = "triangle.frag";
		triangleConfig.useVertexInput = true;
		triangleConfig.topology = PrimitiveTopology::TriangleList;
		triangleConfig.polygonMode = PolygonMode::Fill;
		triangleConfig.cullMode = CullMode::Back;
		triangleConfig.frontFace = FrontFace::CounterClockwise; // CHECK THIS OUT
		triangleConfig.depthTestEnable = false;  // No depth testing for simple triangle
		triangleConfig.depthWriteEnable = false;
		triangleConfig.blendEnable = false;
		triangleConfig.pushConstantSize = 0;  // No push constants for triangle

		if (!m_Data->pipelineAdapter->CreatePipeline(PipelineType::Triangle, triangleConfig))
		{
			LOG_ERROR("Failed to create triangle pipeline");
			return false;
		}
		LOG_INFO("Triangle pipeline created successfully");
#pragma endregion

#pragma region "Create Mesh Pipeline"

		PipelineConfig meshConfig{};
		meshConfig.vertexShaderPath = "Mesh.vert";
		meshConfig.fragmentShaderPath = "Mesh.frag";
		meshConfig.useVertexInput = true;
		meshConfig.topology = PrimitiveTopology::TriangleList;
		meshConfig.polygonMode = PolygonMode::Fill;
		meshConfig.cullMode = CullMode::Back;
		meshConfig.frontFace = FrontFace::CounterClockwise;
		meshConfig.depthTestEnable = true;  // Enable depth testing for mesh
		meshConfig.depthWriteEnable = true;
		meshConfig.depthCompareOp = CompareOp::Less;
		meshConfig.blendEnable = false;
		meshConfig.pushConstantSize = sizeof(PushConstantData);  // Use push constants for mesh
		meshConfig.pushConstantStages = ShaderStage::Vertex;

		if (!m_Data->pipelineAdapter->CreatePipeline(PipelineType::Mesh, meshConfig))
		{
			LOG_ERROR("Failed to create mesh pipeline");
			///return false;
		}
		else
		{
			LOG_INFO("Mesh pipeline created successfully");
		}

		LOG_INFO("=== Pipeline Manager Initialized Successfully ===");
		return true;
	}

	VkCommandBuffer Renderer::BeginSingleTimeCommands()
	{
		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = m_Data->commandPool->GetPool();
		allocInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer;
		vkAllocateCommandBuffers(vulkanDevice->GetDevice(), &allocInfo, &commandBuffer);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		vkBeginCommandBuffer(commandBuffer, &beginInfo);
		return commandBuffer;
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
		#pragma region "ImGui Initialization"

		LOG_INFO("=== Initializing ImGui ===");

		if (!CreateImGuiDescriptorPool()) {
			LOG_WARN("Failed to create ImGui descriptor pool - UI disabled");
			// Continue anyway
		}
		else {
			// Setup ImGui context
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO(); (void)io;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable keyboard navigation

			// Setup style
			ImGui::StyleColorsDark();

			// Platform/Renderer bindings
			ImGui_ImplWin32_Init(windowHandle);

			// Setup Vulkan binding
			ImGui_ImplVulkan_InitInfo init_info = {};
			init_info.Instance = vulkanDevice->GetInstance();
			init_info.PhysicalDevice = vulkanDevice->GetPhysicalDevice();
			init_info.Device = vulkanDevice->GetDevice();
			init_info.QueueFamily = vulkanDevice->GetGraphicsQueueFamily();  // Using the helper method
			init_info.Queue = vulkanDevice->GetGraphicsQueue();
			init_info.PipelineCache = VK_NULL_HANDLE;
			init_info.DescriptorPool = m_Data->imguiDescriptorPool;
			init_info.RenderPass = m_Data->renderPass;  // RenderPass is now part of InitInfo
			init_info.Subpass = 0;
			init_info.MinImageCount = 2;
			init_info.ImageCount = m_Data->swapchain->GetImageCount();
			init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
			init_info.Allocator = nullptr;
			init_info.CheckVkResultFn = nullptr;

			ImGui_ImplVulkan_Init(&init_info);

			m_Data->imguiInitialized = true;
			LOG_INFO("ImGui initialized successfully (fonts will auto-upload on first frame)");
		}

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

		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());

		// Log final performance metrics
		PerformanceMetrics::Get().LogMetrics();

		// SHUTDOWN IMGUI FIRST (before destroying descriptor pool)
		if (m_Data->imguiInitialized) {
			ImGui_ImplVulkan_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
			m_Data->imguiInitialized = false;
			LOG_INFO("ImGui shut down");
		}

		// Destroy ImGui descriptor pool
		if (m_Data->imguiDescriptorPool != VK_NULL_HANDLE) {
			vkDestroyDescriptorPool(vulkanDevice->GetDevice(), m_Data->imguiDescriptorPool, nullptr);
			m_Data->imguiDescriptorPool = VK_NULL_HANDLE;
			LOG_INFO("ImGui descriptor pool destroyed");
		}

		// 1. Destroy pipeline first (uses render pass)
		if (m_Data->pipelineAdapter)
		{
			m_Data->pipelineAdapter.reset();
			LOG_INFO("Pipeline adapter destroyed");
		}

		// 2. Destroy vertex buffer (uses VMA)
		if (m_Data->vertexBuffer)
		{
			m_Data->vertexBuffer.reset();
			LOG_INFO("Vertex buffer destroyed");
		}

		if (m_Data->indexBuffer)
		{
			m_Data->indexBuffer.reset();
			LOG_INFO("Index buffer destroyed");
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

		#pragma region "ImGui BeginFrame"

		if (m_Data->imguiInitialized) {
			ImGui_ImplVulkan_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
		}

#pragma endregion

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

	bool Renderer::CreateImGuiDescriptorPool()
	{
		VkDescriptorPoolSize poolSizes[] = {
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 100 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100 }
		};

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.maxSets = 100;
		poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
		poolInfo.pPoolSizes = poolSizes;

		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());

		if (vkCreateDescriptorPool(vulkanDevice->GetDevice(), &poolInfo, nullptr,
			&m_Data->imguiDescriptorPool) != VK_SUCCESS) {
			LOG_ERROR("Failed to create ImGui descriptor pool");
			return false;
		}

		return true;
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

	void Renderer::EndSingleTimeCommands(VkCommandBuffer commandBuffer)
	{
		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());

		vkEndCommandBuffer(commandBuffer);

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		vkQueueSubmit(vulkanDevice->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(vulkanDevice->GetGraphicsQueue());

		vkFreeCommandBuffers(vulkanDevice->GetDevice(),
			m_Data->commandPool->GetPool(), 1, &commandBuffer);
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

	IPipelineManager* Renderer::GetPipelineManager() const
	{
		return m_Data->pipelineAdapter.get();
	}

	void Renderer::FinalizeFrame()
	{
		if (!m_Data->initialized)
			return;

		// Add Renderer's own debug UI if needed
		if (m_Data->imguiInitialized) {
			// Optional: Add renderer-specific debug window
			ImGui::Begin("Renderer Debug");
			ImGui::Text("Performance");
			ImGui::Separator();
			ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
			ImGui::Text("Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);

			ImGui::Spacing();
			ImGui::Text("Cube Controls");
			ImGui::Separator();
			ImGui::SliderFloat("Rotation Speed", &m_Data->rotationSpeed, 0.0f, 0.1f);

			if (ImGui::Button("Reset Rotation")) {
				m_Data->rotationSpeed = 0.01f;
			}

			ImGui::Spacing();
			ImGui::Checkbox("Show ImGui Demo", &m_Data->showImGuiDemo);

			if (ImGui::Button("Toggle Pipeline (P)")) {
				TogglePipeline();
			}

			ImGui::End();

			// Show demo window if requested
			if (m_Data->showImGuiDemo) {
				ImGui::ShowDemoWindow(&m_Data->showImGuiDemo);
			}

			// NOW render ImGui after all UI has been added
			ImGui::Render();
		}

		// Record command buffer with all the draw commands
		VkCommandBuffer commandBuffer = m_Data->commandBuffers[m_Data->currentFrame];
		vkResetCommandBuffer(commandBuffer, 0);
		RecordCommandBuffer(commandBuffer, m_Data->currentImageIndex);

	}

	void Renderer::TogglePipeline()
	{
		if (!m_Data->initialized || !m_Data->pipelineAdapter)
		{
			LOG_WARN("Cannot toggle pipeline - renderer not initialized");
			return;
		}

		// WAIT FOR DEVICE TO BE IDLE BEFORE RE-RECORDING
		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());
		vkDeviceWaitIdle(vulkanDevice->GetDevice());

		// Toggle between Triangle and Mesh pipelines
		if (m_Data->currentPipeline == PipelineType::Triangle)
		{
			// Check if mesh pipeline exists
			if (m_Data->pipelineAdapter->GetPipeline(PipelineType::Mesh) != VK_NULL_HANDLE)
			{
				m_Data->currentPipeline = PipelineType::Mesh;
				LOG_INFO("Switched to Mesh pipeline (with push constants)");
			}
			else
			{
				LOG_WARN("Mesh pipeline not available");
			}
		}
		else
		{
			m_Data->currentPipeline = PipelineType::Triangle;
			LOG_INFO("Switched to Triangle pipeline (simple)");
		}

		// Re-record command buffers with new pipeline
		PreRecordAllCommandBuffers();
	}

	void Renderer::ReloadShaders()
	{
		if (!m_Data->initialized || !m_Data->pipelineAdapter)
		{
			LOG_WARN("Cannot reload shaders - renderer not initialized");
			return;
		}

		LOG_INFO("Reloading all shaders...");

		// Wait for device to be idle before reloading
		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());
		vkDeviceWaitIdle(vulkanDevice->GetDevice());

		// Reload all pipelines
		if (m_Data->pipelineAdapter->ReloadAllPipelines())
		{
			LOG_INFO("Shaders reloaded successfully");

			// Re-record command buffers with new pipelines
			PreRecordAllCommandBuffers();
		}
		else
		{
			LOG_ERROR("Failed to reload shaders");
		}
	}
}