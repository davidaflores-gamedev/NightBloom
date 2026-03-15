//------------------------------------------------------------------------------
// EditorFileUtils.hpp
//
// Editor-specific file utilities
// Copyright (c) 2024. All rights reserved.
//------------------------------------------------------------------------------

#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace Nightbloom {
namespace Editor {

	class EditorFileUtils
	{
	public:
		struct ProjectContext
		{
			std::filesystem::path root; // Root directory of the current project
			std::string config = "Debug";
			std::filesystem::path runtimeShadersOverride; // Path to override runtime shaders, if any
		};


		static void SetProjectContext(const ProjectContext& ctx);
		static const ProjectContext& GetProjectContext();
		static void SetEditorContext(const ProjectContext& ctx);
		static const ProjectContext& GetEditorContext();

		// Save text content to a file
		static bool SaveTextFile(const std::string& filepath, const std::string& content);

		// Save shader source and compile it
	// This will:
	// 1. Save the .vert/.frag to Editor/Shaders/Source/
	// 2. Compile it to .spv in Editor/Shaders/Compiled/
	// 3. Copy the .spv to Sandbox/Shaders/
		static bool SaveShaderFile(const std::string& filename, const std::string& content);

		// Compile a shader file to SPIR-V
		static bool CompileShader(const std::string& shaderPath);

		// Copy compiled shader to Sandbox
		static bool CopyCompiledShaderToCurrentProject(const std::string& compiledShaderName);

		// Find the glslc compiler
		static std::string FindGlslcCompiler();

		// Create directory if it doesn't exist
		static bool MakeDirectory(const std::string& path);

		// Get various directories
		static std::string GetEditorShadersSourceDirectory();     // Editor/Shaders/Source/
		static std::string GetEditorShadersCompiledDirectory();   // Editor/Shaders/Compiled/
		static std::string GetRuntimeShadersDirectory();
		static std::string GetCurrentProjectShadersDirectory();   // Project/Shaders/
		static std::string GetShadersDirectory();                 // Alias for GetEditorShadersSourceDirectory
		static std::string GetAssetsDirectory();

		// Check if file exists
		static bool FileExists(const std::string& filepath);

		// Read text file
		static bool ReadTextFile(const std::string& filepath, std::string& outContent);
	};

} // namespace Editor
} // namespace Nightbloom