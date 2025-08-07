// VertexBuffer.hpp

#pragma once

#include <vulkan/vulkan.h>
#include <memory>

namespace Nightbloom
{
	class VulkanDevice;
	class VulkanCommandPool;

	class VulkanBuffer
	{
	public:
		VulkanBuffer(VulkanDevice* device);
		~VulkanBuffer();

		// Create buffer with memory
		bool Create(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);

		// copy data to the buffer (for host visible buffers)
		bool CopyData(const void* data, VkDeviceSize size);

		// Copy from another buffer (for staging)
		bool CopyFrom(VulkanBuffer& srcBuffer, VkDeviceSize size, VkCommandPool* commandPool);

		// Cleanup resources
		void Destroy();

		// Getters
		VkBuffer GetBuffer() const { return m_Buffer; }
		VkDeviceMemory GetMemory() const { return m_Memory; }
		VkDeviceSize GetSize() const { return m_Size; }

	private: 
		uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

	private:
		VulkanDevice* m_Device = nullptr;
		VkBuffer m_Buffer = VK_NULL_HANDLE;
		VkDeviceMemory m_Memory = VK_NULL_HANDLE;
		VkDeviceSize m_Size = 0;
		void* m_MappedMemory = nullptr;  // For persistently mapped buffers
	};
}