//------------------------------------------------------------------------------
// Assert.hpp
//
// File for assertion handling in the Nightbloom Engine
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#pragma once

#define UNUSED(x) (void)(x)

#include <format>
#include <string>
#include <string_view>
#include <functional>


namespace Nightbloom
{
	namespace Debug
	{
		//Internal assert Handler - called when assertions faile
		void HandleAssertFailure(
			const char* condition,
			const char* message,
			const char* file,
			int line,
			const char* function
		);

		//Template helper for formatting assert messages
		template<typename... Args>
		std::string FormatAssertMessage(std::string_view format, Args&&... args)
		{
			if constexpr (sizeof...(args) == 0) 
			{
				return std::string(format);
			}
			else 
			{
				return std::format(format, std::forward<Args>(args)...);
			}
		}
	}
}

//Platform-specific debugger break
#ifdef _WIN32
#define NIGHTBLOOM_DEBUG_BREAK() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
#include <signal.h>
#define NIGHTBLOOM_DEBUG_BREAK() raise(SIGTRAP)
#else
#define NIGHTBLOOM_DEBUG_BREAK() ((void)0)
#endif

// Internal macro to handle assert logic
#define NIGHTBLOOM_ASSERT_IMPL(condition, message, ...) \
    do { \
        if (!(condition)) { \
            std::string formatted_msg = ::Nightbloom::Debug::FormatAssertMessage(message, ##__VA_ARGS__); \
            ::Nightbloom::Debug::HandleAssertFailure( \
                #condition, \
                formatted_msg.c_str(), \
                __FILE__, \
                __LINE__, \
                __FUNCTION__ \
            ); \
        } \
    } while (0)

// Debug build configuration check
#if defined(DEBUG) || defined(_DEBUG) || !defined(NDEBUG)
#define NIGHTBLOOM_DEBUG_BUILD 1
#else
#define NIGHTBLOOM_DEBUG_BUILD 0
#endif

// ============================================================================
// PUBLIC API MACROS
// ============================================================================

// ASSERT - Debug-only assertion that completely disappears in release builds
#if NIGHTBLOOM_DEBUG_BUILD
#define ASSERT(condition, message, ...) \
        NIGHTBLOOM_ASSERT_IMPL(condition, message, ##__VA_ARGS__)
#else
#define ASSERT(condition, message, ...) ((void)0)
#endif

// VERIFY - Always evaluates condition, but only shows assert dialog in debug
#if NIGHTBLOOM_DEBUG_BUILD
#define VERIFY(condition, message, ...) \
        NIGHTBLOOM_ASSERT_IMPL(condition, message, ##__VA_ARGS__)
#else
#define VERIFY(condition, message, ...) \
        do { \
            (void)(condition); \
        } while (0)
#endif

// ASSERT_NOT_REACHED - Marks code paths that should never execute
#if NIGHTBLOOM_DEBUG_BUILD
#define ASSERT_NOT_REACHED(message, ...) \
        do { \
            std::string formatted_msg = ::Nightbloom::Debug::FormatAssertMessage(message, ##__VA_ARGS__); \
            ::Nightbloom::Debug::HandleAssertFailure( \
                "false", \
                formatted_msg.c_str(), \
                __FILE__, \
                __LINE__, \
                __FUNCTION__ \
            ); \
        } while (0)
#else
#define ASSERT_NOT_REACHED(message, ...) ((void)0)
#endif

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================

// Common assertion patterns
#define ASSERT_NOT_NULL(ptr, message, ...) \
    ASSERT((ptr) != nullptr, message, ##__VA_ARGS__)

#define ASSERT_VALID_INDEX(index, size, message, ...) \
    ASSERT((index) >= 0 && (index) < (size), message, ##__VA_ARGS__)

#define ASSERT_RANGE(value, min, max, message, ...) \
    ASSERT((value) >= (min) && (value) <= (max), message, ##__VA_ARGS__)

// Static assert for compile-time checks
#define STATIC_ASSERT(condition, message) \
    static_assert(condition, message)

// ============================================================================
// OPTIONAL: SCOPED ASSERTIONS
// ============================================================================

#if NIGHTBLOOM_DEBUG_BUILD
namespace Nightbloom {
	namespace Debug {
		// RAII helper for asserting conditions over a scope
		class ScopedAssertion {
		public:
			template<typename Condition, typename... Args>
			ScopedAssertion(Condition&& condition, std::string_view message, Args&&... args)
				: m_condition([condition = std::forward<Condition>(condition)]() { return condition(); })
				, m_message(FormatAssertMessage(message, std::forward<Args>(args)...))
				, m_file(__builtin_FILE())
				, m_line(__builtin_LINE())
				, m_function(__builtin_FUNCTION())
			{
				// Check condition on construction
				if (!m_condition()) {
					HandleAssertFailure("scoped_condition", m_message.c_str(), m_file, m_line, m_function);
				}
			}

			~ScopedAssertion() {
				// Check condition on destruction
				if (!m_condition()) {
					HandleAssertFailure("scoped_condition", m_message.c_str(), m_file, m_line, m_function);
				}
			}

		private:
			std::function<bool()> m_condition;
			std::string m_message;
			const char* m_file;
			int m_line;
			const char* m_function;
		};
	}
}

#define ASSERT_SCOPED(condition, message, ...) \
    ::Nightbloom::Debug::ScopedAssertion NIGHTBLOOM_CONCAT(_scoped_assert_, __LINE__)( \
        [&]() { return (condition); }, message, ##__VA_ARGS__)

#define NIGHTBLOOM_CONCAT_IMPL(a, b) a##b
#define NIGHTBLOOM_CONCAT(a, b) NIGHTBLOOM_CONCAT_IMPL(a, b)
#else
#define ASSERT_SCOPED(condition, message, ...) ((void)0)
#endif