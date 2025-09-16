#pragma once

#include "Engine/Renderer/RenderDevice.hpp"  // For Buffer base class
#include "VulkanCommon.hpp"
#include "VulkanMemoryManager.hpp"

namespace Nightbloom
{
	class VulkanDevice;
	class VulkanCommandPool;

	class VulkanBuffer : public Buffer
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
		//VkDeviceSize GetSize() const { return m_Size; }
		void* GetMappedData() const { return m_MappedData; }
		bool IsMapped() const { return m_MappedData != nullptr; }
		

		// Add these overrides to satisfy the Buffer interface:
		size_t GetSize() const override { return m_Size; }
		BufferType GetType() const override { return m_BufferType; }
		bool IsHostVisible() const override { return m_IsHostVisible; }


		void* Map() override
		{
			// If you don't have this yet, just return nullptr for now
			//LOG_WARN("Buffer mapping not implemented yet");
			return nullptr;
		}

		void Unmap() override
		{
			// Empty for now
		}

		void Update(const void* data, size_t size, size_t offset) override
		{
			// Use your existing UploadData method or implement later
			//LOG_WARN("Buffer update not implemented yet");
		}


	private:
		VulkanDevice* m_Device = nullptr;
		VulkanMemoryManager* m_MemoryManager = nullptr;
		VulkanMemoryManager::BufferAllocation* m_Allocation = nullptr;
		VkDeviceSize m_Size = 0;
		void* m_MappedData = nullptr;  // For persistently mapped buffers
		bool m_IsHostVisible = false;
		BufferType m_BufferType = BufferType::Vertex;
	};

	class UniformBuffer {
	public:
		bool Create(VulkanDevice* device, VulkanMemoryManager* mgr, size_t dataSize) {
			m_Size = dataSize;
			m_Buffer = std::make_unique<VulkanBuffer>(device, mgr);

			// Create host-visible uniform buffer
			if (!m_Buffer->CreateHostVisible(dataSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) {
				return false;
			}

			// Get the mapped pointer (your VulkanBuffer already maps it)
			m_MappedData = m_Buffer->GetMappedData();
			return m_MappedData != nullptr;
		}

		void Update(const void* data) {
			if (m_MappedData) {
				memcpy(m_MappedData, data, m_Size);
			}
		}

		VkBuffer GetBuffer() const { return m_Buffer->GetBuffer(); }
		size_t GetSize() const { return m_Size; }

	private:
		std::unique_ptr<VulkanBuffer> m_Buffer;
		void* m_MappedData = nullptr;
		size_t m_Size = 0;
	};
}