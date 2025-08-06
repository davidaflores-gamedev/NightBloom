//------------------------------------------------------------------------------
// VulkanCommandPool.hpp
//
// Manages Vulkan command pools and command buffer allocation
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include <vector>

namespace Nightbloom
{
	class VulkanCommandPool
	{
	public:
		VulkanCommandPool(VulkanDevice* device);
		~VulkanCommandPool();

		// Command Pool operations
		bool Initialize(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags = 0);
		void Shutdown();

		// Command Buffer allocation
		VkCommandBuffer AllocateCommandBuffer(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
		std::vector<VkCommandBuffer> AllocateCommandBuffers(uint32_t count, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

		void FreeCommandBuffer(VkCommandBuffer commandBuffer);
		void FreeCommandBuffers(const std::vector<VkCommandBuffer>& commandBuffers);

		// Reset command pool
		void Reset();

		// Getters
		VkCommandPool GetPool() const { return m_CommandPool; }

	private:
		VulkanDevice* m_Device = nullptr;
		VkCommandPool m_CommandPool = VK_NULL_HANDLE;
		uint32_t m_QueueFamilyIndex = 0;
	};

	class VulkanSingleTimeCommand
	{
	public: 
		VulkanSingleTimeCommand(VulkanDevice* device, VulkanCommandPool* commandPool);
		~VulkanSingleTimeCommand();

		VkCommandBuffer Begin();
		void End();

	private:
		VulkanDevice* m_Device = nullptr;
		VulkanCommandPool* m_CommandPool = nullptr;
		VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
		bool m_Started = false;
	};
}