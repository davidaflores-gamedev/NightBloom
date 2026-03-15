//------------------------------------------------------------------------------
// ConsoleLogger.cpp
//
// Console output implementation with color support
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#include "ConsoleLogger.hpp"
#include <iostream>

#ifdef NIGHTBLOOM_PLATFORM_WINDOWS
	#include <windows.h>
#endif

namespace Nightbloom
{
	ConsoleLogger::ConsoleLogger(bool useColors)
		: m_UseColors(useColors)
	{
	}

	void ConsoleLogger::Write(LogLevel level, const std::string& message)
	{
		if (m_UseColors)
			SetConsoleColor(level);

		std::cout << message << std::endl;

		if (m_UseColors)
			ResetConsoleColor();
	}

	void ConsoleLogger::SetConsoleColor(LogLevel level)
	{
#ifdef NIGHTBLOOM_PLATFORM_WINDOWS
		HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
		WORD color;

		switch (level)
		{
		case LogLevel::Trace:
			color = FOREGROUND_INTENSITY; //Gray
			break;
		case LogLevel::Debug:
			color = FOREGROUND_GREEN | FOREGROUND_BLUE; // Cyan
			break;
		case LogLevel::Info:
			color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; // White
			break;
		case LogLevel::Warn:
			color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; // Yellow
			break;
		case LogLevel::Error:
			color = FOREGROUND_RED | FOREGROUND_INTENSITY; // Red
			break;
// 		case LogLevel::Critical:
// 			color = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY; // Magenta
// 			break;
		default:
			color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
			break;
		}

	SetConsoleTextAttribute(hConsole, color);

#else
		// Non-Windows platforms can use ANSI escape codes or other methods
		switch (level)
		{
		case LogLevel::Trace:
			std::cout << "\033[90m"; // Gray
			break;
		case LogLevel::Debug:
			std::cout << "\033[36m"; // Cyan
			break;
		case LogLevel::Info:
			std::cout << "\033[37m"; // White
			break;
		case LogLevel::Warn:
			std::cout << "\033[33m"; // Yellow
			break;
		case LogLevel::Error:
			std::cout << "\033[91m"; // Red
			break;
		default:
			break;
		}
#endif
	}

	void ConsoleLogger::ResetConsoleColor()
	{
#ifdef NIGHTBLOOM_PLATFORM_WINDOWS
		HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
		SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
#else
		std::cout << "\033[0m"; // Reset color
#endif
	}
}