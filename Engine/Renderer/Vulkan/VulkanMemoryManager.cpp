//------------------------------------------------------------------------------
// VulkanMemoryManager.cpp
//
// VMA implementation for efficient GPU memory management
//------------------------------------------------------------------------------

#define VMA_IMPLEMENTATION

#include "VulkanMemoryManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"
#include "Core/Logger/Logger.hpp"
#include <algorithm>

namespace Nightbloom
{
	VulkanMemoryManager::VulkanMemoryManager(VulkanDevice* device)
		: m_Device(device)
	{
		LOG_INFO("VulkanMemoryManager created");
	}

	VulkanMemoryManager::~VulkanMemoryManager()
	{
		Shutdown();
		LOG_INFO("VulkanMemoryManager destroyed");
	}

	bool VulkanMemoryManager::Initialize()
	{
		LOG_INFO("Initializing Vulkan Memory Allocator");

		// Setup VMA creation info
		VmaVulkanFunctions vulkanFunctions = {};
		vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
		vulkanFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

		VmaAllocatorCreateInfo allocatorInfo = {};
		allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
		allocatorInfo.physicalDevice = m_Device->GetPhysicalDevice();
		allocatorInfo.device = m_Device->GetDevice();
		allocatorInfo.instance = m_Device->GetInstance();
		allocatorInfo.pVulkanFunctions = &vulkanFunctions;

		// Enable memory budget tracking
		allocatorInfo.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;

		// Create the allocator
		VkResult result = vmaCreateAllocator(&allocatorInfo, &m_Allocator);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create VMA allocator: {}", static_cast<int>(result));
			return false;
		}

		m_StagingPool = std::make_unique<StagingBufferPool>(m_Device, this);
		LOG_INFO("Created staging buffer pool");

		LOG_INFO("VMA initialized successfully");
		LogMemoryStats();

