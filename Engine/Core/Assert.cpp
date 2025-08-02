//------------------------------------------------------------------------------
// Assert.cpp
//
// File for assertion handling in the Nightbloom Engine
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#include "Assert.hpp"

#include <iostream>
#include <cstdlib>

// Platform-specific includes
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <crtdbg.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#endif

// Optional: Include your engine's logger if available
// #include "Logger.hpp"

namespace Nightbloom {
	namespace Debug {

		namespace {
			// Configuration flags
			bool g_showAssertDialog = true;
			bool g_breakOnAssert = true;
			bool g_logAsserts = true;

			// Assert statistics (useful for debugging)
			struct AssertStats {
				int totalAsserts = 0;
				int uniqueAsserts = 0;
			};
			AssertStats g_assertStats;

			// Platform-specific dialog implementation
#ifdef _WIN32
			bool ShowAssertDialog(const char* condition, const char* message, const char* file, int line, const char* function) {
				char buffer[4096];
				snprintf(buffer, sizeof(buffer),
					"Assertion Failed!\n\n"
					"Condition: %s\n"
					"Message: %s\n\n"
					"File: %s\n"
					"Line: %d\n"
					"Function: %s\n\n"
					"Press Retry to break into debugger\n"
					"Press Ignore to continue (may cause instability)\n"
					"Press Abort to terminate the application",
					condition, message, file, line, function);

				int result = MessageBoxA(nullptr, buffer, "Debug Assertion Failed",
					MB_ABORTRETRYIGNORE | MB_ICONERROR | MB_DEFBUTTON1);

				switch (result) {
				case IDABORT:
					std::exit(EXIT_FAILURE);
					break;
				case IDRETRY:
					return true; // Break into debugger
				case IDIGNORE:
					return false; // Continue execution
				default:
					return true;
				}
			}
#else
			bool ShowAssertDialog(const char* condition, const char* message, const char* file, int line, const char* function) {
				// On Unix-like systems, print to stderr and ask for input
				std::cerr << "\n" << std::string(60, '=') << "\n";
				std::cerr << "ASSERTION FAILED\n";
				std::cerr << std::string(60, '=') << "\n";
				std::cerr << "Condition: " << condition << "\n";
				std::cerr << "Message: " << message << "\n";
				std::cerr << "File: " << file << ":" << line << "\n";
				std::cerr << "Function: " << function << "\n";
				std::cerr << std::string(60, '=') << "\n";
				std::cerr << "Options:\n";
				std::cerr << "  [a]bort - Terminate the application\n";
				std::cerr << "  [b]reak - Break into debugger (if attached)\n";
				std::cerr << "  [i]gnore - Continue execution (dangerous)\n";
				std::cerr << "Choice: ";

				char choice;
				std::cin >> choice;

				switch (choice) {
				case 'a': case 'A':
					std::exit(EXIT_FAILURE);
					break;
				case 'b': case 'B':
					return true;
				case 'i': case 'I':
					return false;
				default:
					return true;
				}
			}
#endif

			// Check if debugger is attached
			bool IsDebuggerAttached() {
#ifdef _WIN32
				return IsDebuggerPresent() != 0;
#elif defined(__linux__)
				// Check if we're being traced (crude debugger detection)
				char buf[4096];
				FILE* file = fopen("/proc/self/status", "r");
				if (!file) return false;

				while (fgets(buf, sizeof(buf), file)) {
					if (strncmp(buf, "TracerPid:", 10) == 0) {
						int pid = atoi(buf + 10);
						fclose(file);
						return pid != 0;
					}
				}
				fclose(file);
				return false;
#elif defined(__APPLE__)
				// macOS debugger detection
				int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
				struct kinfo_proc info = {};
				size_t size = sizeof(info);

				if (sysctl(mib, 4, &info, &size, nullptr, 0) == 0) {
					return (info.kp_proc.p_flag & P_TRACED) != 0;
				}
				return false;
#else
				return false;
#endif
			}

			// Log assert to console/file
			void LogAssert(const char* condition, const char* message, const char* file, int line, const char* function) {
				// If you have a logger system, use it here instead
				std::cerr << "[ASSERT] " << file << ":" << line << " in " << function
					<< " - Condition '" << condition << "' failed: " << message << std::endl;

				// Optional: Write to log file
				// if (auto logger = Logger::GetInstance()) {
				//     logger->Error("ASSERT FAILED: {} at {}:{} in {} - {}", 
				//                   condition, file, line, function, message);
				// }
			}
		}

		// Configuration functions
		void SetShowAssertDialog(bool show) {
			g_showAssertDialog = show;
		}

		void SetBreakOnAssert(bool breakOnAssert) {
			g_breakOnAssert = breakOnAssert;
		}

		void SetLogAsserts(bool log) {
			g_logAsserts = log;
		}

		// Get assert statistics
		int GetTotalAssertCount() {
			return g_assertStats.totalAsserts;
		}

		int GetUniqueAssertCount() {
			return g_assertStats.uniqueAsserts;
		}

		// Main assert handler
		void HandleAssertFailure(
			const char* condition,
			const char* message,
			const char* file,
			int line,
			const char* function)
		{
			// Update statistics
			g_assertStats.totalAsserts++;

			// Log the assertion
			if (g_logAsserts) {
				LogAssert(condition, message, file, line, function);
			}

			// Show dialog if enabled
			bool shouldBreak = false;
			if (g_showAssertDialog) {
				shouldBreak = ShowAssertDialog(condition, message, file, line, function);
			}
			else {
				shouldBreak = g_breakOnAssert;
			}

			// Break into debugger if requested and available
			if (shouldBreak) {
				if (IsDebuggerAttached()) {
					NIGHTBLOOM_DEBUG_BREAK();
				}
				else {
					// No debugger attached, just abort
					std::cerr << "No debugger attached. Terminating application.\n";
					std::abort();
				}
			}
		}

		// Alternative assert handler for custom behavior
		void HandleAssertFailureCustom(
			const char* condition,
			const char* message,
			const char* file,
			int line,
			const char* function,
			std::function<void()> customHandler)
		{
			// Update statistics
			g_assertStats.totalAsserts++;

			// Log the assertion
			if (g_logAsserts) {
				LogAssert(condition, message, file, line, function);
			}

			// Call custom handler
			if (customHandler) {
				customHandler();
			}

			// Default behavior
			HandleAssertFailure(condition, message, file, line, function);
		}
	}
}

// ============================================================================
// C-style API for integration with C libraries
// ============================================================================

extern "C" {
	void nightbloom_assert_handler(const char* condition, const char* message,
		const char* file, int line, const char* function) {
		Nightbloom::Debug::HandleAssertFailure(condition, message, file, line, function);
	}
}