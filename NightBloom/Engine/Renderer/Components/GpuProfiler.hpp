//------------------------------------------------------------------------------
// GpuProfiler.hpp
//
// Lightweight GPU timing via Vulkan timestamp queries. Each "scope" writes a
// TOP_OF_PIPE timestamp on begin and a BOTTOM_OF_PIPE timestamp on end; the
// delta * timestampPeriod gives the GPU wall time for that span.
//
// Double-buffered by frame-in-flight: results are read back when a frame slot
// comes around again (the fence for that slot has already been waited on in
// Renderer::BeginFrame, so the timestamps are guaranteed available — no stall).
// Results are therefore one full frame-in-flight cycle old, which is fine for
// an on-screen overlay.
//
// Usage per frame (inside the recorded command buffer):
//   profiler.BeginFrame(cmd, frameIndex);          // reset + read previous results
//   uint32_t s = profiler.BeginScope(cmd, "Shadow");
//   ... record the pass ...
//   profiler.EndScope(cmd, s);
//   profiler.EndFrame(frameIndex);                 // latch span count/names
//------------------------------------------------------------------------------
#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <string>
#include <vector>
#include <cstdint>

namespace Nightbloom
{
	class VulkanDevice;

	class GpuProfiler
	{
	public:
		static constexpr uint32_t MAX_FRAMES = 2;
		static constexpr uint32_t MAX_SPANS  = 16;  // per frame

		bool Initialize(VulkanDevice* device);
		void Cleanup();

		// Start of command-buffer recording: read back this slot's previous results
		// and reset its query pool. Must be called inside an open command buffer.
		void BeginFrame(VkCommandBuffer cmd, uint32_t frameIndex);
		// End of command-buffer recording: latch how many spans / which names were used.
		void EndFrame(uint32_t frameIndex);

		// Returns a scope handle (or UINT32_MAX if the per-frame span budget is full /
		// profiling unsupported). EndScope is a no-op for an invalid handle.
		uint32_t BeginScope(VkCommandBuffer cmd, const char* name);
		void EndScope(VkCommandBuffer cmd, uint32_t scope);

		struct Result { std::string name; float ms; };
		const std::vector<Result>& GetResults() const { return m_Results; }
		bool IsSupported() const { return m_Supported; }

	private:
		VulkanDevice* m_Device = nullptr;
		bool     m_Supported = false;
		float    m_TimestampPeriod = 1.0f;       // nanoseconds per tick
		uint64_t m_ValidBitsMask = ~0ull;

		VkQueryPool m_Pools[MAX_FRAMES] = {};

		uint32_t m_FrameIndex = 0;               // slot currently being recorded
		uint32_t m_SpanCount = 0;                // spans recorded so far this frame
		std::array<std::string, MAX_SPANS> m_Names{};

		bool     m_PoolValid[MAX_FRAMES] = {};   // slot holds results worth reading
		uint32_t m_PoolSpans[MAX_FRAMES] = {};
		std::array<std::array<std::string, MAX_SPANS>, MAX_FRAMES> m_PoolNames{};

		std::vector<Result> m_Results;

		GpuProfiler(const GpuProfiler&) = delete;
		GpuProfiler& operator=(const GpuProfiler&) = delete;
	public:
		GpuProfiler() = default;
		~GpuProfiler() = default;
	};
} // namespace Nightbloom
