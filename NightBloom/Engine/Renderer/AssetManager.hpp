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
		const std::string& GetAssetsPath() const { return m_AssetsPath; }

		// --- Portable scene asset paths (M3) ---------------------------------------
		// The active project's Assets/ dir, set when a project is opened. Scene asset
		// paths are resolved against this first, then the global Assets/ dir. Empty in
		// dev where assets live in the shared global Assets/.
		void SetProjectAssetsPath(const std::string& path) { m_ProjectAssetsPath = path; }
		const std::string& GetProjectAssetsPath() const { return m_ProjectAssetsPath; }

		// Convert an absolute asset path to one relative to the assets root (project
		// preferred, else global) for storage in a scene file. Returns the input
		// unchanged if it lies under neither root (kept absolute as a dev fallback).
		std::string ToAssetRelativePath(const std::string& absolutePath) const;

		// Resolve a stored scene asset path (relative or legacy-absolute) to an
		// existing file, trying the project Assets/ then the global Assets/.
		std::string ResolveAssetRelativePath(const std::string& storedPath) const;

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
		std::string m_ProjectAssetsPath;  // active project's Assets/ (M3); empty in dev
		std::string m_ShadersPath;
		std::string m_TexturesPath;
		std::string m_ModelsPath;
		bool m_Initialized = false;
	};
}