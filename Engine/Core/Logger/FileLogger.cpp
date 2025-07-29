//------------------------------------------------------------------------------
// FileLogger.cpp
//
// File output implementation
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#include "FileLogger.hpp"

namespace Nightbloom
{
	FileLogger::FileLogger(const std::string& filename)
	{
		m_File.open(filename, std::ios::app);
		if (!m_File.is_open())
		{
			throw std::runtime_error("Failed to open log file: " + filename);
			// trysometime std::cerr << "Failed to open log file: " << filename << std::endl;
		}
	}

	FileLogger::~FileLogger()
	{
		if (m_File.is_open())
		{
			m_File.close();
		}
	}

	void FileLogger::Write(LogLevel level, const std::string& message)
	{
		std::lock_guard<std::mutex> lock(m_FileMutex);

		if (!m_File.is_open())
			return;

		// Get timestamp
		m_File << message << std::endl;
		m_File.flush();
	}
}