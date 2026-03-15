//------------------------------------------------------------------------------
// Base.hpp
//
// Common includes and definitions for Nightbloom Engine
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------
#pragma once

// Disable warnings for third-party headers
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996) // Disable deprecated warnings
#pragma warning(disable: 4251) // Disable DLL interface warnings
#endif

// Platform detection - verify CMake defined the platform
#if !defined(NIGHTBLOOM_PLATFORM_WINDOWS) && !defined(NIGHTBLOOM_PLATFORM_LINUX) && !defined(NIGHTBLOOM_PLATFORM_MACOS)
#error "No platform defined! Check CMakeLists.txt"
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

#ifdef _MSC_VER
#pragma warning(pop)
#endif