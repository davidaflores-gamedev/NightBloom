//------------------------------------------------------------------------------
// VulkanBuffer.cpp
//
// Unified buffer implementation for all buffer types
//------------------------------------------------------------------------------

#include "VulkanBuffer.hpp"
#include "VulkanDevice.hpp"
#include "VulkanCommandPool.hpp"
#include "Core/Logger/Logger.hpp"
#include <cstring>

namespace Nightbloom
{
	VulkanBuffer::VulkanBuffer(VulkanDevice* device, VulkanMemoryManager* memoryManager)
		: m_Device(device)
		, m_MemoryManager(memoryManager)
	{
		LOG_TRACE("Creating VulkanBuffer");
	}

	VulkanBuffer::~VulkanBuffer()
	{
		if (m_Allocation)
		{
			// Unmap if needed (but not persistent mappings - they're unmapped by VMA)
			if (m_MappedData && !m_PersistentMapped)
			{
				Unmap();
			}

			// Destroy the buffer allocation
			m_MemoryManager->DestroyBuffer(m_Allocation);
			m_Allocation = nullptr;

			LOG_TRACE("Destroyed VulkanBuffer '{}'", m_DebugName);
		}
	}

	bool VulkanBuffer::Initialize(const BufferDesc& desc)
	{
		// Validate input
		if (desc.size == 0)
		{
			LOG_ERROR("Cannot create buffer with size 0");
			return false;
		}

		// Store properties
		m_Size = desc.size;
		m_Usage = desc.usage;
		m_MemoryAccess = desc.memoryAccess;
		m_DebugName = desc.debugName.empty() ? "UnnamedBuffer" : desc.debugName;

		// Determine if host visible based on memory access
		m_IsHostVisible = (m_MemoryAccess != MemoryAccess::GpuOnly);

		// Special handling for certain buffer types
		if (m_Usage == BufferUsage::Staging)
		{
			// Staging buffers MUST be host visible
			m_IsHostVisible = true;
			if (m_MemoryAccess == MemoryAccess::GpuOnly)
			{
				LOG_WARN("Staging buffer must be host visible, overriding memory access");
				m_MemoryAccess = MemoryAccess::CpuToGpu;
			}
		}
		else if (m_Usage == BufferUsage::Uniform)
		{
			// Uniform buffers are typically host visible for updates
			if (m_MemoryAccess == MemoryAccess::GpuOnly)
			{
				LOG_WARN("Uniform buffer with GpuOnly access may not be updateable");
			}
		}

		// Get Vulkan usage flags and memory usage
		VkBufferUsageFlags vkUsage = GetVulkanUsageFlags(m_Usage);
		VmaMemoryUsage vmaUsage = GetVmaMemoryUsage(m_MemoryAccess);

		// Create the buffer
		if (!CreateBuffer(desc.size, vkUsage, vmaUsage, desc.persistentMap))
		{
			LOG_ERROR("Failed to create buffer '{}'", m_DebugName);
			return false;
		}

		// Handle persistent mapping (typically for uniform buffers)
		if (desc.persistentMap && m_IsHostVisible)
		{
			m_PersistentMapped = Map();
			if (!m_PersistentMapped)
			{
				LOG_ERROR("Failed to persistently map buffer '{}'", m_DebugName);
				return false;
			}
			// Don't unmap - keep it mapped
		}

		// Upload initial data if provided
		if (desc.initialData && desc.initialDataSize > 0)
		{
			size_t uploadSize = desc.initialDataSize ? desc.initialDataSize : desc.size;
			if (!Update(desc.initialData, uploadSize, 0))
			{
				LOG_ERROR("Failed to upload initial data for buffer '{}'", m_DebugName);
				return false;
			}
		}

		// Set legacy buffer type for compatibility
		//m_BufferType = ConvertUsageToLegacyType(m_Usage);

		LOG_INFO("Created buffer '{}': size={} bytes, usage={}, access={}",
			m_DebugName, m_Size,
			static_cast<int>(m_Usage),
			static_cast<int>(m_MemoryAccess));

		return true;
	}

