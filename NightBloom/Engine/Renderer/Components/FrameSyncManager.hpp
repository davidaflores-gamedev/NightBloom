//------------------------------------------------------------------------------
// FrameSyncManager.hpp
//
// Manages frame synchronization primitives (semaphores, fences)
// Handles frame pacing and swapchain image acquisition
//------------------------------------------------------------------------------
#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

namespace Nightbloom
{
	class VulkanSwapchain;

	class FrameSyncManager
	{
	public:
		static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

		FrameSyncManager() = default;
		~FrameSyncManager() = default;

		// Lifecycle
		bool Initialize(VkDevice device, uint32_t swapchainImageCount);
		void Cleanup(VkDevice device);

		// Frame management
		bool WaitForFrame(VkDevice device);
		bool AcquireNextImage(VkDevice device, VulkanSwapchain* swapchain, uint32_t& imageIndex);
		void ResetFence(VkDevice device);

		// Submission and Presentation
		bool SubmitCommandBuffer(VkDevice device, VkQueue graphicsQueue, VkCommandBuffer commandBuffer, uint32_t imageIndex);
		bool PresentImage(VulkanSwapchain* swapchain, VkQueue presentQueue, uint32_t imageIndex);
		void NextFrame() { m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT; }

		// Getters
		uint32_t GetCurrentFrame() const { return m_CurrentFrame; }
		VkSemaphore GetImageAvailableSemaphore() const { return m_ImageAvailableSemaphores[m_CurrentFrame]; }
		VkSemaphore GetRenderFinishedSemaphore(uint32_t imageIndex) const { return m_RenderFinishedSemaphores[imageIndex]; }
		VkFence GetInFlightFence() const { return m_InFlightFences[m_CurrentFrame]; }

	private:
		uint32_t m_CurrentFrame = 0;

		// Synchronization objects (per frame in flight)
		std::vector<VkSemaphore> m_ImageAvailableSemaphores;
		std::vector<VkFence> m_InFlightFences;

		// Per Swapchain Image
		std::vector<VkSemaphore> m_RenderFinishedSemaphores;
		std::vector<VkFence> m_ImagesInFlight;

		// prevent copying
		FrameSyncManager(const FrameSyncManager&) = delete;
		FrameSyncManager& operator=(const FrameSyncManager&) = delete;

	};
}