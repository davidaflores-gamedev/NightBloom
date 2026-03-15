//------------------------------------------------------------------------------
// ConsoleLogger.hpp
//
// Console output sink for logging system
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#pragma once

#include "Logger.hpp"

namespace Nightbloom
{
	class ConsoleLogger : public ILogSink
	{
	public:
		ConsoleLogger(bool useColors = true);
		virtual ~ConsoleLogger() = default;

		// ILogSink interface implementation
		virtual void Write(LogLevel level, const std::string& message) override;

	private:
		bool m_UseColors;

		//Platform-specific color handling
		void SetConsoleColor(LogLevel level);
		void ResetConsoleColor();
	};
}