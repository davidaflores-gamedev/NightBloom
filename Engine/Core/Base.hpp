//------------------------------------------------------------------------------
// Base.hpp
//
// Common includes and definitions for Nightbloom Engine
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------
#pragma once

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
#define NIGHTBLOOM_PLATFORM_WINDOWS
#elif defined(__linux__)
#define NIGHTBLOOM_PLATFORM_LINUX  
#elif defined(__APPLE__)
#define NIGHTBLOOM_PLATFORM_MACOS
#else
#error "Unknown platform"
#endif

// Standard includes
#include <cstdint>
#include <cstddef>
#include <cassert>

// Engine version
#define NIGHTBLOOM_VERSION_MAJOR 0
#define NIGHTBLOOM_VERSION_MINOR 1
#define NIGHTBLOOM_VERSION_PATCH 0

namespace Nightbloom
{
	// Type aliases
	using uint8 = std::uint8_t;
	using uint16 = std::uint16_t;
	using uint32 = std::uint32_t;
	using uint64 = std::uint64_t;

	using int8 = std::int8_t;
	using int16 = std::int16_t;
	using int32 = std::int32_t;
	using int64 = std::int64_t;

	// Utility macros
#define NIGHTBLOOM_DISABLE_COPY(ClassName) \
        ClassName(const ClassName&) = delete; \
        ClassName& operator=(const ClassName&) = delete;

#define NIGHTBLOOM_DISABLE_MOVE(ClassName) \
        ClassName(ClassName&&) = delete; \
        ClassName& operator=(ClassName&&) = delete;

#define NIGHTBLOOM_DISABLE_COPY_AND_MOVE(ClassName) \
        NIGHTBLOOM_DISABLE_COPY(ClassName) \
        NIGHTBLOOM_DISABLE_MOVE(ClassName)
}