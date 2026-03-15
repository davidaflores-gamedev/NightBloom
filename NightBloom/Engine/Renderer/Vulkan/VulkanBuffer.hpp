//------------------------------------------------------------------------------
// VulkanBuffer.hpp
//
// Unified buffer implementation for all buffer types
// Replaces VulkanBuffer, UniformBuffer, and StagingBuffer classes
//------------------------------------------------------------------------------

#pragma once

#include "Engine/Renderer/RenderDevice.hpp"  // For Buffer base class
#include "VulkanCommon.hpp"
#include "VulkanMemoryManager.hpp"
#include <atomic>
#include <string>

namespace Nightbloom
{
	class VulkanDevice;
	class VulkanCommandPool;

	class VulkanBuffer : public Buffer
	{
	public:
		VulkanBuffer(VulkanDevice* device, VulkanMemoryManager* memoryManager);
		~VulkanBuffer();

		bool Initialize(const BufferDesc& desc);

		bool UploadData(const void* data, size_t size, size_t offset = 0,
			VulkanCommandPool* cmdPool = nullptr);

		size_t GetSize() const override { return m_Size; }
		BufferUsage GetUsage() const override { return m_Usage; }
		MemoryAccess GetMemoryAccess() const override { return m_MemoryAccess; }

		void* Map(size_t offset = 0, size_t size = 0) override;
		void Unmap() override;
		void Flush(size_t offset = 0, size_t size = 0) override;

		bool Update(const void* data, size_t size, size_t offset = 0) override;

		void* GetPersistentMappedPtr() const override { return m_PersistentMapped; }
		bool IsHostVisible() const override { return m_IsHostVisible; }
		bool IsMapped() const override { return m_MappedData != nullptr || m_PersistentMapped != nullptr; }
		const std::string& GetDebugName() const override { return m_DebugName; }

		// Vulkan Specific
		VkBuffer GetBuffer() const { return m_Allocation ? m_Allocation->buffer : VK_NULL_HANDLE; }
		VulkanMemoryManager::BufferAllocation* GetAllocation() const { return m_Allocation; }

	private:
		//----------------------------------------------------------------------
		// Helper Methods
		//----------------------------------------------------------------------

		// Convert our enums to Vulkan flags
		VkBufferUsageFlags GetVulkanUsageFlags(BufferUsage usage);
		VmaMemoryUsage GetVmaMemoryUsage(MemoryAccess access);

		// Internal buffer creation
		bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags vkUsage,
			VmaMemoryUsage vmaUsage, bool persistentMap);


		VulkanDevice* m_Device = nullptr;
		VulkanMemoryManager* m_MemoryManager = nullptr;

		VulkanMemoryManager::BufferAllocation* m_Allocation = nullptr;

		VkDeviceSize m_Size = 0;
		BufferUsage m_Usage = BufferUsage::Vertex;
		MemoryAccess m_MemoryAccess = MemoryAccess::GpuOnly;
		bool m_IsHostVisible = false;
		std::string m_DebugName;

		void* m_MappedData = nullptr;           // Current mapped pointer
		void* m_PersistentMapped = nullptr;     // Persistent mapping (for uniforms)
		std::atomic<int> m_MapRefCount{ 0 };      // Reference count for nested maps
		size_t m_MappedOffset = 0;              // Currently mapped offset
		size_t m_MappedSize = 0;                // Currently mapped size
	};
}