//------------------------------------------------------------------------------
// VulkanMemoryManager.hpp
//
// Manages memory allocation using Vulkan Memory Allocator (VMA)
// This replaces all raw vkAllocateMemory calls
//------------------------------------------------------------------------------

#pragma once

#include "Engine/Renderer/Vulkan/VulkanCommon.hpp"
#include "Engine/Renderer/Vulkan/VulkanStagingBufferPool.hpp"

// VMA Configuration

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#include "ThirdParty/VMA/vk_mem_alloc.h"
#include <memory>

namespace Nightbloom
{
	class VulkanDevice;

	// Forward declarations for out allocation types
	struct BufferAllocation;
	struct ImageAllocation;

	// Centralizes all GPU memory management through VMA
	class VulkanMemoryManager
	{
	public:
		VulkanMemoryManager(VulkanDevice* device);
		~VulkanMemoryManager();

		// Initialize VMA
		bool Initialize();
		void Shutdown();
		void DestroyStagingPool();

		// Buffer Allocation
		struct BufferCreateInfo
		{
			VkDeviceSize size;
			VkBufferUsageFlags usage;
			VmaMemoryUsage memoryUsage = VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO;
			VmaAllocationCreateFlags flags = 0;
			bool mappable = false; // if true, adds host_access flag
		};

		struct BufferAllocation
		{
			VkBuffer buffer = VK_NULL_HANDLE;
			VmaAllocation allocation = VK_NULL_HANDLE;
			VmaAllocationInfo allocationInfo = {};
			void* mappedData = nullptr;  // Only valid if persistently mapped
		};

		BufferAllocation* CreateBuffer(const BufferCreateInfo& createInfo);
		//void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
		void DestroyBuffer(BufferAllocation* allocation);

		// Image allocation
		struct ImageCreateInfo
		{
			uint32_t width;
			uint32_t height;
			uint32_t depth = 1;
			uint32_t mipLevels = 1;
			uint32_t arrayLayers = 1;
			VkFormat format;
			VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
			VkImageUsageFlags usage;
			VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
			VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
		};

		struct ImageAllocation
		{
			VkImage image = VK_NULL_HANDLE;
			VmaAllocation allocation = VK_NULL_HANDLE;
			VmaAllocationInfo allocationInfo = {};
		};

		ImageAllocation* CreateImage(const ImageCreateInfo& createInfo);
		void DestroyImage(ImageAllocation* allocation);

		// Memory operations
		void* MapMemory(VmaAllocation allocation);
		void UnmapMemory(VmaAllocation allocation);
		void FlushMemory(VmaAllocation allocation, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);

		// Statistics and debugging
		struct MemoryStats
		{
			size_t totalAllocatedBytes;
			size_t totalUsedBytes;
			size_t allocationCount;
			size_t totalDeviceMemory;
			size_t usedDeviceMemory;
		};

		MemoryStats GetMemoryStats() const;
		void LogMemoryStats() const;

		// Get the allocator for advanced usage
		VmaAllocator GetAllocator() const { return m_Allocator; }

		StagingBufferPool* GetStagingPool() { return m_StagingPool.get(); }

	private:
		VulkanDevice* m_Device = nullptr;
		VmaAllocator m_Allocator = VK_NULL_HANDLE;

		// Track allocations for cleanup and debugging
		std::vector<std::unique_ptr<BufferAllocation>> m_BufferAllocations;
		std::vector<std::unique_ptr<ImageAllocation>> m_ImageAllocations;
		std::unique_ptr<StagingBufferPool> m_StagingPool;
	};
}