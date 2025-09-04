//------------------------------------------------------------------------------
// VulkanCommandPool.cpp
//
// Command pool implementation
//------------------------------------------------------------------------------

#include "VulkanDevice.hpp"
#include "VulkanCommandPool.hpp"
#include "Core/Logger/Logger.hpp"

namespace Nightbloom
{
	VulkanCommandPool::VulkanCommandPool(VulkanDevice* device)
		: m_Device(device)
	{
	}

	VulkanCommandPool::~VulkanCommandPool()
	{
		Shutdown();
	}

	bool VulkanCommandPool::Initialize(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags)
	{
		m_QueueFamilyIndex = queueFamilyIndex;

		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = flags;
		poolInfo.queueFamilyIndex = m_QueueFamilyIndex;

		VkResult result = vkCreateCommandPool(
			m_Device->GetDevice(), 
			&poolInfo, 
			nullptr, 
			&m_CommandPool
		);

		if (result != VK_SUCCESS)
		{
			//LOG_ERROR("Failed to create command pool: {}", result);
			return false;
		}

		LOG_INFO("Command pool created for queue family {}", queueFamilyIndex);
		return true;
	}

	void VulkanCommandPool::Shutdown()
	{
		if (m_CommandPool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(m_Device->GetDevice(), m_CommandPool, nullptr);
			m_CommandPool = VK_NULL_HANDLE;
			LOG_INFO("Command pool destroyed");
		}
	}

	VkCommandBuffer VulkanCommandPool::AllocateCommandBuffer(VkCommandBufferLevel level)
	{
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = m_CommandPool;
		allocInfo.level = level;
		allocInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer;
		VkResult result = vkAllocateCommandBuffers(
			m_Device->GetDevice(), 
			&allocInfo, 
			&commandBuffer
		);

		if (result != VK_SUCCESS)
		{
			//LOG_ERROR("Failed to allocate command buffer: {}", result);
			return VK_NULL_HANDLE;
		}

		return commandBuffer;
	}

	std::vector<VkCommandBuffer> VulkanCommandPool::AllocateCommandBuffers(uint32_t count, VkCommandBufferLevel level)
	{
		std::vector<VkCommandBuffer> commandBuffers(count);

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = m_CommandPool;
		allocInfo.level = level;
		allocInfo.commandBufferCount = count;

		VkResult result = vkAllocateCommandBuffers(
			m_Device->GetDevice(),
			&allocInfo,
			commandBuffers.data()
		);

		if (result != VK_SUCCESS)
		{
			//LOG_ERROR("Failed to allocate command buffers: {}", result);
			return {};
		}

		return commandBuffers;
	}

	void VulkanCommandPool::FreeCommandBuffer(VkCommandBuffer commandBuffer)
	{
		vkFreeCommandBuffers(m_Device->GetDevice(), m_CommandPool, 1, &commandBuffer);
	}

	void VulkanCommandPool::FreeCommandBuffers(const std::vector<VkCommandBuffer>& commandBuffers)
	{
		vkFreeCommandBuffers(m_Device->GetDevice(), m_CommandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
	}

	VkCommandBuffer VulkanCommandPool::BeginSingleTimeCommand()
	{
		VkCommandBuffer commandBuffer = AllocateCommandBuffer();

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to begin single time command buffer");
			FreeCommandBuffer(commandBuffer);
			return VK_NULL_HANDLE;
		}

		return commandBuffer;
	}

	void VulkanCommandPool::EndSingleTimeCommand(VkCommandBuffer commandBuffer, VkQueue queue)
	{
		if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to end single time command buffer");
			FreeCommandBuffer(commandBuffer);
			return;
		}

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		// Get the graphics queue from the device
		// You'll need to add a GetGraphicsQueue() method to VulkanDevice if you don't have one
		VkQueue graphicsQueue = m_Device->GetGraphicsQueue();

		// Submit and wait for completion
		if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to submit single time command buffer");
		}

		vkQueueWaitIdle(graphicsQueue);

		// Free the command buffer
		FreeCommandBuffer(commandBuffer);
	}

	void VulkanCommandPool::Reset()
	{
		vkResetCommandPool(m_Device->GetDevice(), m_CommandPool, 0);
	}

	// VulkanSingleTimeCommand implementation
	VulkanSingleTimeCommand::VulkanSingleTimeCommand(VulkanDevice* device, VulkanCommandPool* commandPool)
		: m_Device(device), m_CommandPool(commandPool)
	{
	}

	VulkanSingleTimeCommand::~VulkanSingleTimeCommand()
	{
		if (m_Started)
		{
			End();
		}
	}

	VkCommandBuffer VulkanSingleTimeCommand::Begin()
	{
		m_CommandBuffer = m_CommandPool->AllocateCommandBuffer();

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		vkBeginCommandBuffer(m_CommandBuffer, &beginInfo);
		m_Started = true;

		return m_CommandBuffer;
	}

	void VulkanSingleTimeCommand::End()
	{
		if (!m_Started)
			return;

		vkEndCommandBuffer(m_CommandBuffer);

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &m_CommandBuffer;

		vkQueueSubmit(m_Device->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(m_Device->GetGraphicsQueue());

		m_CommandPool->FreeCommandBuffer(m_CommandBuffer);
		m_CommandBuffer = VK_NULL_HANDLE;
		m_Started = false;
	}
}