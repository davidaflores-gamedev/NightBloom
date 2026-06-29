//------------------------------------------------------------------------------
// GpuProfiler.cpp
//------------------------------------------------------------------------------
#include "Engine/Renderer/Components/GpuProfiler.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Core/Logger/Logger.hpp"

namespace Nightbloom
{
	bool GpuProfiler::Initialize(VulkanDevice* device)
	{
		m_Device = device;

		VkPhysicalDeviceProperties props{};
		vkGetPhysicalDeviceProperties(device->GetPhysicalDevice(), &props);
		m_TimestampPeriod = props.limits.timestampPeriod;

		// Timestamp validity is per queue family. If the graphics queue reports 0
		// valid bits, timestamps aren't usable there — disable profiling gracefully.
		uint32_t familyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device->GetPhysicalDevice(), &familyCount, nullptr);
		std::vector<VkQueueFamilyProperties> families(familyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(device->GetPhysicalDevice(), &familyCount, families.data());

		uint32_t gfxFamily = device->GetGraphicsQueueFamily();
		uint32_t validBits = (gfxFamily < familyCount) ? families[gfxFamily].timestampValidBits : 0;

		if (m_TimestampPeriod <= 0.0f || validBits == 0)
		{
			LOG_WARN("GpuProfiler: timestamp queries unsupported on graphics queue — profiling disabled");
			m_Supported = false;
			return true;  // not fatal
		}

		m_ValidBitsMask = (validBits >= 64) ? ~0ull : ((1ull << validBits) - 1ull);

		VkQueryPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
		poolInfo.queryCount = MAX_SPANS * 2;  // begin + end per span

		for (uint32_t i = 0; i < MAX_FRAMES; ++i)
		{
			if (vkCreateQueryPool(device->GetDevice(), &poolInfo, nullptr, &m_Pools[i]) != VK_SUCCESS)
			{
				LOG_ERROR("GpuProfiler: failed to create query pool {}", i);
				m_Supported = false;
				return false;
			}
		}

		m_Supported = true;
		LOG_INFO("GpuProfiler initialized (timestampPeriod {} ns/tick)", m_TimestampPeriod);
		return true;
	}

	void GpuProfiler::Cleanup()
	{
		if (!m_Device) return;
		for (uint32_t i = 0; i < MAX_FRAMES; ++i)
		{
			if (m_Pools[i] != VK_NULL_HANDLE)
			{
				vkDestroyQueryPool(m_Device->GetDevice(), m_Pools[i], nullptr);
				m_Pools[i] = VK_NULL_HANDLE;
			}
		}
		m_Device = nullptr;
	}

	void GpuProfiler::BeginFrame(VkCommandBuffer cmd, uint32_t frameIndex)
	{
		if (!m_Supported) return;
		m_FrameIndex = frameIndex;
		m_SpanCount = 0;

		// Read back the results this slot recorded the previous time around. The
		// fence for this frame slot was waited on before recording, so they're ready.
		if (m_PoolValid[frameIndex] && m_PoolSpans[frameIndex] > 0)
		{
			uint32_t spans = m_PoolSpans[frameIndex];
			uint32_t queryCount = spans * 2;
			std::array<uint64_t, MAX_SPANS * 2> raw{};

			VkResult r = vkGetQueryPoolResults(
				m_Device->GetDevice(), m_Pools[frameIndex],
				0, queryCount,
				queryCount * sizeof(uint64_t), raw.data(), sizeof(uint64_t),
				VK_QUERY_RESULT_64_BIT);

			if (r == VK_SUCCESS)
			{
				m_Results.clear();
				for (uint32_t s = 0; s < spans; ++s)
				{
					uint64_t begin = raw[s * 2 + 0] & m_ValidBitsMask;
					uint64_t end   = raw[s * 2 + 1] & m_ValidBitsMask;
					float ms = (end > begin)
						? static_cast<float>(end - begin) * m_TimestampPeriod * 1e-6f
						: 0.0f;
					m_Results.push_back({ m_PoolNames[frameIndex][s], ms });
				}
			}
		}

		// Pools must be reset before reuse (timestamp queries can't be overwritten).
		vkCmdResetQueryPool(cmd, m_Pools[frameIndex], 0, MAX_SPANS * 2);
	}

	void GpuProfiler::EndFrame(uint32_t frameIndex)
	{
		if (!m_Supported) return;
		m_PoolValid[frameIndex] = (m_SpanCount > 0);
		m_PoolSpans[frameIndex] = m_SpanCount;
		for (uint32_t s = 0; s < m_SpanCount; ++s)
			m_PoolNames[frameIndex][s] = m_Names[s];
	}

	uint32_t GpuProfiler::BeginScope(VkCommandBuffer cmd, const char* name)
	{
		if (!m_Supported || m_SpanCount >= MAX_SPANS) return UINT32_MAX;
		uint32_t idx = m_SpanCount++;
		m_Names[idx] = name;
		vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_Pools[m_FrameIndex], idx * 2);
		return idx;
	}

	void GpuProfiler::EndScope(VkCommandBuffer cmd, uint32_t scope)
	{
		if (!m_Supported || scope == UINT32_MAX) return;
		vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_Pools[m_FrameIndex], scope * 2 + 1);
	}
} // namespace Nightbloom
