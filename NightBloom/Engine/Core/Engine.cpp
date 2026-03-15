//------------------------------------------------------------------------------
// Engine.cpp
//
// Dummy source file to make CMake happy
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#include "Engine.hpp"
#include "Base.hpp"
#include "Logger/Logger.hpp"
#include "Logger/ConsoleLogger.hpp"
#include "Logger/FileLogger.hpp"

namespace Nightbloom
{
    // This exists just so CMake has something to compile
    void EngineInit()
    {
		// Initialize the logger
        Logger& logger = Logger::Get();

        // add console output sink with colors
		logger.AddSink(std::make_shared<ConsoleLogger>());
        
		// add file output sink
		logger.AddSink(std::make_shared<FileLogger>("engine.log"));

		// Set default log level
		logger.SetLogLevel(LogLevel::Trace);

		LOG_INFO("Nightbloom Engine initialized successfully!");
		LOG_INFO("Version {}.{}.{}",
			NIGHTBLOOM_VERSION_MAJOR,
			NIGHTBLOOM_VERSION_MINOR,
			NIGHTBLOOM_VERSION_PATCH);

		const char* platformName = nullptr;

#ifdef NIGHTBLOOM_PLATFORM_WINDOWS
		platformName = "Windows";
#elif defined(NIGHTBLOOM_PLATFORM_LINUX)
		platformName = "Linux";
#elif defined(NIGHTBLOOM_PLATFORM_MACOS)
		platformName = "MacOS";
#else
		"Unknown Platform"
#endif

		LOG_INFO("Running on platform: {}", platformName);
    }

    void EngineShutdown()
	{
		LOG_INFO("Shutting down Nightbloom Engine...");
		Logger::Get().ClearSinks();
	}
}