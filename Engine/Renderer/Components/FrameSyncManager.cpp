//------------------------------------------------------------------------------
// FrameSyncManager.cpp
//
// Implementation of frame synchronization management
//------------------------------------------------------------------------------

#include "Engine/Renderer/Components/FrameSyncManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanSwapchain.hpp"
#include "Engine/Core/Logger/Logger.hpp"

namespace Nightbloom
{
	bool FrameSyncManager::Initialize(VkDevice device, uint32_t swapchainImageCount)
	{
		// Resize vectors to match requirements
		m_ImageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
		m_RenderFinishedSemaphores.resize(swapchainImageCount);
		m_InFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
		m_ImagesInFlight.resize(swapchainImageCount, VK_NULL_HANDLE);

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Start in signaled state

		// Create synchronization objects for each frame in flight
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]) != VK_SUCCESS ||
				vkCreateFence(device, &fenceInfo, nullptr, &m_InFlightFences[i]) != VK_SUCCESS)
			{
				LOG_ERROR("Failed to create synchronization objects for frame {}", i);
				Cleanup(device);  // Clean up any already created objects
				return false;
			}
		}

		// Create render finished semaphores for each swapchain image
		for (size_t i = 0; i < swapchainImageCount; i++)
		{
			if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]) != VK_SUCCESS)
			{
				LOG_ERROR("Failed to create render finished semaphore for image {}", i);
				Cleanup(device);
				return false;
			}
		}

		LOG_INFO("Frame synchronization initialized ({} frames in flight, {} swapchain images)",
			MAX_FRAMES_IN_FLIGHT, swapchainImageCount);

		return true;
	}

	void FrameSyncManager::Cleanup(VkDevice device)
	{
		// Destroy Semaphors
		for (auto& semaphore : m_ImageAvailableSemaphores)
		{
			if (semaphore != VK_NULL_HANDLE)
			{
				vkDestroySemaphore(device, semaphore, nullptr);
				semaphore = VK_NULL_HANDLE;
			}
		}

		for (auto& semaphore : m_RenderFinishedSemaphores)
		{
			if (semaphore != VK_NULL_HANDLE)
			{
				vkDestroySemaphore(device, semaphore, nullptr);
				semaphore = VK_NULL_HANDLE;
			}
		}

		// Destroy fences
		for (auto& fence : m_InFlightFences)
		{
			if (fence != VK_NULL_HANDLE)
			{
				vkDestroyFence(device, fence, nullptr);
				fence = VK_NULL_HANDLE;
			}
		}

		// Clear vectors
		m_ImageAvailableSemaphores.clear();
		m_RenderFinishedSemaphores.clear();
		m_InFlightFences.clear();
		m_ImagesInFlight.clear();

		LOG_INFO("Frame synchronization cleaned up");
	}

	bool FrameSyncManager::WaitForFrame(VkDevice device)
	{
		// Wait for the fence from MAX_FRAMES_IN_FLIGHT submissions ago
		VkResult result = vkWaitForFences(
			device,
			1,
			&m_InFlightFences[m_CurrentFrame],
			VK_TRUE,     // Wait for all fences
			UINT64_MAX   // No timeout
		);

		if (result != VK_SUCCESS)
		{
			LOG_ERROR("Failed to wait for in-flight fence: {}", static_cast<int>(result));
			return false;
		}

		return true;
	}

	bool FrameSyncManager::AcquireNextImage(VkDevice device, VulkanSwapchain* swapchain, uint32_t& imageIndex)
	{
		bool result = swapchain->AcquireNextImage(
			imageIndex,
			m_ImageAvailableSemaphores[m_CurrentFrame]
		);

		if (!result)
		{
			// Swapchain might be out of date
			return false;
		}

		// Check if a previous frame is using this image
		if (m_ImagesInFlight[imageIndex] != VK_NULL_HANDLE)
		{
			// We need to pass device as parameter or get it another way
			// For now, just comment this out - it's a safety check
			// TODO: Pass VkDevice as parameter to this function
			//VkDevice device = swapchain->GetDevice();
			vkWaitForFences(device, 1, &m_ImagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
		}

		// Mark the image as now being in use by this frame
		m_ImagesInFlight[imageIndex] = m_InFlightFences[m_CurrentFrame];

		return true;
	}

	void FrameSyncManager::ResetFence(VkDevice device)
	{
		vkResetFences(device, 1, &m_InFlightFences[m_CurrentFrame]);
	}

	bool FrameSyncManager::SubmitCommandBuffer(VkDevice device, VkQueue graphicsQueue,
		VkCommandBuffer commandBuffer, uint32_t imageIndex)
	{
		// Reset the fence before submission
		ResetFence(device);

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		// Wait on image available semaphore
		VkSemaphore waitSemaphores[] = { m_ImageAvailableSemaphores[m_CurrentFrame] };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;

		// Submit command buffer
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		// Signal render finished semaphore when done
		VkSemaphore signalSemaphores[] = { m_RenderFinishedSemaphores[imageIndex] };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		// Submit with fence for CPU-GPU sync
		VkResult result = vkQueueSubmit(
			graphicsQueue,
			1,
			&submitInfo,
			m_InFlightFences[m_CurrentFrame]
		);

		if (result != VK_SUCCESS)
		{
			LOG_ERROR("Failed to submit draw command buffer: {}", static_cast<int>(result));
			return false;
		}

		return true;
	}

	bool FrameSyncManager::PresentImage(VulkanSwapchain* swapchain, VkQueue presentQueue, uint32_t imageIndex)
	{
		bool result = swapchain->Present(
			imageIndex,
			m_RenderFinishedSemaphores[imageIndex]
		);

		// Advance to next frame regardless of present result
		NextFrame();

		return result;
	}
}