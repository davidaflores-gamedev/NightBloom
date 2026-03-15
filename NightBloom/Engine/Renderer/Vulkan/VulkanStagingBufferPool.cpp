//------------------------------------------------------------------------------
// StagingBufferPool.cpp
//
// Implementation of staging buffer pooling
//------------------------------------------------------------------------------

#include "Engine/Renderer/Vulkan/VulkanStagingBufferPool.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanMemoryManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <algorithm>

namespace Nightbloom
{
	StagingBufferPool::StagingBufferPool(VulkanDevice* device, VulkanMemoryManager* memoryManager)
		: m_Device(device), m_MemoryManager(memoryManager)
	{
		LOG_INFO("Created staging buffer pool");
	}

	StagingBufferPool::~StagingBufferPool()
	{
		// Log pool statistics
		size_t totalSize = 0;
		size_t inUseCount = 0;
		for (const auto& entry : m_Pool)
		{
			totalSize += entry.size;
			if (entry.inUse) inUseCount++;
		}

		LOG_INFO("Destroying staging buffer pool: {} buffers ({} in use), {:.2f} MB total",
			m_Pool.size(), inUseCount, totalSize / (1024.0 * 1024.0));

		// Buffers will be automatically destroyed
	}

	void StagingBufferPool::Cleanup() {
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		m_Pool.clear();  // unique_ptr<VulkanBuffer> destructors free allocations
		LOG_INFO("Staging buffer pool cleared");
	}

	VulkanBuffer* StagingBufferPool::Acquire(size_t size)
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);

		// Round up to minimum size
		size = std::max(size, MIN_BUFFER_SIZE);

		// First, try to find an existing buffer that's large enough and not in use
		for (auto& entry : m_Pool)
		{
			if (!entry.inUse && entry.size >= size)
			{
				entry.inUse = true;
				entry.lastUsed = std::chrono::steady_clock::now();
				LOG_TRACE("Reusing staging buffer of size {} for request of {}",
					entry.size, size);
				return entry.buffer.get();
			}
		}

		// If pool is full, try garbage collection first
		if (m_Pool.size() >= MAX_POOL_SIZE)
		{
			// Remove old unused buffers
			auto now = std::chrono::steady_clock::now();
			m_Pool.erase(
				std::remove_if(m_Pool.begin(), m_Pool.end(),
					[now](const PoolEntry& entry) {
						return !entry.inUse &&
							(now - entry.lastUsed) > MAX_AGE;
					}),
				m_Pool.end()
			);
		}

		// If still full, find the smallest unused buffer to replace
		if (m_Pool.size() >= MAX_POOL_SIZE)
		{
			auto smallestIt = std::min_element(m_Pool.begin(), m_Pool.end(),
				[](const PoolEntry& a, const PoolEntry& b) {
					if (a.inUse != b.inUse) return b.inUse; // Prefer unused
					return a.size < b.size;
				});

			if (!smallestIt->inUse && smallestIt->size < size)
			{
				// Remove the smallest buffer to make room
				m_Pool.erase(smallestIt);
				LOG_TRACE("Evicted small staging buffer to make room");
			}
			else if (m_Pool.size() >= MAX_POOL_SIZE)
			{
				LOG_WARN("Staging buffer pool exhausted - all buffers in use");
				return nullptr;
			}
		}

		// Create a new buffer
		auto buffer = std::make_unique<VulkanBuffer>(m_Device, m_MemoryManager);

		BufferDesc desc;
		desc.usage = BufferUsage::Staging;
		desc.memoryAccess = MemoryAccess::CpuToGpu;
		desc.size = size;
		desc.debugName = "PooledStaging_" + std::to_string(m_Pool.size());

		if (!buffer->Initialize(desc))
		{
			LOG_ERROR("Failed to create staging buffer of size {}", size);
			return nullptr;
		}

		PoolEntry entry;
		entry.buffer = std::move(buffer);
		entry.size = size;
		entry.inUse = true;
		entry.lastUsed = std::chrono::steady_clock::now();

		m_Pool.push_back(std::move(entry));
		LOG_TRACE("Created new staging buffer of size {}", size);

		return m_Pool.back().buffer.get();
	}

	void StagingBufferPool::Release(VulkanBuffer* buffer)
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);

		for (auto& entry : m_Pool)
		{
			if (entry.buffer.get() == buffer)
			{
				entry.inUse = false;
				entry.lastUsed = std::chrono::steady_clock::now();
				LOG_TRACE("Released staging buffer back to pool");
				return;
			}
		}

		LOG_WARN("Attempted to release unknown staging buffer");
	}

	void StagingBufferPool::GarbageCollect()
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);

		auto now = std::chrono::steady_clock::now();
		size_t beforeCount = m_Pool.size();

		m_Pool.erase(
			std::remove_if(m_Pool.begin(), m_Pool.end(),
				[now](const PoolEntry& entry) {
					return !entry.inUse &&
						(now - entry.lastUsed) > MAX_AGE;
				}),
			m_Pool.end()
		);

		size_t removed = beforeCount - m_Pool.size();
		if (removed > 0)
		{
			LOG_INFO("Garbage collected {} old staging buffers", removed);
		}
	}
}