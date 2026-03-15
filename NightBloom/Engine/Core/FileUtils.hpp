// File utils folder

#pragma once

#include <string>
#include <vector>

namespace Nightbloom
{
	// FileUtils.hpp

	class FileUtils
	{
	public:
		// Reads the entire file into a string
		static std::string ReadFile(const std::string& filePath);

		// Reads the entire file into a vector of bytes
		static std::vector<uint8_t> ReadFileAsBytes(const std::string& filePath);

		static std::vector<char> ReadFileAsChars(const std::string& filePath);
	};
}