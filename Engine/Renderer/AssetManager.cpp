//------------------------------------------------------------------------------
// AssetManager.cpp
//
// Asset path resolution and loading implementation
//------------------------------------------------------------------------------

#include "Engine/Renderer/AssetManager.hpp"
#include "Core/Logger/Logger.hpp"
#include "Engine/Core/FileUtils.hpp"  
#include <filesystem>
#include <fstream>
#include "AssetManager.hpp"

namespace Nightbloom
{
	bool AssetManager::Initialize(const std::string& executablePath)
	{
		if (m_Initialized)
		{
			LOG_WARN("AssetManager already initialized");
			return true;
		}

		LOG_INFO("Initializing AssetManager with executable path: {}", executablePath);

		// Find project root
		if (!FindProjectRoot(executablePath))
		{
			LOG_ERROR("Failed to find project root directory");
			return false;
		}

		// Set up asset paths
		m_AssetsPath = m_RootPath + "/Assets";
		m_TexturesPath = m_AssetsPath + "/Textures";
		m_ModelsPath = m_AssetsPath + "/Models";

		// For shaders, we need to check multiple possible locations
		std::filesystem::path exePath(executablePath);

		// Priority order for shader locations:
		// 1. Build output directory (compiled SPIR-V files) - where the .exe actually is
		// 2. Build/bin/Debug/Shaders or Build/bin/Release/Shaders
		// 3. Project Shaders directory (source shaders)

		std::vector<std::string> possibleShaderPaths = {
			// Where the executable actually is + /Shaders
			exePath.parent_path().string() + "/Shaders",

			// Standard build output locations
			m_RootPath + "/Build/bin/Debug/Shaders",
			m_RootPath + "/Build/bin/Release/Shaders",

			// Source shader directory (least preferred for .spv files)
			m_RootPath + "/Shaders"
		};

		// Find the first existing shader directory
		bool foundShaderPath = false;
		for (const auto& path : possibleShaderPaths)
		{
			if (std::filesystem::exists(path))
			{
				// Check if it contains .spv files (compiled shaders)
				bool hasCompiledShaders = false;
				if (std::filesystem::is_directory(path))
				{
					for (const auto& entry : std::filesystem::directory_iterator(path))
					{
						if (entry.path().extension() == ".spv")
						{
							hasCompiledShaders = true;
							break;
						}
					}
				}

				if (hasCompiledShaders)
				{
					m_ShadersPath = path;
					foundShaderPath = true;
					LOG_INFO("Using compiled shaders from: {}", m_ShadersPath);
					break;
				}
			}
		}

		// If no compiled shaders found, use the default location where they SHOULD be
		if (!foundShaderPath)
		{
			// Use the executable's directory as the base
			m_ShadersPath = exePath.parent_path().string() + "/Shaders";
			LOG_WARN("No compiled shaders found, using default location: {}", m_ShadersPath);

			// Create the directory if it doesn't exist
			if (!std::filesystem::exists(m_ShadersPath))
			{
				std::filesystem::create_directories(m_ShadersPath);
				LOG_INFO("Created shader directory: {}", m_ShadersPath);
			}
		}

		// Validate asset paths
		if (!ValidateAssetPaths())
		{
			LOG_WARN("Some asset paths do not exist, creating them...");

			// Create directories if they don't exist
			std::filesystem::create_directories(m_AssetsPath);
			std::filesystem::create_directories(m_ShadersPath);
			std::filesystem::create_directories(m_TexturesPath);
			std::filesystem::create_directories(m_ModelsPath);
		}

		LOG_INFO("AssetManager initialized:");
		LOG_INFO("  Root: {}", m_RootPath);
		LOG_INFO("  Assets: {}", m_AssetsPath);
		LOG_INFO("  Shaders: {}", m_ShadersPath);
		LOG_INFO("  Textures: {}", m_TexturesPath);
		LOG_INFO("  Models: {}", m_ModelsPath);

		// List available shaders for debugging
		LOG_INFO("Available compiled shaders:");
		if (std::filesystem::exists(m_ShadersPath))
		{
			for (const auto& entry : std::filesystem::directory_iterator(m_ShadersPath))
			{
				if (entry.path().extension() == ".spv")
				{
					LOG_INFO("  - {}", entry.path().filename().string());
				}
			}
		}

		m_Initialized = true;
		return true;
	}

