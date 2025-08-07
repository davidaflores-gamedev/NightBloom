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
//#include "Vulkan/VulkanCommandPool.hpp" 
#include "Core/Logger/Logger.hpp"
#include "Core/Assert.hpp"
#include "Core/FileUtils.hpp"
#include "Engine/Renderer/Vertex.hpp"

//// In Renderer.cpp or wherever you want to define it
//const std::vector<VertexPCU> triangleVertices = {
//	// Position          Color           UV
//	{{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},  // Bottom left - Red
//	{{ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},  // Bottom right - Green  
//	{{ 0.0f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}   // Top - Blue
//};
//
namespace Nightbloom
{
	struct Renderer::RendererData
	{
		std::unique_ptr<RenderDevice> device; // The rendering device (e.g., VulkanDevice)
		std::unique_ptr<VulkanSwapchain> swapchain; // Swapchain for presenting images
		std::unique_ptr<VulkanCommandPool> commandPool; // Command pool for allocating command buffers


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

		VkClearValue clearColor = { {{1.0f, 0.0f, 1.0f, 1.0f}} };  // Purple
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues = &clearColor;

		vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Future drawing commands would go here

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
		LOG_INFO("Initializing Renderer with window handle: {}, width: {}, height: {}", windowHandle, width, height);

		if (m_Data->initialized)
		{
			LOG_WARN("Renderer is already initialized");
			//not sure what to return here
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

//-------------------------------------------------------------//
		#pragma region "Renderer Initialization"
		try
		{
			LOG_INFO("Creating VulkanDevice...");
			m_Data->device = std::make_unique<VulkanDevice>();

			// Initialize the device
			if (!m_Data->device->Initialize(windowHandle, width, height))
			{
				LOG_ERROR("Failed to initialize VulkanDevice");
				return false;
			}

			LOG_INFO("VulkanDevice initialized successfully");

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

#pragma endregion

//-------------------------------------------------------------//
		#pragma region "Swapchain Initialization"

		LOG_INFO("Creating VulkanSwapchain... ");
		VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(m_Data->device.get());
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
		#pragma region "Command Pool/Buffers and sync objects Initialization"
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

		m_Data->initialized = true;
		LOG_INFO("Renderer initialized successfully");
		return true;
	}

	void Renderer::Shutdown()
	{
		if (!m_Data->initialized)
		{
			LOG_WARN("Renderer is not initialized, cannot shutdown");
			return;
		}

		LOG_INFO("Shutting down Renderer");

		if (m_Data->device)
		{
			// try this
			m_Data->device->WaitForIdle();
		}

		DestroyFramebuffers();

		if (m_Data->swapchain)
		{
			m_Data->swapchain->Shutdown();
			m_Data->swapchain.reset();
		}

		DestroyRenderPass();

		DestroySyncObjects();

		DestroyCommandBuffers();

		// Command buffers are automatically destroyed with the command pool
		m_Data->commandBuffers.clear();

		//Destroy command pool
		if (m_Data->commandPool)
		{
			m_Data->commandPool->Shutdown();
			m_Data->commandPool.reset();
			LOG_INFO("Command pool shutdown complete");
		}

		if (m_Data->device)
		{
			m_Data->device->Shutdown();
			m_Data->device.reset();
			LOG_INFO("RenderDevice shutdown complete");
		}

		m_Data->initialized = false;
		m_Data->windowHandle = nullptr;
		//m_Data->width = 0;
		//m_Data->height = 0;

		LOG_INFO("Renderer shutdown complete");
	}

	void Renderer::BeginFrame()
	{
		if (!m_Data->initialized)
		{
			LOG_ERROR("Renderer not initialized, cannot begin frame");
			return;
		}

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
			return;
		}

		// Reset fence only if we're submitting work
		vkResetFences(device, 1, &m_Data->inFlightFences[m_Data->currentFrame]);

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

		// Present 
		bool result = m_Data->swapchain->Present(
			m_Data->currentImageIndex,
			m_Data->renderFinishedSemaphores[m_Data->currentImageIndex]
		);

		if (!result || m_Data->swapchain->IsOutOfDate()) {
			// Need to recreate swapchain
			LOG_WARN("Swapchain out of date after present");
		}

		// advance to the next frame
		m_Data->currentFrame = (m_Data->currentFrame + 1) % RendererData::MAX_FRAMES_IN_FLIGHT;
	}

	void Renderer::Clear(float r, float g, float b, float a)
	{
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



