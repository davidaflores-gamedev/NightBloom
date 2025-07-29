//------------------------------------------------------------------------------
// Logger.hpp
//
// Core logging system for NightBloom Engine
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#pragma once

#include <string>
#include <memory>
#include <vector>
#include <mutex>
#include <format>

namespace Nightbloom
{
	enum class LogLevel
	{
		Trace,
		Debug,
		Info,
		Warn,
		Error,
		//Critical,
		None
	};

	class ILogSink;

	// a singleton logger class that will handle logging messages
	class Logger
	{
	public:
		
		// singleton instance access
		static Logger& Get();

		// config
		void SetLogLevel(LogLevel level);
		void AddSink(std::shared_ptr<ILogSink> sink);
		void ClearSinks();

		// Core Logging Function
		void Log(LogLevel level, const std::string& message);

		//Formatted logging 
		template<typename... Args>
		void LogFormatted(LogLevel level, const std::string& format, Args&&... args)
		{
			if (level < m_MinLogLevel)
				return;

			std::string message = std::vformat(format, std::make_format_args(std::forward<Args>(args)...));
			Log(level, message);
		}

		//template<typename... Args>
		//void LogInfo

	private:
		Logger();
		~Logger() = default;
		Logger(const Logger&) = delete;
		Logger& operator=(const Logger&) = delete;

		LogLevel m_MinLogLevel = LogLevel::Trace;
		std::vector<std::shared_ptr<ILogSink>> m_Sinks;
		std::mutex m_Mutex;
	};

	// sink interface for logging
	class ILogSink
	{
	public:
		virtual ~ILogSink() = default;
		virtual void Write(LogLevel level, const std::string& message) = 0;
	};
}

// Convenience macros for logging
#define LOG_TRACE(...) ::Nightbloom::Logger::Get().LogFormatted(::Nightbloom::LogLevel::Trace, __VA_ARGS__)
#define LOG_DEBUG(...) ::Nightbloom::Logger::Get().LogFormatted(::Nightbloom::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...)  ::Nightbloom::Logger::Get().LogFormatted(::Nightbloom::LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...)  ::Nightbloom::Logger::Get().LogFormatted(::Nightbloom::LogLevel::Warn, __VA_ARGS__)
#define LOG_ERROR(...) ::Nightbloom::Logger::Get().LogFormatted(::Nightbloom::LogLevel::Error, __VA_ARGS__)

// Overloads for simple strings (no formatting)
//#define LOG_INFO_S(...) ::Nightbloom::Logger::Get().LogInfo(msg)