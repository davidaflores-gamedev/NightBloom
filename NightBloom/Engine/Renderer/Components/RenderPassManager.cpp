//------------------------------------------------------------------------------
// RenderPassManager.cpp
//
// Implementation of render pass and framebuffer management
//------------------------------------------------------------------------------

#include "Engine/Renderer/Components/RenderPassManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanMemoryManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanSwapchain.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <array>

namespace Nightbloom
{
	bool RenderPassManager::Initialize(VkDevice device, VulkanSwapchain* swapchain, VulkanMemoryManager* memoryManager,
		VkSampleCountFlagBits sampleCount)
	{

		m_MemoryManager = memoryManager;
		m_HasDepth = true;
		m_SampleCount = sampleCount;
		m_SceneColorFormat = swapchain->GetImageFormat();

		// Create depth resources first (needed for render pass creation to know format)
		if (m_HasDepth)
		{
			if (!CreateDepthResources(device, swapchain->GetExtent()))
			{
				LOG_ERROR("Failed to create depth resources");
				return false;
			}
		}

		if (!CreateSceneColorResources(device, m_SceneColorFormat, swapchain->GetExtent()))
		{
			LOG_ERROR("Failed to create scene color resources");
			DestroyDepthResources(device);
			return false;
		}

		// Create the scene render pass (renders into the offscreen scene-color texture)
		if (!CreateSceneRenderPass(device, m_SceneColorFormat, m_HasDepth))
		{
			LOG_ERROR("Failed to create scene render pass");
			DestroySceneColorResources(device);
			DestroyDepthResources(device);
			return false;
		}

		if (!CreateSceneFramebuffer(device, swapchain->GetExtent()))
		{
			LOG_ERROR("Failed to create scene framebuffer");
			Cleanup(device);
			return false;
		}

		// Create the post-process render pass (samples scene-color, writes the swapchain image)
		if (!CreatePostProcessRenderPass(device, swapchain->GetImageFormat()))
		{
			LOG_ERROR("Failed to create post-process render pass");
			Cleanup(device);
			return false;
		}

		if (!CreatePostProcessFramebuffers(device, swapchain))
		{
			LOG_ERROR("Failed to create post-process framebuffers");
			Cleanup(device);
			return false;
		}

		// Reflection target (mirror-camera scene render, sampled by the water
		// surface). Single-sample, sized to the swapchain for now.
		if (!CreateReflectionRenderPass(device, m_SceneColorFormat))
		{
			LOG_ERROR("Failed to create reflection render pass");
			Cleanup(device);
			return false;
		}

		if (!CreateReflectionResources(device, m_SceneColorFormat, swapchain->GetExtent()))
		{
			LOG_ERROR("Failed to create reflection resources");
			Cleanup(device);
			return false;
		}

		if (!CreateReflectionFramebuffer(device, swapchain->GetExtent()))
		{
			LOG_ERROR("Failed to create reflection framebuffer");
			Cleanup(device);
			return false;
		}

		LOG_INFO("Render pass manager initialized ({} post-process framebuffers, depth: {})",
			m_PostProcessFramebuffers.size(), m_HasDepth);
		return true;
	}

	void RenderPassManager::Cleanup(VkDevice device)
	{
		DestroyReflectionFramebuffer(device);
		DestroyReflectionResources(device);

		if (m_ReflectionRenderPass != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(device, m_ReflectionRenderPass, nullptr);
			m_ReflectionRenderPass = VK_NULL_HANDLE;
		}

		DestroyPostProcessFramebuffers(device);

		if (m_PostProcessRenderPass != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(device, m_PostProcessRenderPass, nullptr);
			m_PostProcessRenderPass = VK_NULL_HANDLE;
		}

		DestroySceneFramebuffer(device);
		DestroySceneColorResources(device);
		DestroyDepthResources(device);

		if (m_SceneRenderPass != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(device, m_SceneRenderPass, nullptr);
			m_SceneRenderPass = VK_NULL_HANDLE;
		}

		LOG_INFO("Render pass manager cleaned up");
	}

