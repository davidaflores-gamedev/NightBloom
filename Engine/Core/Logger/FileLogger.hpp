//------------------------------------------------------------------------------
// FileLogger.hpp
//
// File output sink for logging system
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#pragma once

#include "Logger.hpp"
#include <fstream>

namespace Nightbloom
{
	class FileLogger : public ILogSink
	{
	public:
		FileLogger(const std::string& filename);
		virtual ~FileLogger();

		// ILogSink interface implementation
		virtual void Write(LogLevel level, const std::string& message) override;

		bool IsOpen() const { return m_File.is_open(); }

	private:
		std::ofstream m_File;
		std::mutex m_FileMutex;
	};
}