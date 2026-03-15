//------------------------------------------------------------------------------
// Logger.cpp
//
// Core logging system implementation
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#include "Core/Logger/Logger.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>

namespace Nightbloom
{
	Logger& Logger::Get()
	{
		static Logger instance;
		return instance;
	}

	Logger::Logger()
	{
		// Initialize with default log level
		// Application must add sinks themselves
		// m_MinLogLevel = LogLevel::Trace
	}

	void Logger::SetLogLevel(LogLevel level)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_MinLogLevel = level;
	}

	void Logger::AddSink(std::shared_ptr<ILogSink> sink)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Sinks.push_back(sink);
	}

	void Logger::ClearSinks()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Sinks.clear();
	}

	void Logger::Log(LogLevel level, const std::string& message)
	{
		if (level < m_MinLogLevel)
			return;

		// Get timestamp
		auto now = std::chrono::system_clock::now();
		auto time_t = std::chrono::system_clock::to_time_t(now);
		
		std::stringstream ss;
		ss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
		std::string timestamp = ss.str();

		std::string levelStr;
		switch (level)
		{
		case LogLevel::Trace: levelStr = "TRACE"; break;
		case LogLevel::Debug: levelStr = "DEBUG"; break;
		case LogLevel::Info: levelStr = "INFO"; break;
		case LogLevel::Warn: levelStr = "WARN"; break;
		case LogLevel::Error: levelStr = "ERROR"; break;
		default: levelStr = "UNKNOWN"; break;
		}

		std::string fullMessage = std::format("[{}] [{}] {}", timestamp, levelStr, message);

		// Send to all sinks
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (const auto& sink : m_Sinks)
		{
			sink->Write(level, fullMessage);
		}
	}
}