	bool RenderPassManager::RecreateFramebuffers(VkDevice device, VulkanSwapchain* swapchain)
	{
		LOG_INFO("Recreating framebuffers for swapchain resize");

		DestroyPostProcessFramebuffers(device);
		DestroySceneFramebuffer(device);

		if (m_HasDepth)
		{
			DestroyDepthResources(device);
			if (!CreateDepthResources(device, swapchain->GetExtent()))
			{
				LOG_ERROR("Failed to recreate depth resources");
				return false;
			}
		}

		DestroySceneColorResources(device);
		if (!CreateSceneColorResources(device, m_SceneColorFormat, swapchain->GetExtent()))
		{
			LOG_ERROR("Failed to recreate scene color resources");
			return false;
		}

		if (!CreateSceneFramebuffer(device, swapchain->GetExtent()))
		{
			LOG_ERROR("Failed to recreate scene framebuffer");
			return false;
		}

		if (!CreatePostProcessFramebuffers(device, swapchain))
		{
			LOG_ERROR("Failed to recreate post-process framebuffers");
			return false;
		}

		// Reflection target is sized to the swapchain — recreate it too.
		DestroyReflectionFramebuffer(device);
		DestroyReflectionResources(device);
		if (!CreateReflectionResources(device, m_SceneColorFormat, swapchain->GetExtent()))
		{
			LOG_ERROR("Failed to recreate reflection resources");
			return false;
		}
		if (!CreateReflectionFramebuffer(device, swapchain->GetExtent()))
		{
			LOG_ERROR("Failed to recreate reflection framebuffer");
			return false;
		}

		return true;
	}

	bool RenderPassManager::CreateSceneRenderPass(VkDevice device, VkFormat colorFormat, bool hasDepth)
	{
		const bool msaa = (m_SampleCount != VK_SAMPLE_COUNT_1_BIT);

		// Attachment layout:
		//   no MSAA  : [0]=color(sampled), [1]=depth
		//   with MSAA: [0]=MS color, [1]=MS depth, [2]=resolve color(sampled)
		// The sampled target (what the post-process pass reads) is the color
		// attachment without MSAA, or the resolve attachment with it — in both
		// cases it carries SHADER_READ_ONLY final layout + STORE.

		// Color attachment (multisampled when MSAA — it's resolved, not sampled,
		// so it stores DONT_CARE; without MSAA it IS the sampled target).
		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = colorFormat;
		colorAttachment.samples = m_SampleCount;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = msaa
			? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
			: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkAttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		std::vector<VkAttachmentDescription> attachments = { colorAttachment };

		// Depth attachment (optional) — index 1, multisampled to match color.
		VkAttachmentDescription depthAttachment{};
		VkAttachmentReference depthAttachmentRef{};

		if (hasDepth)
		{
			depthAttachment.format = VK_FORMAT_D32_SFLOAT;
			depthAttachment.samples = m_SampleCount;
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

		// Resolve attachment (only with MSAA) — single-sample, this is the
		// sampled target the post-process pass reads.
		VkAttachmentReference resolveAttachmentRef{};
		if (msaa)
		{
			VkAttachmentDescription resolveAttachment{};
			resolveAttachment.format = colorFormat;
			resolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			resolveAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			resolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			resolveAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			resolveAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			resolveAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			resolveAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			resolveAttachmentRef.attachment = static_cast<uint32_t>(attachments.size());
			resolveAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			attachments.push_back(resolveAttachment);
		}

		// Subpass description
		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;
		subpass.pResolveAttachments = msaa ? &resolveAttachmentRef : nullptr;
		subpass.pDepthStencilAttachment = hasDepth ? &depthAttachmentRef : nullptr;

		// Subpass dependency for entering this pass (layout transitions in)
		VkSubpassDependency dependencyIn{};
		dependencyIn.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencyIn.dstSubpass = 0;
		dependencyIn.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencyIn.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencyIn.srcAccessMask = 0;
		dependencyIn.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		if (hasDepth)
		{
			dependencyIn.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			dependencyIn.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			dependencyIn.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		}

		// Subpass dependency for leaving this pass — the post-process pass
		// samples the color attachment as a fragment-shader input in a
		// *separate* render pass instance, so nothing else inserts this
		// barrier automatically; without it the FXAA pass could read before
		// this pass's writes are visible.
		VkSubpassDependency dependencyOut{};
		dependencyOut.srcSubpass = 0;
		dependencyOut.dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencyOut.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencyOut.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencyOut.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencyOut.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		std::array<VkSubpassDependency, 2> dependencies = { dependencyIn, dependencyOut };

		// Create render pass
		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_SceneRenderPass) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create scene render pass");
			return false;
		}