		return true;
	}

	void VulkanMemoryManager::Shutdown()
	{
		if (m_Allocator == VK_NULL_HANDLE)
			return;

		LOG_INFO("Shutting down VMA");

		// Log final memory stats before cleanup
		LogMemoryStats();

		// Destroy all tracked allocations
		if (!m_BufferAllocations.empty())
		{
			LOG_WARN("Destroying {} remaining buffer allocations", m_BufferAllocations.size());
			m_BufferAllocations.clear();
		}

		if (!m_ImageAllocations.empty())
		{
			LOG_WARN("Destroying {} remaining image allocations", m_ImageAllocations.size());
			m_ImageAllocations.clear();
		}


		// Destroy the allocator
		vmaDestroyAllocator(m_Allocator);
		m_Allocator = VK_NULL_HANDLE;

		LOG_INFO("VMA shutdown complete");
	}

	void VulkanMemoryManager::DestroyStagingPool()
	{
		//Destroy StagingPool
		if (m_StagingPool)
		{
			m_StagingPool->Cleanup();
			m_StagingPool.reset();
		}
	}

	VulkanMemoryManager::BufferAllocation* VulkanMemoryManager::CreateBuffer(const BufferCreateInfo& createInfo)
	{
		auto allocation = std::make_unique<BufferAllocation>();

		// Setup buffer creation info
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = createInfo.size;
		bufferInfo.usage = createInfo.usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		// Setup allocation info
		VmaAllocationCreateInfo allocInfo = {};
		allocInfo.usage = createInfo.memoryUsage;
		allocInfo.flags = createInfo.flags;

		// Add host access flag if mappable
		if (createInfo.mappable)
		{
			allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
			allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;  // Keep persistently mapped
		}

		// Create buffer
		VkResult result = vmaCreateBuffer(
			m_Allocator,
			&bufferInfo,
			&allocInfo,
			&allocation->buffer,
			&allocation->allocation,
			&allocation->allocationInfo
		);

		if (result != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create buffer through VMA: {}", static_cast<int>(result));
			return nullptr;
		}

		// If mappable and successfully mapped, store the pointer
		if (createInfo.mappable && allocation->allocationInfo.pMappedData)
		{
			allocation->mappedData = allocation->allocationInfo.pMappedData;
		}

		// Track the allocation
		BufferAllocation* rawPtr = allocation.get();
		m_BufferAllocations.push_back(std::move(allocation));

		LOG_TRACE("Created buffer: size={} bytes, usage=0x{:X}", createInfo.size, createInfo.usage);

		return rawPtr;
	}

	void VulkanMemoryManager::DestroyBuffer(BufferAllocation* allocation)
	{
		if (!allocation)
			return;

		// Find and remove from tracking
		auto it = std::find_if(m_BufferAllocations.begin(), m_BufferAllocations.end(),
			[allocation](const std::unique_ptr<BufferAllocation>& ptr) {
				return ptr.get() == allocation;
			});

		if (it != m_BufferAllocations.end())
		{
			// VMA will handle unmapping if needed
			vmaDestroyBuffer(m_Allocator, (*it)->buffer, (*it)->allocation);
			m_BufferAllocations.erase(it);
			LOG_TRACE("Destroyed buffer allocation");
		}
	}

	VulkanMemoryManager::ImageAllocation* VulkanMemoryManager::CreateImage(const ImageCreateInfo& createInfo)
	{
		auto allocation = std::make_unique<ImageAllocation>();

		// Setup image creation info
		VkImageCreateInfo imageInfo = {};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = (createInfo.depth > 1) ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = createInfo.width;
		imageInfo.extent.height = createInfo.height;
		imageInfo.extent.depth = createInfo.depth;
		imageInfo.mipLevels = createInfo.mipLevels;
		imageInfo.arrayLayers = createInfo.arrayLayers;
		imageInfo.format = createInfo.format;
		imageInfo.tiling = createInfo.tiling;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = createInfo.usage;
		imageInfo.samples = createInfo.samples;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		// Setup allocation info
		VmaAllocationCreateInfo allocInfo = {};
		allocInfo.usage = createInfo.memoryUsage;

		// Create image
		VkResult result = vmaCreateImage(
			m_Allocator,
			&imageInfo,
			&allocInfo,
			&allocation->image,
			&allocation->allocation,
			&allocation->allocationInfo
		);

		if (result != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create image through VMA: {}", static_cast<int>(result));
			return nullptr;
		}

		// Track the allocation
		ImageAllocation* rawPtr = allocation.get();
		m_ImageAllocations.push_back(std::move(allocation));

		LOG_TRACE("Created image: {}x{}x{}, format={}, mips={}",
			createInfo.width, createInfo.height, createInfo.depth,
			static_cast<int>(createInfo.format), createInfo.mipLevels);

		return rawPtr;
	}

	void VulkanMemoryManager::DestroyImage(ImageAllocation* allocation)
	{
		if (!allocation)
			return;

		// Find and remove from tracking
		auto it = std::find_if(m_ImageAllocations.begin(), m_ImageAllocations.end(),
			[allocation](const std::unique_ptr<ImageAllocation>& ptr) {
				return ptr.get() == allocation;
			});

		if (it != m_ImageAllocations.end())
		{
			vmaDestroyImage(m_Allocator, (*it)->image, (*it)->allocation);
			m_ImageAllocations.erase(it);
			LOG_TRACE("Destroyed image allocation");
		}
	}

	void* VulkanMemoryManager::MapMemory(VmaAllocation allocation)
	{
		void* mappedData = nullptr;
		VkResult result = vmaMapMemory(m_Allocator, allocation, &mappedData);

		if (result != VK_SUCCESS)
		{
			LOG_ERROR("Failed to map memory: {}", static_cast<int>(result));
			return nullptr;
		}

		return mappedData;
	}

	void VulkanMemoryManager::UnmapMemory(VmaAllocation allocation)
	{
		vmaUnmapMemory(m_Allocator, allocation);
	}

	void VulkanMemoryManager::FlushMemory(VmaAllocation allocation, VkDeviceSize offset, VkDeviceSize size)
	{
		vmaFlushAllocation(m_Allocator, allocation, offset, size);
	}

	VulkanMemoryManager::MemoryStats VulkanMemoryManager::GetMemoryStats() const
	{
		MemoryStats stats = {};

		// Get detailed stats from VMA
		VmaTotalStatistics vmaStats = {};
		vmaCalculateStatistics(m_Allocator, &vmaStats);

		stats.totalAllocatedBytes = vmaStats.total.statistics.blockBytes;
		stats.totalUsedBytes = vmaStats.total.statistics.allocationBytes;
		stats.allocationCount = vmaStats.total.statistics.allocationCount;

		// Get budget info
		VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
		vmaGetHeapBudgets(m_Allocator, budgets);

		// Sum up device local memory (usually heap 0)
		for (uint32_t i = 0; i < VK_MAX_MEMORY_HEAPS; ++i)
		{
			stats.totalDeviceMemory += budgets[i].budget;
			stats.usedDeviceMemory += budgets[i].usage;
		}

		return stats;
	}

	void VulkanMemoryManager::LogMemoryStats() const
	{
		auto stats = GetMemoryStats();

		LOG_INFO("=== VMA Memory Statistics ===");
		LOG_INFO("  Allocations: {}", stats.allocationCount);
		LOG_INFO("  Used Memory: {:.2f} MB / {:.2f} MB",
			stats.totalUsedBytes / (1024.0 * 1024.0),
			stats.totalAllocatedBytes / (1024.0 * 1024.0));
		LOG_INFO("  Device Memory: {:.2f} MB / {:.2f} MB",
			stats.usedDeviceMemory / (1024.0 * 1024.0),
			stats.totalDeviceMemory / (1024.0 * 1024.0));
		LOG_INFO("  Tracked Buffers: {}", m_BufferAllocations.size());
		LOG_INFO("  Tracked Images: {}", m_ImageAllocations.size());
	}
}