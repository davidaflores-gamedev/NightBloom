#pragma once

#include "VulkanCommon.hpp"
#include "VulkanMemoryManager.hpp"

namespace Nightbloom
{
	class VulkanDevice;
	class VulkanCommandPool;

	class VulkanBuffer
	{
	public:
		VulkanBuffer(VulkanDevice* device, VulkanMemoryManager* memoryManager);
		~VulkanBuffer();

		// Create buffer with VMA
		bool Create(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO);

		// Create with specific requirements
		bool CreateHostVisible(VkDeviceSize size, VkBufferUsageFlags usage);
		bool CreateDeviceLocal(VkDeviceSize size, VkBufferUsageFlags usage);

		// Copy data to the buffer (for host visible buffers)
		bool CopyData(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

		// Copy from another buffer using staging
		bool CopyFrom(VulkanBuffer& srcBuffer, VkDeviceSize size, VulkanCommandPool* commandPool);

		// Copy from host memory using staging
		bool UploadData(const void* data, VkDeviceSize size, VulkanCommandPool* commandPool);

		// Cleanup resources
		void Destroy();

		// Getters
		VkBuffer GetBuffer() const { return m_Allocation ? m_Allocation->buffer : VK_NULL_HANDLE; }
		VkDeviceSize GetSize() const { return m_Size; }
		void* GetMappedData() const { return m_MappedData; }
		bool IsMapped() const { return m_MappedData != nullptr; }

	private:
		VulkanDevice* m_Device = nullptr;
		VulkanMemoryManager* m_MemoryManager = nullptr;
		VulkanMemoryManager::BufferAllocation* m_Allocation = nullptr;
		VkDeviceSize m_Size = 0;
		void* m_MappedData = nullptr;  // For persistently mapped buffers
		bool m_IsHostVisible = false;
	};
}