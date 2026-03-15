//------------------------------------------------------------------------------
// AssetManager.hpp
//
// Handles asset path resolution and loading
//------------------------------------------------------------------------------

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace Nightbloom
{
	class AssetManager
	{
	public:
		static AssetManager& Get()
		{
			static AssetManager instance;
			return instance;
		}

		// Initialize with base paths
		bool Initialize(const std::string& executablePath);
		void Shutdown();

		// Path resolution
		std::string GetShaderPath(const std::string& shaderName) const;
		std::string GetTexturePath(const std::string& textureName) const;
		std::string GetModelPath(const std::string& modelName) const;
		std::string GetAssetPath(const std::string& relativePath) const;

		// Shader loading helpers
		std::vector<char> LoadShaderBinary(const std::string& shaderName) const;

		// Check if paths exist
		bool ValidateAssetPaths() const;

		// Get root directories
		const std::string& GetRootPath() const { return m_RootPath; }
		const std::string& GetShadersPath() const { return m_ShadersPath; }
		const std::string& GetTexturesPath() const { return m_TexturesPath; }
		const std::string& GetModelsPath() const { return m_ModelsPath; }

	private:
		AssetManager() = default;
		~AssetManager() = default;

		// Delete copy/move
		AssetManager(const AssetManager&) = delete;
		AssetManager& operator=(const AssetManager&) = delete;

		bool FindProjectRoot(const std::string& executablePath);

	private:
		std::string m_RootPath;
		std::string m_AssetsPath;
		std::string m_ShadersPath;
		std::string m_TexturesPath;
		std::string m_ModelsPath;
		bool m_Initialized = false;
	};
}