	bool VulkanBuffer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags vkUsage,
		VmaMemoryUsage vmaUsage, bool persistentMap)
	{
		VulkanMemoryManager::BufferCreateInfo createInfo = {};
		createInfo.size = size;
		createInfo.usage = vkUsage;
		createInfo.memoryUsage = vmaUsage;
		createInfo.mappable = m_IsHostVisible;

		// Add flags for persistent mapping
		if (persistentMap && m_IsHostVisible)
		{
			createInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
			createInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
		}

		// Create the buffer through VMA
		m_Allocation = m_MemoryManager->CreateBuffer(createInfo);
		if (!m_Allocation)
		{
			LOG_ERROR("Failed to allocate buffer through VMA");
			return false;
		}

		// Store persistent mapping if it was created
		if (persistentMap && m_Allocation->mappedData)
		{
			m_PersistentMapped = m_Allocation->mappedData;
		}

		return true;
	}

	void* VulkanBuffer::Map(size_t offset, size_t size)
	{
		if (!m_IsHostVisible)
		{
			LOG_ERROR("Cannot map GPU-only buffer '{}'", m_DebugName);
			return nullptr;
		}

		// If persistently mapped, just return the offset pointer
		if (m_PersistentMapped)
		{
			m_MapRefCount++;
			return static_cast<uint8_t*>(m_PersistentMapped) + offset;
		}

		// Handle nested mapping
		if (m_MappedData)
		{
			// Already mapped - just increment ref count and return
			m_MapRefCount++;
			return static_cast<uint8_t*>(m_MappedData) + offset;
		}

		// Map the memory
		m_MappedData = m_MemoryManager->MapMemory(m_Allocation->allocation);
		if (!m_MappedData)
		{
			LOG_ERROR("Failed to map buffer '{}'", m_DebugName);
			return nullptr;
		}

		m_MapRefCount = 1;
		m_MappedOffset = offset;
		m_MappedSize = (size == 0) ? (m_Size - offset) : size;

		LOG_TRACE("Mapped buffer '{}' at offset {}, size {}", m_DebugName, offset, m_MappedSize);

		return static_cast<uint8_t*>(m_MappedData) + offset;
	}

	void VulkanBuffer::Unmap()
	{
		// Never unmap persistent mappings
		if (m_PersistentMapped)
		{
			if (m_MapRefCount > 0)
				m_MapRefCount--;
			return;
		}

		// Handle nested unmapping
		if (m_MapRefCount > 0)
		{
			m_MapRefCount--;
			if (m_MapRefCount == 0 && m_MappedData)
			{
				m_MemoryManager->UnmapMemory(m_Allocation->allocation);
				m_MappedData = nullptr;
				m_MappedOffset = 0;
				m_MappedSize = 0;

				LOG_TRACE("Unmapped buffer '{}'", m_DebugName);
			}
		}
	}

	void VulkanBuffer::Flush(size_t offset, size_t size)
	{
		if (!m_IsHostVisible)
			return;

		// Flush the memory range to make CPU writes visible to GPU
		VkDeviceSize flushSize = (size == 0) ? VK_WHOLE_SIZE : size;
		m_MemoryManager->FlushMemory(m_Allocation->allocation, offset, flushSize);

		//LOG_TRACE("Flushed buffer '{}' at offset {}, size {}", m_DebugName, offset, flushSize);
	}

	bool VulkanBuffer::Update(const void* data, size_t size, size_t offset)
	{
		// Validate parameters
		if (!data || size == 0 || (offset + size) > m_Size)
		{
			LOG_ERROR("Invalid update parameters for buffer '{}': size={}, offset={}, buffer_size={}",
				m_DebugName, size, offset, m_Size);
			return false;
		}

		if (m_IsHostVisible)
		{
			// Direct update for host-visible buffers
			void* mapped = Map(offset, size);
			if (!mapped)
			{
				LOG_ERROR("Failed to map buffer '{}' for update", m_DebugName);
				return false;
			}

			// Copy the data
			memcpy(mapped, data, size);

			// Flush to ensure visibility
			Flush(offset, size);

			// Unmap (will be no-op for persistent mappings)
			Unmap();

			LOG_TRACE("Updated {} bytes in buffer '{}' at offset {}", size, m_DebugName, offset);
			return true;
		}
		else
		{
			// Device-local buffers cannot be updated directly
			LOG_ERROR("Cannot update device-local buffer '{}' without staging. Use UploadData() instead.",
				m_DebugName);
			return false;
		}
	}

	bool VulkanBuffer::UploadData(const void* data, size_t size, size_t offset,
		VulkanCommandPool* cmdPool)
	{
		// Validate parameters
		if (!data || size == 0 || (offset + size) > m_Size)
		{
			LOG_ERROR("Invalid upload parameters for buffer '{}': size={}, offset={}, buffer_size={}",
				m_DebugName, size, offset, m_Size);
			return false;
		}

		// If host visible, just use Update
		if (m_IsHostVisible)
		{
			return Update(data, size, offset);
		}

		// For device-local buffers, we need staging
		if (!cmdPool)
		{
			LOG_ERROR("Command pool required for device-local buffer upload");
			return false;
		}

		// Get staging pool from memory manager
		StagingBufferPool* pool = m_MemoryManager->GetStagingPool();
		if (!pool)
		{
			LOG_ERROR("No staging buffer pool available");
			return false;
		}

		// Use pool for staging
		bool success = pool->WithStagingBuffer(size,
			[&](VulkanBuffer* stagingBuffer) {
				// Upload data to staging
				if (!stagingBuffer->Update(data, size, 0))
				{
					LOG_ERROR("Failed to update staging buffer");
					return false;
				}

				// Record copy command
				VulkanSingleTimeCommand cmd(m_Device, cmdPool);
				VkCommandBuffer commandBuffer = cmd.Begin();

				VkBufferCopy copyRegion = {};
				copyRegion.srcOffset = 0;
				copyRegion.dstOffset = offset;
				copyRegion.size = size;

				vkCmdCopyBuffer(commandBuffer, stagingBuffer->GetBuffer(),
					m_Allocation->buffer, 1, &copyRegion);

				cmd.End();

				return true;
			});

		if (success)
		{
			LOG_TRACE("Uploaded {} bytes using pooled staging buffer", size);
		}

		return success;
	}

	VkBufferUsageFlags VulkanBuffer::GetVulkanUsageFlags(BufferUsage usage)
	{
		switch (usage)
		{
		case BufferUsage::Vertex:
			return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		case BufferUsage::Index:
			return VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		case BufferUsage::Uniform:
			return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		case BufferUsage::Storage:
			return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		case BufferUsage::Staging:
			return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

		case BufferUsage::Indirect:
			return VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		//case BufferUsage::Query:
		//	return VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		default:
			LOG_ERROR("Unknown buffer usage: {}", static_cast<int>(usage));
			return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		}
	}

	VmaMemoryUsage VulkanBuffer::GetVmaMemoryUsage(MemoryAccess access)
	{
		switch (access)
		{
		case MemoryAccess::GpuOnly:
			// Prefer device local memory for best GPU performance
			return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

		case MemoryAccess::CpuToGpu:
			// Prefer host visible for CPU writes
			return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;

		case MemoryAccess::GpuToCpu:
			// Host visible for CPU reads
			return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;

		case MemoryAccess::CpuCached:
			// Host visible and cached for frequent CPU access
			return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;

		default:
			LOG_WARN("Unknown memory access: {}", static_cast<int>(access));
			return VMA_MEMORY_USAGE_AUTO;
		}
	}
}
