//------------------------------------------------------------------------------
// RenderPassManager.cpp
//
// Implementation of render pass and framebuffer management
//------------------------------------------------------------------------------

#include "Engine/Renderer/Components/RenderPassManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanSwapchain.hpp"
#include "Engine/Core/Logger/Logger.hpp"

namespace Nightbloom
{
	bool RenderPassManager::Initialize(VkDevice device, VulkanSwapchain* swapchain)
	{
		// Create the main render pass
		if (!CreateMainRenderPass(device, swapchain->GetImageFormat(), false))
		{
			LOG_ERROR("Failed to create main render pass");
			return false;
		}

		// Create framebuffers for each swapchain image
		if (!CreateFramebuffers(device, swapchain))
		{
			LOG_ERROR("Failed to create framebuffers");
			Cleanup(device);
			return false;
		}

		LOG_INFO("Render pass manager initialized with {} framebuffers", m_Framebuffers.size());
		return true;
	}

	void RenderPassManager::Cleanup(VkDevice device)
	{
		// Destroy framebuffers
		DestroyFramebuffers(device);

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

		// Subpass description
		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;

		// TODO: Add depth attachment support when needed
		if (hasDepth)
		{
			LOG_WARN("Depth attachment not yet implemented");
		}

		// Subpass dependencies for layout transitions
		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		// Create render pass
		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;

		if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_MainRenderPass) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create render pass");
			return false;
		}

		LOG_INFO("Main render pass created");
		return true;
	}

	bool RenderPassManager::CreateFramebuffers(VkDevice device, VulkanSwapchain* swapchain)
	{
		const auto& imageViews = swapchain->GetImageViews();
		VkExtent2D extent = swapchain->GetExtent();

		m_Framebuffers.resize(imageViews.size());

		for (size_t i = 0; i < imageViews.size(); i++)
		{
			VkImageView attachments[] = { imageViews[i] };

			VkFramebufferCreateInfo framebufferInfo{};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = m_MainRenderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachments;
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

		LOG_INFO("Created {} framebuffers", m_Framebuffers.size());
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
}