#include "Engine/Core/PerformanceMetrics.hpp"
#include "Core/Logger/Logger.hpp"
#include <cmath>
#include <numeric>
#include <sstream>
#include <iomanip>

void Nightbloom::PerformanceMetrics::BeginFrame()
{
    m_FrameStartTime = Clock::now();
}

void Nightbloom::PerformanceMetrics::EndFrame()
{
	auto endTime = Clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_FrameStartTime);
	float frameTimeMs = duration.count() / 1000.0f;

	// Update history
	m_FrameTimeHistory.push_back(frameTimeMs);
	if (m_FrameTimeHistory.size() > HISTORY_SIZE)
	{
		m_FrameTimeHistory.pop_front();
	}

	// Update current metrics
	m_CurrentFrameTime = frameTimeMs;
	m_CurrentFPS = (frameTimeMs > 0) ? 1000.0f / frameTimeMs : 0.0f;

	// Update min/max
	m_MinFrameTime = std::min(m_MinFrameTime, frameTimeMs);
	m_MaxFrameTime = std::max(m_MaxFrameTime, frameTimeMs);

	// Update stats periodically
	m_TimeSinceUpdate += frameTimeMs / 1000.0f;
	m_FramesSinceUpdate++;

	if (m_TimeSinceUpdate >= UPDATE_INTERVAL)
	{
		UpdateStats();
		m_TimeSinceUpdate = 0.0f;
		m_FramesSinceUpdate = 0;
	}

	m_FrameCount++;
}

void Nightbloom::PerformanceMetrics::BeginGPUWork()
{
	m_GPUStartTime = Clock::now();
}

void Nightbloom::PerformanceMetrics::EndGPUWork()
{
	auto endTime = Clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_GPUStartTime);
	m_GPUTime = duration.count() / 1000.0f;
}

void Nightbloom::PerformanceMetrics::UpdateMemoryStats(size_t allocated, size_t used)
{
	m_MemoryAllocated = allocated;
	m_MemoryUsed = used;
}

std::string Nightbloom::PerformanceMetrics::GetReport() const
{
	std::stringstream ss;
	ss << std::fixed << std::setprecision(2);
	ss << "=== Performance Report ===\n";
	ss << "FPS: " << m_CurrentFPS << " (" << m_CurrentFrameTime << "ms)\n";
	ss << "Frame Time: Avg=" << m_AverageFrameTime << "ms, "
		<< "Min=" << m_MinFrameTime << "ms, "
		<< "Max=" << m_MaxFrameTime << "ms\n";
	ss << "Variance: " << m_FrameTimeVariance << "ms\n";
	ss << "GPU Time: " << m_GPUTime << "ms\n";
	ss << "Memory: " << (m_MemoryUsed / (1024.0 * 1024.0)) << "MB / "
		<< (m_MemoryAllocated / (1024.0 * 1024.0)) << "MB\n";
	ss << "Total Frames: " << m_FrameCount;
	return ss.str();
}

void Nightbloom::PerformanceMetrics::LogMetrics() const
{
	LOG_INFO("=== Performance Metrics ===");
	LOG_INFO("  FPS: {:.1f} ({:.2f}ms)", m_CurrentFPS, m_CurrentFrameTime);
	LOG_INFO("  Average: {:.2f}ms (Min: {:.2f}ms, Max: {:.2f}ms)",
		m_AverageFrameTime, m_MinFrameTime, m_MaxFrameTime);
	LOG_INFO("  Frame Variance: {:.2f}ms", m_FrameTimeVariance);
	LOG_INFO("  GPU Time: {:.2f}ms", m_GPUTime);
	LOG_INFO("  Memory: {:.1f}MB / {:.1f}MB",
		m_MemoryUsed / (1024.0 * 1024.0),
		m_MemoryAllocated / (1024.0 * 1024.0));
}

void Nightbloom::PerformanceMetrics::Reset()
{
	m_FrameTimeHistory.clear();
	m_CurrentFPS = 0.0f;
	m_CurrentFrameTime = 0.0f;
	m_AverageFrameTime = 0.0f;
	m_MinFrameTime = 999999.0f;
	m_MaxFrameTime = 0.0f;
	m_FrameTimeVariance = 0.0f;
	m_GPUTime = 0.0f;
	m_MemoryAllocated = 0;
	m_MemoryUsed = 0;
	m_FrameCount = 0;
	m_TimeSinceUpdate = 0.0f;
	m_FramesSinceUpdate = 0;
}

void Nightbloom::PerformanceMetrics::UpdateStats()
{
	if (m_FrameTimeHistory.empty())
		return;

	// Calculate average
	float sum = std::accumulate(m_FrameTimeHistory.begin(), m_FrameTimeHistory.end(), 0.0f);
	m_AverageFrameTime = sum / m_FrameTimeHistory.size();

	// Calculate variance
	float varianceSum = 0.0f;
	for (float frameTime : m_FrameTimeHistory)
	{
		float diff = frameTime - m_AverageFrameTime;
		varianceSum += diff * diff;
	}

	float variance = varianceSum / m_FrameTimeHistory.size();
	m_FrameTimeVariance = std::sqrt(variance);  // Standard deviation
}