	void AssetManager::Shutdown()
	{
		if (!m_Initialized)
			return;

		LOG_INFO("Shutting down AssetManager");

		m_RootPath.clear();
		m_AssetsPath.clear();
		m_ShadersPath.clear();
		m_TexturesPath.clear();
		m_ModelsPath.clear();

		m_Initialized = false;
	}

	std::string AssetManager::GetShaderPath(const std::string& shaderName) const
	{
		if (!m_Initialized)
		{
			LOG_ERROR("AssetManager not initialized");
			return "";
		}

		// Add .spv extension if not present (for Vulkan compiled shaders)
		std::string filename = shaderName;
		if (filename.find(".spv") == std::string::npos)
		{
			filename += ".spv";
		}

		return m_ShadersPath + "/" + filename;
	}

	std::string AssetManager::GetTexturePath(const std::string& textureName) const
	{
		if (!m_Initialized)
		{
			LOG_ERROR("AssetManager not initialized");
			return "";
		}

		return m_TexturesPath + "/" + textureName;
	}

	std::string AssetManager::GetModelPath(const std::string& modelName) const
	{
		if (!m_Initialized)
		{
			LOG_ERROR("AssetManager not initialized");
			return "";
		}

		return m_ModelsPath + "/" + modelName;
	}

	std::string AssetManager::GetAssetPath(const std::string& relativePath) const
	{
		if (!m_Initialized)
		{
			LOG_ERROR("AssetManager not initialized");
			return "";
		}

		return m_AssetsPath + "/" + relativePath;
	}

	std::vector<char> AssetManager::LoadShaderBinary(const std::string& shaderName) const
	{
		std::string path = GetShaderPath(shaderName);

		if (path.empty())
		{
			LOG_ERROR("Invalid shader path for: {}", shaderName);
			return {};
		}

		// Check if file exists
		if (!std::filesystem::exists(path))
		{
			LOG_ERROR("Shader file not found: {}", path);
			// Also log where we're looking for debugging
			LOG_ERROR("Full path attempted: {}", std::filesystem::absolute(path).string());
			return {};
		}

		LOG_TRACE("Loading shader from: {}", path);
		return FileUtils::ReadFileAsChars(path);
	}

	bool AssetManager::ValidateAssetPaths() const
	{
		bool allValid = true;

		if (!std::filesystem::exists(m_AssetsPath))
		{
			LOG_WARN("Assets directory does not exist: {}", m_AssetsPath);
			allValid = false;
		}

		if (!std::filesystem::exists(m_ShadersPath))
		{
			LOG_WARN("Shaders directory does not exist: {}", m_ShadersPath);
			allValid = false;
		}

		if (!std::filesystem::exists(m_TexturesPath))
		{
			LOG_WARN("Textures directory does not exist: {}", m_TexturesPath);
			allValid = false;
		}

		if (!std::filesystem::exists(m_ModelsPath))
		{
			LOG_WARN("Models directory does not exist: {}", m_ModelsPath);
			allValid = false;
		}

		return allValid;
	}

	bool AssetManager::FindProjectRoot(const std::string& executablePath)
	{
		std::filesystem::path currentPath(executablePath);

		// Get the directory containing the executable
		if (currentPath.has_parent_path())
		{
			currentPath = currentPath.parent_path();
		}

		// Search up the directory tree for markers that indicate project root
		const std::vector<std::string> rootMarkers = {
			"Assets",           // Assets folder
			"CMakeLists.txt",   // CMake project file
			".git",             // Git repository
			"NightBloom.sln",   // Visual Studio solution
			"README.md"         // Project readme
		};

		// Search up to 5 levels up
		for (int i = 0; i < 5; ++i)
		{
			for (const auto& marker : rootMarkers)
			{
				if (std::filesystem::exists(currentPath / marker))
				{
					m_RootPath = currentPath.string();
					LOG_INFO("Found project root at: {}", m_RootPath);
					return true;
				}
			}

			// Go up one directory
			if (currentPath.has_parent_path())
			{
				currentPath = currentPath.parent_path();
			}
			else
			{
				break;
			}
		}

		// Fallback: use executable directory
		currentPath = std::filesystem::path(executablePath).parent_path();
		m_RootPath = currentPath.string();
		LOG_WARN("Could not find project root markers, using executable directory: {}", m_RootPath);

		return true;
	}
}