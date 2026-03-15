// Platform detection and configuration
//------------------------------------------------------------------------------
#pragma once

// Platform detection
#ifdef _WIN32
#ifdef _WIN64
#define NIGHTBLOOM_PLATFORM_WINDOWS
#else
#error "x86 builds are not supported!"
#endif
#elif defined(__APPLE__) || defined(__MACH__)
#include <TargetConditionals.h>
#if TARGET_IPHONE_SIMULATOR == 1
#error "IOS simulator is not supported!"
#elif TARGET_OS_IPHONE == 1
#define NIGHTBLOOM_PLATFORM_IOS
#error "IOS is not supported!"
#elif TARGET_OS_MAC == 1
#define NIGHTBLOOM_PLATFORM_MACOS
#else
#error "Unknown Apple platform!"
#endif
#elif defined(__ANDROID__)
#define NIGHTBLOOM_PLATFORM_ANDROID
#error "Android is not supported!"
#elif defined(__linux__)
#define NIGHTBLOOM_PLATFORM_LINUX
#else
#error "Unknown platform!"
#endif

// Platform-specific includes
#ifdef NIGHTBLOOM_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX  // Prevent Windows.h from defining min/max macros
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// DLL export/import macros (if you ever need them)
#ifdef NIGHTBLOOM_PLATFORM_WINDOWS
#ifdef NIGHTBLOOM_BUILD_DLL
#define NIGHTBLOOM_API __declspec(dllexport)
#else
#define NIGHTBLOOM_API __declspec(dllimport)
#endif
#else
#define NIGHTBLOOM_API
#endif

// Debugbreak macro
#ifdef NIGHTBLOOM_DEBUG
#ifdef NIGHTBLOOM_PLATFORM_WINDOWS
#define NIGHTBLOOM_DEBUGBREAK() __debugbreak()
#elif defined(NIGHTBLOOM_PLATFORM_LINUX)
#include <signal.h>
#define NIGHTBLOOM_DEBUGBREAK() raise(SIGTRAP)
#else
#define NIGHTBLOOM_DEBUGBREAK()
#endif
#else
#define NIGHTBLOOM_DEBUGBREAK()
#endif