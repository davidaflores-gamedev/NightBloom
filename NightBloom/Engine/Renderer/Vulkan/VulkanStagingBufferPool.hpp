//------------------------------------------------------------------------------
// StagingBufferPool.hpp
//
// Efficient pooling of staging buffers to avoid constant allocation
//------------------------------------------------------------------------------
#pragma once

#include <memory>
#include <vector>
#include <mutex>
#include <chrono>

namespace Nightbloom
{
	class VulkanDevice;
	class VulkanMemoryManager;
	class VulkanBuffer;

	class StagingBufferPool
	{
	public:
		StagingBufferPool(VulkanDevice* device, VulkanMemoryManager* memoryManager);
		~StagingBufferPool();


		void Cleanup();
		// Get a staging buffer of at least 'size' bytes
		VulkanBuffer* Acquire(size_t size);

		// Return buffer to pool
		void Release(VulkanBuffer* buffer);

		// Clean up old unused buffers
		void GarbageCollect();

		// Utility for one-shot staging operations
		template<typename Func>
		bool WithStagingBuffer(size_t size, Func&& func)
		{
			VulkanBuffer* staging = Acquire(size);
			if (!staging) return false;

			bool result = func(staging);
			Release(staging);
			return result;
		}

	private:
		struct PoolEntry
		{
			std::unique_ptr<VulkanBuffer> buffer;
			size_t size;
			bool inUse;
			std::chrono::steady_clock::time_point lastUsed;
		};

		VulkanDevice* m_Device;
		VulkanMemoryManager* m_MemoryManager;
		std::vector<PoolEntry> m_Pool;
		std::mutex m_PoolMutex;

		// Pool configuration
		static constexpr size_t MAX_POOL_SIZE = 10;
		static constexpr size_t MIN_BUFFER_SIZE = 65536;  // 64KB minimum
		static constexpr auto MAX_AGE = std::chrono::seconds(30);
	};
}