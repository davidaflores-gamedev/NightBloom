#include "VulkanBuffer.hpp"
#include "VulkanDevice.hpp"
#include "VulkanCommandPool.hpp"
#include "Core/Logger/Logger.hpp"

namespace Nightbloom
{
	Nightbloom::VulkanBuffer::VulkanBuffer(VulkanDevice* device, VulkanMemoryManager* memoryManager)
		: m_Device(device)
		, m_MemoryManager(memoryManager)
	{
	}

	Nightbloom::VulkanBuffer::~VulkanBuffer()
	{
		Destroy();
	}

	bool Nightbloom::VulkanBuffer::Create(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
	{
		if (!m_MemoryManager)
		{
			LOG_ERROR("VulkanBuffer: Memory manager not set");
			return false;
		}

		m_Size = size;

		VulkanMemoryManager::BufferCreateInfo createInfo = {};
		createInfo.size = size;
		createInfo.usage = usage;
		createInfo.memoryUsage = memoryUsage;

		// If host visible, request mapping
		if (memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU ||
			memoryUsage == VMA_MEMORY_USAGE_CPU_ONLY ||
			memoryUsage == VMA_MEMORY_USAGE_GPU_TO_CPU)
		{
			createInfo.mappable = true;
			m_IsHostVisible = true;
		}

		m_Allocation = m_MemoryManager->CreateBuffer(createInfo);
		if (!m_Allocation)
		{
			LOG_ERROR("Failed to create buffer through VMA");
			return false;
		}

		// Store mapped pointer if available
		if (m_Allocation->mappedData)
		{
			m_MappedData = m_Allocation->mappedData;
			LOG_TRACE("Buffer created with persistent mapping");
		}

		LOG_TRACE("Buffer created: size={} bytes, usage=0x{:X}", size, usage);
		return true;

	}

	bool VulkanBuffer::CreateHostVisible(VkDeviceSize size, VkBufferUsageFlags usage)
	{
		return Create(size, usage, VMA_MEMORY_USAGE_CPU_TO_GPU);
	}

	bool VulkanBuffer::CreateDeviceLocal(VkDeviceSize size, VkBufferUsageFlags usage)
	{
		return Create(size, usage, VMA_MEMORY_USAGE_GPU_ONLY);
	}

	bool Nightbloom::VulkanBuffer::CopyData(const void* data, VkDeviceSize size, VkDeviceSize offset)
	{
		if (!m_MappedData || !m_IsHostVisible)
		{
			LOG_ERROR("Buffer is not mapped or not host visible");
			return false;
		}

		if (offset + size > m_Size)
		{
			LOG_ERROR("Copy would exceed buffer size");
			return false;
		}

		memcpy(static_cast<uint8_t*>(m_MappedData) + offset, data, static_cast<size_t>(size));

		// Flush if needed (VMA handles this based on memory type)
		if (m_Allocation)
		{
			m_MemoryManager->FlushMemory(m_Allocation->allocation, offset, size);
		}

		return true;
	}

	bool Nightbloom::VulkanBuffer::CopyFrom(VulkanBuffer& srcBuffer, VkDeviceSize size, VulkanCommandPool* commandPool)
	{
		// Create one-time command buffer

		m_Device;
		VulkanSingleTimeCommand cmd(m_Device, commandPool);
		VkCommandBuffer commandBuffer = cmd.Begin();

		VkBufferCopy copyRegion = {};
		copyRegion.srcOffset = 0; // Optional
		copyRegion.dstOffset = 0; // Optional
		copyRegion.size = size;
		vkCmdCopyBuffer(commandBuffer, srcBuffer.GetBuffer(), GetBuffer(), 1, &copyRegion);

		cmd.End();
		return true;
	}

	bool VulkanBuffer::UploadData(const void* data, VkDeviceSize size, VulkanCommandPool* commandPool)
	{
		// Create staging buffer
		StagingBuffer staging(m_MemoryManager, size);

		// Copy data to staging
		if (!staging.CopyData(data, size))
		{
			LOG_ERROR("Failed to copy data to staging buffer");
			return false;
		}

		// Copy from staging to device
		VulkanSingleTimeCommand cmd(m_Device, commandPool);
		VkCommandBuffer commandBuffer = cmd.Begin();

		VkBufferCopy copyRegion = {};
		copyRegion.size = size;
		vkCmdCopyBuffer(commandBuffer, staging.GetBuffer(), GetBuffer(), 1, &copyRegion);

		cmd.End();

		LOG_TRACE("Uploaded {} bytes to device buffer", size);
		return true;
	}

	void VulkanBuffer::Destroy()
	{
		if (m_Allocation)
		{
			m_MemoryManager->DestroyBuffer(m_Allocation);
			m_Allocation = nullptr;
			m_MappedData = nullptr;
			m_Size = 0;
			m_IsHostVisible = false;
		}
	}
}
