//------------------------------------------------------------------------------
// RenderPassManager.cpp
//
// Implementation of render pass and framebuffer management
//------------------------------------------------------------------------------

#include "Engine/Renderer/Components/RenderPassManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanMemoryManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanSwapchain.hpp"
#include "Engine/Core/Logger/Logger.hpp"

namespace Nightbloom
{
	bool RenderPassManager::Initialize(VkDevice device, VulkanSwapchain* swapchain, VulkanMemoryManager* memoryManager)
	{

		m_MemoryManager = memoryManager;
		m_HasDepth = true;

		// Create depth resources first (needed for render pass creation to know format)
		if (m_HasDepth)
		{
			if (!CreateDepthResources(device, swapchain->GetExtent()))
			{
				LOG_ERROR("Failed to create depth resources");
				return false;
			}
		}

		// Create the main render pass
		if (!CreateMainRenderPass(device, swapchain->GetImageFormat(), m_HasDepth))
		{
			LOG_ERROR("Failed to create main render pass");
			DestroyDepthResources(device);
			return false;
		}

		// Create framebuffers for each swapchain image
		if (!CreateFramebuffers(device, swapchain))
		{
			LOG_ERROR("Failed to create framebuffers");
			Cleanup(device);
			return false;
		}

		LOG_INFO("Render pass manager initialized with {} framebuffers (depth: {})", m_Framebuffers.size(), m_HasDepth);
		return true;
	}

	void RenderPassManager::Cleanup(VkDevice device)
	{
		// Destroy framebuffers
		DestroyFramebuffers(device);

		DestroyDepthResources(device);

		// Destroy render passes
		if (m_MainRenderPass != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(device, m_MainRenderPass, nullptr);
			m_MainRenderPass = VK_NULL_HANDLE;
		}

		LOG_INFO("Render pass manager cleaned up");
	}

	bool RenderPassManager::RecreateFramebuffers(VkDevice device, VulkanSwapchain* swapchain)
	{
		LOG_INFO("Recreating framebuffers for swapchain resize");

		// Destroy old framebuffers
		DestroyFramebuffers(device);

		if (m_HasDepth)
		{
			DestroyDepthResources(device);
			if (!CreateDepthResources(device, swapchain->GetExtent()))
			{
				LOG_ERROR("Failed to recreate depth resources");
				return false;
			}
		}

		// Create new framebuffers
		if (!CreateFramebuffers(device, swapchain))
		{
			LOG_ERROR("Failed to recreate framebuffers");
			return false;
		}

		return true;
	}

	bool RenderPassManager::CreateMainRenderPass(VkDevice device, VkFormat colorFormat, bool hasDepth)
	{
		// Color attachment description
		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = colorFormat;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		// Build attachment arrays - we'll add depth if needed
		std::vector<VkAttachmentDescription> attachments = { colorAttachment };

		// Depth attachment (optional)
		VkAttachmentDescription depthAttachment{};
		VkAttachmentReference depthAttachmentRef{};

		if (hasDepth)
		{
			depthAttachment.format = VK_FORMAT_D32_SFLOAT;
			depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // We don't need depth after rendering
			depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			depthAttachmentRef.attachment = 1; // Second attachment
			depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			attachments.push_back(depthAttachment);
		}

		// Subpass description
		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;
		subpass.pDepthStencilAttachment = hasDepth ? &depthAttachmentRef : nullptr;

		// Subpass dependencies for layout transitions
		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		// Add depth stages if we have a depth buffer
		if (hasDepth)
		{
			dependency.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			dependency.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		}

		// Create render pass
		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;

		if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_MainRenderPass) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create render pass");
			return false;
		}

		m_HasDepth = hasDepth;
		LOG_INFO("Main render pass created (depth: {})", hasDepth);
		return true;
	}

	bool RenderPassManager::CreateFramebuffers(VkDevice device, VulkanSwapchain* swapchain)
	{
		const auto& imageViews = swapchain->GetImageViews();
		VkExtent2D extent = swapchain->GetExtent();

		m_Framebuffers.resize(imageViews.size());

		for (size_t i = 0; i < imageViews.size(); i++)
		{
			// Build attachments array - order must match render pass!
			// Index 0: color (unique per swapchain image)
			// Index 1: depth (shared across all framebuffers)

			std::vector<VkImageView> attachments;
			attachments.push_back(imageViews[i]);  // Color

			if (m_HasDepth)
			{
				attachments.push_back(m_DepthImageView);
			}

			VkFramebufferCreateInfo framebufferInfo{};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = m_MainRenderPass;
			framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			framebufferInfo.pAttachments = attachments.data();
			framebufferInfo.width = extent.width;
			framebufferInfo.height = extent.height;
			framebufferInfo.layers = 1;

			if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_Framebuffers[i]) != VK_SUCCESS)
			{
				LOG_ERROR("Failed to create framebuffer {}", i);

				// Clean up already created framebuffers
				for (size_t j = 0; j < i; j++)
				{
					vkDestroyFramebuffer(device, m_Framebuffers[j], nullptr);
				}
				m_Framebuffers.clear();

				return false;
			}
		}

		LOG_INFO("Created {} framebuffers (attachments per FrameBuffer: {})", 
			m_Framebuffers.size(), m_HasDepth ? 2 : 1);

		return true;
	}

	void RenderPassManager::DestroyFramebuffers(VkDevice device)
	{
		for (auto framebuffer : m_Framebuffers)
		{
			if (framebuffer != VK_NULL_HANDLE)
			{
				vkDestroyFramebuffer(device, framebuffer, nullptr);
			}
		}
		m_Framebuffers.clear();
		LOG_INFO("Destroyed framebuffers");
	}
	bool RenderPassManager::CreateDepthResources(VkDevice device, VkExtent2D extent)
	{
		if (!m_MemoryManager)
		{
			LOG_ERROR("Memory manager not set - cannot create depth resources");
			return false;
		}

		VulkanMemoryManager::ImageCreateInfo imageInfo{};
		imageInfo.width = extent.width;
		imageInfo.height = extent.height;
		imageInfo.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = m_DepthFormat;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		imageInfo.memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

		// Create the image through vma

		auto* allocation = m_MemoryManager->CreateImage(imageInfo);
		if (!allocation)
		{
			LOG_ERROR("Failed to create depth image");
			return false;
		}

		m_DepthAllocation = allocation;
		m_DepthImage = allocation->image;

		// Create image view for the depth image
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_DepthImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = m_DepthFormat;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		if (vkCreateImageView(device, &viewInfo, nullptr, &m_DepthImageView) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create depth image view");
			m_MemoryManager->DestroyImage(static_cast<VulkanMemoryManager::ImageAllocation*>(m_DepthAllocation));
			m_DepthAllocation = nullptr;
			m_DepthImage = VK_NULL_HANDLE;
			return false;
		}

		LOG_INFO("Depth buffer created: {}x{}, format={}",
			extent.width, extent.height, static_cast<int>(m_DepthFormat));
		return true;
	}

	void RenderPassManager::DestroyDepthResources(VkDevice device)
	{
		if (m_DepthImageView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, m_DepthImageView, nullptr);
			m_DepthImageView = VK_NULL_HANDLE;
		}

		if (m_DepthAllocation && m_MemoryManager)
		{
			m_MemoryManager->DestroyImage(
				static_cast<VulkanMemoryManager::ImageAllocation*>(m_DepthAllocation));
			m_DepthAllocation = nullptr;
			m_DepthImage = VK_NULL_HANDLE;
		}

		LOG_INFO("Depth resources destroyed");
	}
}