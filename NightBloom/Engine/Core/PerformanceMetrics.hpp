//------------------------------------------------------------------------------
// PerformanceMetrics.hpp
//
// Performance tracking and frame timing
//------------------------------------------------------------------------------

#pragma once

#include <chrono>
#include <deque>
#include <string>

namespace Nightbloom
{
	class PerformanceMetrics
	{
	public:
		static PerformanceMetrics& Get()
		{
			static PerformanceMetrics instance;
			return instance;
		}

		// Frame timing
		void BeginFrame();
		void EndFrame();

		// Get metrics
		float GetFPS() const { return m_CurrentFPS; }
		float GetFrameTime() const { return m_CurrentFrameTime; }
		float GetFrameTimeVariance() const { return m_FrameTimeVariance; }
		float GetAverageFrameTime() const { return m_AverageFrameTime; }
		float GetMinFrameTime() const { return m_MinFrameTime; }
		float GetMaxFrameTime() const { return m_MaxFrameTime; }

		// Get detailed timing for different stages
		void BeginGPUWork();
		void EndGPUWork();
		float GetGPUTime() const { return m_GPUTime; }

		// Memory tracking (integrates with VMA)
		void UpdateMemoryStats(size_t allocated, size_t used);
		size_t GetMemoryAllocated() const { return m_MemoryAllocated; }
		size_t GetMemoryUsed() const { return m_MemoryUsed; }

		// Generate report string
		std::string GetReport() const;
		void LogMetrics() const;

		// Reset statistics
		void Reset();

	private:
		PerformanceMetrics() = default;
		~PerformanceMetrics() = default;

		void UpdateStats();

	private:
		// Timing
		using Clock = std::chrono::high_resolution_clock;
		using TimePoint = std::chrono::time_point<Clock>;

		TimePoint m_FrameStartTime;
		TimePoint m_GPUStartTime;

		// Frame time history (for variance calculation)
		static constexpr size_t HISTORY_SIZE = 100;
		std::deque<float> m_FrameTimeHistory;

		// Current metrics
		float m_CurrentFPS = 0.0f;
		float m_CurrentFrameTime = 0.0f;
		float m_AverageFrameTime = 0.0f;
		float m_MinFrameTime = 999999.0f;
		float m_MaxFrameTime = 0.0f;
		float m_FrameTimeVariance = 0.0f;
		float m_GPUTime = 0.0f;

		// Memory
		size_t m_MemoryAllocated = 0;
		size_t m_MemoryUsed = 0;

		// Frame counter
		uint64_t m_FrameCount = 0;

		// Update frequency
		static constexpr float UPDATE_INTERVAL = 0.5f; // Update display every 0.5 seconds
		float m_TimeSinceUpdate = 0.0f;
		int m_FramesSinceUpdate = 0;
	};
}