		m_HasDepth = hasDepth;
		LOG_INFO("Scene render pass created (depth: {})", hasDepth);
		return true;
	}

	bool RenderPassManager::CreateSceneColorResources(VkDevice device, VkFormat colorFormat, VkExtent2D extent)
	{
		if (!m_MemoryManager)
		{
			LOG_ERROR("Memory manager not set - cannot create scene color resources");
			return false;
		}

		VulkanMemoryManager::ImageCreateInfo imageInfo{};
		imageInfo.width = extent.width;
		imageInfo.height = extent.height;
		imageInfo.format = colorFormat;
		imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

		auto* allocation = m_MemoryManager->CreateImage(imageInfo);
		if (!allocation)
		{
			LOG_ERROR("Failed to create scene color image");
			return false;
		}

		m_SceneColorAllocation = allocation;
		m_SceneColorImage = allocation->image;

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_SceneColorImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = colorFormat;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		if (vkCreateImageView(device, &viewInfo, nullptr, &m_SceneColorImageView) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create scene color image view");
			m_MemoryManager->DestroyImage(static_cast<VulkanMemoryManager::ImageAllocation*>(m_SceneColorAllocation));
			m_SceneColorAllocation = nullptr;
			m_SceneColorImage = VK_NULL_HANDLE;
			return false;
		}

		// Linear + clamp-to-edge: this is a 1:1 full-screen post-process
		// source, not a tiled asset texture (no repeat, no mipmaps needed).
		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;

		if (vkCreateSampler(device, &samplerInfo, nullptr, &m_SceneColorSampler) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create scene color sampler");
			vkDestroyImageView(device, m_SceneColorImageView, nullptr);
			m_SceneColorImageView = VK_NULL_HANDLE;
			m_MemoryManager->DestroyImage(static_cast<VulkanMemoryManager::ImageAllocation*>(m_SceneColorAllocation));
			m_SceneColorAllocation = nullptr;
			m_SceneColorImage = VK_NULL_HANDLE;
			return false;
		}

		// Multisampled color attachment (rendered into, then resolved into the
		// single-sample m_SceneColorImage above). Only needed with MSAA.
		if (m_SampleCount != VK_SAMPLE_COUNT_1_BIT)
		{
			VulkanMemoryManager::ImageCreateInfo msInfo{};
			msInfo.width = extent.width;
			msInfo.height = extent.height;
			msInfo.format = colorFormat;
			msInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
			msInfo.memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
			msInfo.samples = m_SampleCount;

			auto* msAlloc = m_MemoryManager->CreateImage(msInfo);
			if (!msAlloc)
			{
				LOG_ERROR("Failed to create multisampled scene color image");
				DestroySceneColorResources(device);
				return false;
			}
			m_SceneColorMSAllocation = msAlloc;
			m_SceneColorMSImage = msAlloc->image;

			VkImageViewCreateInfo msView{};
			msView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			msView.image = m_SceneColorMSImage;
			msView.viewType = VK_IMAGE_VIEW_TYPE_2D;
			msView.format = colorFormat;
			msView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			msView.subresourceRange.baseMipLevel = 0;
			msView.subresourceRange.levelCount = 1;
			msView.subresourceRange.baseArrayLayer = 0;
			msView.subresourceRange.layerCount = 1;

			if (vkCreateImageView(device, &msView, nullptr, &m_SceneColorMSImageView) != VK_SUCCESS)
			{
				LOG_ERROR("Failed to create multisampled scene color image view");
				DestroySceneColorResources(device);
				return false;
			}
		}

		LOG_INFO("Scene color target created: {}x{}, format={}, samples={}",
			extent.width, extent.height, static_cast<int>(colorFormat), static_cast<int>(m_SampleCount));
		return true;
	}

	void RenderPassManager::DestroySceneColorResources(VkDevice device)
	{
		if (m_SceneColorMSImageView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, m_SceneColorMSImageView, nullptr);
			m_SceneColorMSImageView = VK_NULL_HANDLE;
		}
		if (m_SceneColorMSAllocation && m_MemoryManager)
		{
			m_MemoryManager->DestroyImage(
				static_cast<VulkanMemoryManager::ImageAllocation*>(m_SceneColorMSAllocation));
			m_SceneColorMSAllocation = nullptr;
			m_SceneColorMSImage = VK_NULL_HANDLE;
		}

		if (m_SceneColorSampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_SceneColorSampler, nullptr);
			m_SceneColorSampler = VK_NULL_HANDLE;
		}

		if (m_SceneColorImageView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, m_SceneColorImageView, nullptr);
			m_SceneColorImageView = VK_NULL_HANDLE;
		}

		if (m_SceneColorAllocation && m_MemoryManager)
		{
			m_MemoryManager->DestroyImage(
				static_cast<VulkanMemoryManager::ImageAllocation*>(m_SceneColorAllocation));
			m_SceneColorAllocation = nullptr;
			m_SceneColorImage = VK_NULL_HANDLE;
		}
	}

	bool RenderPassManager::CreateSceneFramebuffer(VkDevice device, VkExtent2D extent)
	{
		// Attachment order must match CreateSceneRenderPass:
		//   no MSAA  : [0]=color(sampled), [1]=depth
		//   with MSAA: [0]=MS color, [1]=MS depth, [2]=resolve color(sampled)
		const bool msaa = (m_SampleCount != VK_SAMPLE_COUNT_1_BIT);

		std::vector<VkImageView> attachments;
		attachments.push_back(msaa ? m_SceneColorMSImageView : m_SceneColorImageView);

		if (m_HasDepth)
		{
			attachments.push_back(m_DepthImageView);
		}

		if (msaa)
		{
			attachments.push_back(m_SceneColorImageView); // resolve target
		}

		VkFramebufferCreateInfo framebufferInfo{};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = m_SceneRenderPass;
		framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		framebufferInfo.pAttachments = attachments.data();
		framebufferInfo.width = extent.width;
		framebufferInfo.height = extent.height;
		framebufferInfo.layers = 1;

		if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_SceneFramebuffer) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create scene framebuffer");
			return false;
		}

		LOG_INFO("Created scene framebuffer (attachments: {})", attachments.size());
		return true;
	}

	void RenderPassManager::DestroySceneFramebuffer(VkDevice device)
	{
		if (m_SceneFramebuffer != VK_NULL_HANDLE)
		{
			vkDestroyFramebuffer(device, m_SceneFramebuffer, nullptr);
			m_SceneFramebuffer = VK_NULL_HANDLE;
		}
	}

	bool RenderPassManager::CreatePostProcessRenderPass(VkDevice device, VkFormat colorFormat)
	{
		// Color only — the post-process composite/AA pass doesn't depth-test,
		// it overwrites every pixel with a full-screen triangle.
		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = colorFormat;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;

		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;

		if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_PostProcessRenderPass) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create post-process render pass");
			return false;
		}

		LOG_INFO("Post-process render pass created");
		return true;
	}

	bool RenderPassManager::CreatePostProcessFramebuffers(VkDevice device, VulkanSwapchain* swapchain)
	{
		const auto& imageViews = swapchain->GetImageViews();
		VkExtent2D extent = swapchain->GetExtent();

		m_PostProcessFramebuffers.resize(imageViews.size());

		for (size_t i = 0; i < imageViews.size(); i++)
		{
			VkImageView attachment = imageViews[i];

			VkFramebufferCreateInfo framebufferInfo{};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = m_PostProcessRenderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = &attachment;
			framebufferInfo.width = extent.width;
			framebufferInfo.height = extent.height;
			framebufferInfo.layers = 1;

			if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_PostProcessFramebuffers[i]) != VK_SUCCESS)
			{
				LOG_ERROR("Failed to create post-process framebuffer {}", i);

				for (size_t j = 0; j < i; j++)
				{
					vkDestroyFramebuffer(device, m_PostProcessFramebuffers[j], nullptr);
				}
				m_PostProcessFramebuffers.clear();

				return false;
			}
		}

		LOG_INFO("Created {} post-process framebuffers", m_PostProcessFramebuffers.size());
		return true;
	}

	void RenderPassManager::DestroyPostProcessFramebuffers(VkDevice device)
	{
		for (auto framebuffer : m_PostProcessFramebuffers)
		{
			if (framebuffer != VK_NULL_HANDLE)
			{
				vkDestroyFramebuffer(device, framebuffer, nullptr);
			}
		}
		m_PostProcessFramebuffers.clear();
	}

	bool RenderPassManager::CreateReflectionRenderPass(VkDevice device, VkFormat colorFormat)
	{
		// MUST match the scene render pass's attachment structure (formats +
		// sample count) so the scene's Mesh/Terrain/Foliage pipelines are
		// render-pass-compatible when reused here. Mirrors CreateSceneRenderPass:
		//   no MSAA  : [0]=color(sampled), [1]=depth
		//   with MSAA: [0]=MS color, [1]=MS depth, [2]=resolve color(sampled)
		const bool msaa = (m_SampleCount != VK_SAMPLE_COUNT_1_BIT);

		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = colorFormat;
		colorAttachment.samples = m_SampleCount;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = msaa
			? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
			: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkAttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		std::vector<VkAttachmentDescription> attachments = { colorAttachment };

		VkAttachmentDescription depthAttachment{};
		depthAttachment.format = m_DepthFormat;
		depthAttachment.samples = m_SampleCount;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthAttachmentRef{};
		depthAttachmentRef.attachment = 1;
		depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attachments.push_back(depthAttachment);

		VkAttachmentReference resolveAttachmentRef{};
		if (msaa)
		{
			VkAttachmentDescription resolveAttachment{};
			resolveAttachment.format = colorFormat;
			resolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			resolveAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			resolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			resolveAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			resolveAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			resolveAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			resolveAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			resolveAttachmentRef.attachment = static_cast<uint32_t>(attachments.size());
			resolveAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachments.push_back(resolveAttachment);
		}

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;
		subpass.pResolveAttachments = msaa ? &resolveAttachmentRef : nullptr;
		subpass.pDepthStencilAttachment = &depthAttachmentRef;

		VkSubpassDependency dependencyIn{};
		dependencyIn.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencyIn.dstSubpass = 0;
		dependencyIn.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependencyIn.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependencyIn.srcAccessMask = 0;
		dependencyIn.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		// The water surface (in the later scene pass) samples this color as a
		// fragment-shader input, so insert the same out-barrier the scene target uses.
		VkSubpassDependency dependencyOut{};
		dependencyOut.srcSubpass = 0;
		dependencyOut.dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencyOut.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencyOut.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencyOut.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencyOut.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		std::array<VkSubpassDependency, 2> dependencies = { dependencyIn, dependencyOut };

		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_ReflectionRenderPass) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create reflection render pass");
			return false;
		}

		LOG_INFO("Reflection render pass created (samples={})", static_cast<int>(m_SampleCount));
		return true;
	}

	bool RenderPassManager::CreateReflectionResources(VkDevice device, VkFormat colorFormat, VkExtent2D extent)
	{
		if (!m_MemoryManager)
		{
			LOG_ERROR("Memory manager not set - cannot create reflection resources");
			return false;
		}

		m_ReflectionExtent = extent;

		// Single-sample color target — the SAMPLED resolve target under MSAA,
		// or the color attachment itself without MSAA.
		VulkanMemoryManager::ImageCreateInfo colorInfo{};
		colorInfo.width = extent.width;
		colorInfo.height = extent.height;
		colorInfo.format = colorFormat;
		colorInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		colorInfo.memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		colorInfo.samples = VK_SAMPLE_COUNT_1_BIT;

		auto* colorAlloc = m_MemoryManager->CreateImage(colorInfo);
		if (!colorAlloc)
		{
			LOG_ERROR("Failed to create reflection color image");
			return false;
		}
		m_ReflectionColorAllocation = colorAlloc;
		m_ReflectionColorImage = colorAlloc->image;

		VkImageViewCreateInfo colorView{};
		colorView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		colorView.image = m_ReflectionColorImage;
		colorView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		colorView.format = colorFormat;
		colorView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorView.subresourceRange.baseMipLevel = 0;
		colorView.subresourceRange.levelCount = 1;
		colorView.subresourceRange.baseArrayLayer = 0;
		colorView.subresourceRange.layerCount = 1;

		if (vkCreateImageView(device, &colorView, nullptr, &m_ReflectionColorImageView) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create reflection color image view");
			DestroyReflectionResources(device);
			return false;
		}

		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;

		if (vkCreateSampler(device, &samplerInfo, nullptr, &m_ReflectionColorSampler) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create reflection color sampler");
			DestroyReflectionResources(device);
			return false;
		}

		// Depth target (own depth, multisampled to match the color sample count)
		VulkanMemoryManager::ImageCreateInfo depthInfo{};
		depthInfo.width = extent.width;
		depthInfo.height = extent.height;
		depthInfo.depth = 1;
		depthInfo.mipLevels = 1;
		depthInfo.arrayLayers = 1;
		depthInfo.format = m_DepthFormat;
		depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		depthInfo.memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		depthInfo.samples = m_SampleCount;

		auto* depthAlloc = m_MemoryManager->CreateImage(depthInfo);
		if (!depthAlloc)
		{
			LOG_ERROR("Failed to create reflection depth image");
			DestroyReflectionResources(device);
			return false;
		}
		m_ReflectionDepthAllocation = depthAlloc;
		m_ReflectionDepthImage = depthAlloc->image;

		VkImageViewCreateInfo depthView{};
		depthView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		depthView.image = m_ReflectionDepthImage;
		depthView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		depthView.format = m_DepthFormat;
		depthView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		depthView.subresourceRange.baseMipLevel = 0;
		depthView.subresourceRange.levelCount = 1;
		depthView.subresourceRange.baseArrayLayer = 0;
		depthView.subresourceRange.layerCount = 1;

		if (vkCreateImageView(device, &depthView, nullptr, &m_ReflectionDepthImageView) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create reflection depth image view");
			DestroyReflectionResources(device);
			return false;
		}

		// Multisampled color attachment (rendered into, resolved into the
		// single-sample color target above). Only needed with MSAA.
		if (m_SampleCount != VK_SAMPLE_COUNT_1_BIT)
		{
			VulkanMemoryManager::ImageCreateInfo msInfo{};
			msInfo.width = extent.width;
			msInfo.height = extent.height;
			msInfo.format = colorFormat;
			msInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
			msInfo.memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
			msInfo.samples = m_SampleCount;

			auto* msAlloc = m_MemoryManager->CreateImage(msInfo);
			if (!msAlloc)
			{
				LOG_ERROR("Failed to create multisampled reflection color image");
				DestroyReflectionResources(device);
				return false;
			}
			m_ReflectionColorMSAllocation = msAlloc;
			m_ReflectionColorMSImage = msAlloc->image;

			VkImageViewCreateInfo msView{};
			msView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			msView.image = m_ReflectionColorMSImage;
			msView.viewType = VK_IMAGE_VIEW_TYPE_2D;
			msView.format = colorFormat;
			msView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			msView.subresourceRange.baseMipLevel = 0;
			msView.subresourceRange.levelCount = 1;
			msView.subresourceRange.baseArrayLayer = 0;
			msView.subresourceRange.layerCount = 1;

			if (vkCreateImageView(device, &msView, nullptr, &m_ReflectionColorMSImageView) != VK_SUCCESS)
			{
				LOG_ERROR("Failed to create multisampled reflection color image view");
				DestroyReflectionResources(device);
				return false;
			}
		}

		LOG_INFO("Reflection target created: {}x{}, samples={}",
			extent.width, extent.height, static_cast<int>(m_SampleCount));
		return true;
	}

	void RenderPassManager::DestroyReflectionResources(VkDevice device)
	{
		if (m_ReflectionColorMSImageView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, m_ReflectionColorMSImageView, nullptr);
			m_ReflectionColorMSImageView = VK_NULL_HANDLE;
		}
		if (m_ReflectionColorMSAllocation && m_MemoryManager)
		{
			m_MemoryManager->DestroyImage(
				static_cast<VulkanMemoryManager::ImageAllocation*>(m_ReflectionColorMSAllocation));
			m_ReflectionColorMSAllocation = nullptr;
			m_ReflectionColorMSImage = VK_NULL_HANDLE;
		}

		if (m_ReflectionDepthImageView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, m_ReflectionDepthImageView, nullptr);
			m_ReflectionDepthImageView = VK_NULL_HANDLE;
		}
		if (m_ReflectionDepthAllocation && m_MemoryManager)
		{
			m_MemoryManager->DestroyImage(
				static_cast<VulkanMemoryManager::ImageAllocation*>(m_ReflectionDepthAllocation));
			m_ReflectionDepthAllocation = nullptr;
			m_ReflectionDepthImage = VK_NULL_HANDLE;
		}

		if (m_ReflectionColorSampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_ReflectionColorSampler, nullptr);
			m_ReflectionColorSampler = VK_NULL_HANDLE;
		}
		if (m_ReflectionColorImageView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, m_ReflectionColorImageView, nullptr);
			m_ReflectionColorImageView = VK_NULL_HANDLE;
		}
		if (m_ReflectionColorAllocation && m_MemoryManager)
		{
			m_MemoryManager->DestroyImage(
				static_cast<VulkanMemoryManager::ImageAllocation*>(m_ReflectionColorAllocation));
			m_ReflectionColorAllocation = nullptr;
			m_ReflectionColorImage = VK_NULL_HANDLE;
		}
	}

	bool RenderPassManager::CreateReflectionFramebuffer(VkDevice device, VkExtent2D extent)
	{
		// Attachment order must match CreateReflectionRenderPass:
		//   no MSAA  : [0]=color(sampled), [1]=depth
		//   with MSAA: [0]=MS color, [1]=MS depth, [2]=resolve color(sampled)
		const bool msaa = (m_SampleCount != VK_SAMPLE_COUNT_1_BIT);

		std::vector<VkImageView> attachments;
		attachments.push_back(msaa ? m_ReflectionColorMSImageView : m_ReflectionColorImageView);
		attachments.push_back(m_ReflectionDepthImageView);
		if (msaa)
		{
			attachments.push_back(m_ReflectionColorImageView); // resolve target
		}

		VkFramebufferCreateInfo framebufferInfo{};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = m_ReflectionRenderPass;
		framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		framebufferInfo.pAttachments = attachments.data();
		framebufferInfo.width = extent.width;
		framebufferInfo.height = extent.height;
		framebufferInfo.layers = 1;

		if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_ReflectionFramebuffer) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create reflection framebuffer");
			return false;
		}

		LOG_INFO("Created reflection framebuffer");
		return true;
	}

	void RenderPassManager::DestroyReflectionFramebuffer(VkDevice device)
	{
		if (m_ReflectionFramebuffer != VK_NULL_HANDLE)
		{
			vkDestroyFramebuffer(device, m_ReflectionFramebuffer, nullptr);
			m_ReflectionFramebuffer = VK_NULL_HANDLE;
		}
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
		imageInfo.samples = m_SampleCount; // must match the scene color sample count

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
