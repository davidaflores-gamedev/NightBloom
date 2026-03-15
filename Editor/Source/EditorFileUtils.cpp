//------------------------------------------------------------------------------
// EditorFileUtils.cpp
//
// Editor-specific file utilities with shader pipeline support
// Copyright (c) 2024. All rights reserved.
//------------------------------------------------------------------------------

#include "EditorFileUtils.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include "Engine/Renderer/AssetManager.hpp"
#include <fstream>
#include <sstream>
#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {
	Nightbloom::Editor::EditorFileUtils::ProjectContext g_ProjectCtx{};
}

namespace Nightbloom {
namespace Editor {

	void EditorFileUtils::SetProjectContext(const ProjectContext& ctx) { g_ProjectCtx = ctx; }

	const EditorFileUtils::ProjectContext& EditorFileUtils::GetProjectContext() { return g_ProjectCtx; }

	bool EditorFileUtils::SaveTextFile(const std::string& filepath, const std::string& content)
	{
		std::ofstream file(filepath);
		if (file.is_open())
		{
			file << content;
			file.close();
			LOG_INFO("Saved file: {}", filepath);
			return true;
		}

		LOG_ERROR("Failed to save file: {}", filepath);
		return false;
	}

	bool EditorFileUtils::SaveShaderFile(const std::string& filename, const std::string& content)
	{
		// Save to Editor's source shaders directory
		std::string editorShadersDir = GetEditorShadersSourceDirectory();

		// Make sure the directory exists
		if (!MakeDirectory(editorShadersDir))
		{
			LOG_ERROR("Failed to create editor shaders directory: {}", editorShadersDir);
			return false;
		}

		// Build full path for source shader
		std::filesystem::path sourcePath = std::filesystem::path(editorShadersDir) / filename;

		// Save the source file
		if (!SaveTextFile(sourcePath.string(), content))
		{
			return false;
		}

		// Now compile it to SPIR-V
		bool compiled = CompileShader(sourcePath.string());

		if (compiled)
		{
			// Copy the compiled .spv to Sandbox
			CopyCompiledShaderToCurrentProject(filename + ".spv");
		}

		return compiled;
	}

	static const char* StageFlagFor(const std::string& path) {
		auto ends = [](const std::string& s, const char* suf) {
#ifdef _WIN32
			return s.size() >= strlen(suf) &&
				_stricmp(s.c_str() + s.size() - strlen(suf), suf) == 0;
#else
			return s.size() >= strlen(suf) &&
				strcasecmp(s.c_str() + s.size() - strlen(suf), suf) == 0;
#endif
			};
		if (ends(path, ".vert")) return "-fshader-stage=vert";
		if (ends(path, ".frag")) return "-fshader-stage=frag";
		if (ends(path, ".comp")) return "-fshader-stage=comp";
		if (ends(path, ".geom")) return "-fshader-stage=geom";
		if (ends(path, ".tesc")) return "-fshader-stage=tesc";
		if (ends(path, ".tese")) return "-fshader-stage=tese";
		return "";
	}

	static std::string Quote(const std::string& s) {
		if (s.empty()) return "\"\"";
		if (s.front() == '"' && s.back() == '"') return s; // already quoted
		return "\"" + s + "\"";
	}

	static int RunAndCapture(const std::string& exePath,
		const std::vector<std::string>& args,
		std::string& out)
	{
		// Build command with proper quoting
		std::string cmd = Quote(exePath);  // Quote the exe path

		for (const auto& arg : args) {
			cmd += " ";
			// Only quote if the arg contains spaces AND isn't already quoted
			if (arg.find(' ') != std::string::npos &&
				!(arg.front() == '"' && arg.back() == '"')) {
				cmd += Quote(arg);
			}
			else {
				cmd += arg;  // Use as-is
			}
		}
		cmd += " 2>&1"; // capture stderr

#ifdef _WIN32
		FILE* pipe = _popen(cmd.c_str(), "r");
		if (!pipe) return -1;
		char buf[4096];
		while (fgets(buf, sizeof(buf), pipe)) out += buf;
		return _pclose(pipe);
#else
		FILE* pipe = popen(cmd.c_str(), "r");
		if (!pipe) return -1;
		char buf[4096];
		while (fgets(buf, sizeof(buf), pipe)) out += buf;
		return pclose(pipe);
#endif
	}

	bool EditorFileUtils::CompileShader(const std::string& shaderPath)
	{
		// Get the path to glslc compiler
		//std::string glslcPath = FindGlslcCompiler();
		//if (glslcPath.empty())
		//{
		//	LOG_ERROR("Could not find glslc compiler! Please ensure Vulkan SDK is installed.");
		//	return false;
		//}
		//
		//// Get output path for compiled shader
		//std::filesystem::path inputPath(shaderPath);
		//std::string outputDir = GetEditorShadersCompiledDirectory();
		//CreateDirectory(outputDir);
		//
		//std::filesystem::path outputPath = std::filesystem::path(outputDir) / (inputPath.filename().string() + ".spv");
		//
		//// Build compile command
		//std::string command = "\"" + glslcPath + "\" \"" + shaderPath + "\" -o \"" + outputPath.string() + "\"";
		//
		//LOG_INFO("Compiling shader: {}", command);
		//
		//// Execute compilation
		//int result = std::system(command.c_str());
		//
		//if (result == 0)
		//{
		//	LOG_INFO("Successfully compiled shader to: {}", outputPath.string());
		//	return true;
		//}
		//else
		//{
		//	LOG_ERROR("Shader compilation failed with error code: {}", result);
		//	return false;
		//}

		std::string glslcPath = FindGlslcCompiler();
		if (glslcPath.empty() || !std::filesystem::exists(glslcPath)) {
			LOG_ERROR("Could not find glslc compiler! Please ensure Vulkan SDK is installed.");
			return false;
		}

		std::filesystem::path inputPath(shaderPath);
		std::string outputDir = GetEditorShadersCompiledDirectory();
		MakeDirectory(outputDir);

		std::filesystem::path outputPath =
			std::filesystem::path(outputDir) / (inputPath.filename().string() + ".spv");

		// Build args
		std::vector<std::string> args;
		if (const char* st = StageFlagFor(shaderPath); st[0]) args.emplace_back(st);
		//args.emplace_back("--target-env=vulkan1.3"); // tweak if needed
		args.emplace_back("-O");                     // optional
		args.emplace_back(shaderPath);        // quote individual paths too
		args.emplace_back("-o");
		args.emplace_back(outputPath.string());

		// Run and capture output
		std::string compilerOut;
		int rc = RunAndCapture(glslcPath, args, compilerOut);

		if (rc == 0) {
			// Get the shader path from AssetManager - this is where shaders are LOADED from
			std::string assetShaderPath = AssetManager::Get().GetShadersPath();
			std::filesystem::path runtimeShadersPath;

			// If AssetManager has a valid path, use it
			if (!assetShaderPath.empty() && std::filesystem::exists(std::filesystem::path(assetShaderPath).parent_path())) {
				runtimeShadersPath = assetShaderPath;
				LOG_INFO("Using AssetManager shader path: {}", runtimeShadersPath.string());
			}
			else {
				// Fallback: find where the .exe actually is and use that + /Shaders
				// This matches what AssetManager::Initialize does
				char exePathBuf[MAX_PATH];
				GetModuleFileNameA(NULL, exePathBuf, MAX_PATH);
				std::filesystem::path exePath(exePathBuf);
				runtimeShadersPath = exePath.parent_path() / "Shaders";
				LOG_INFO("Using exe-relative shader path: {}", runtimeShadersPath.string());
			}

			std::filesystem::path runtimeDest = runtimeShadersPath / outputPath.filename();

			MakeDirectory(runtimeShadersPath.string());
			std::filesystem::copy_file(outputPath, runtimeDest,
				std::filesystem::copy_options::overwrite_existing);

			LOG_INFO("Deployed to runtime shaders: {}", runtimeDest.string());

			return true;
		}
		else {
			LOG_ERROR("glslc failed (code {}):\n{}", rc, compilerOut);
			return false;
		}
	}

	bool EditorFileUtils::CopyCompiledShaderToCurrentProject(const std::string& compiledShaderName)
	{
		// Souirce
		std::filesystem::path sourcePath = std::filesystem::path(GetEditorShadersCompiledDirectory()) / compiledShaderName;

		// Destination: 
		std::filesystem::path destPath = std::filesystem::path(GetCurrentProjectShadersDirectory()) / compiledShaderName;

		try
		{
			// Create Sandbox shaders directory if it doesn't exist
			MakeDirectory(GetCurrentProjectShadersDirectory());

			// Copy the file
			std::filesystem::copy_file(sourcePath, destPath, std::filesystem::copy_options::overwrite_existing);

			LOG_INFO("Copied compiled shader to Project: {}", destPath.string());

			return true;
		}
		catch (const std::filesystem::filesystem_error& e)
		{
			LOG_ERROR("Failed to copy shader to Project: {}", e.what());
			return false;
		}
	}

	std::string EditorFileUtils::FindGlslcCompiler()
	{
		// Check common locations for glslc
		std::vector<std::string> possiblePaths = {
			// Environment variable
			std::string(std::getenv("VULKAN_SDK") ? std::getenv("VULKAN_SDK") : "") + "/Bin/glslc.exe",
			std::string(std::getenv("VULKAN_SDK") ? std::getenv("VULKAN_SDK") : "") + "/Bin32/glslc.exe",

			// Common installation paths on Windows
			"C:/VulkanSDK/1.4.321.1/Bin/glslc.exe",
			"C:/VulkanSDK/1.3.280.0/Bin/glslc.exe",

			// System PATH
			"glslc.exe",
			"glslc"
		};

		for (const auto& path : possiblePaths)
		{
			if (!path.empty() && std::filesystem::exists(path))
			{
				return path;
			}
		}

		return "";
	}

	bool EditorFileUtils::MakeDirectory(const std::string& path)
	{
		try
		{
			if (!std::filesystem::exists(path))
			{
				std::filesystem::create_directories(path);
				LOG_INFO("Created directory: {}", path);
			}
			return true;
		}
		catch (const std::filesystem::filesystem_error& e)
		{
			LOG_ERROR("Failed to create directory {}: {}", path, e.what());
			return false;
		}
	}

	std::string EditorFileUtils::GetEditorShadersSourceDirectory()
	{
		// Editor/Shaders/Source/
		std::filesystem::path currentPath = std::filesystem::current_path();
		std::filesystem::path shadersPath = currentPath / "Shaders" / "Source";
		return shadersPath.string();
	}

	std::string EditorFileUtils::GetEditorShadersCompiledDirectory()
	{
		// Editor/Shaders/Compiled/
		std::filesystem::path currentPath = std::filesystem::current_path();
		std::filesystem::path shadersPath = currentPath / "Shaders" / "Compiled";
		return shadersPath.string();
	}

	std::string EditorFileUtils::GetCurrentProjectShadersDirectory()
	{
		// If user provided an explicit runtime shaders dir, use it.
		if (!g_ProjectCtx.runtimeShadersOverride.empty())
			return g_ProjectCtx.runtimeShadersOverride.string();

		// Otherwise, compute: <projectRoot>/Build/bin/<config>/Shaders
		std::filesystem::path p = g_ProjectCtx.root / "Build" / "bin" / g_ProjectCtx.config / "Shaders";
		// Create parent folders if needed
		MakeDirectory(p.string());
		return p.string();
	}

	//std::string EditorFileUtils::GetSandboxShadersDirectory()
	//{
	//	// Find Sandbox/Shaders/ directory
	//	std::filesystem::path currentPath = std::filesystem::current_path();
	//
	//	// Navigate from Editor/Build/bin/Debug to Sandbox/Build/bin/Debug/Shaders
	//	std::filesystem::path sandboxShaders = currentPath / "../../../../Sandbox/Build/bin/Debug/Shaders";
	//
	//	if (std::filesystem::exists(sandboxShaders.parent_path()))
	//	{
	//		return sandboxShaders.string();
	//	}
	//
	//	// Try Release path
	//	sandboxShaders = currentPath / "../../../../Sandbox/Build/bin/Release/Shaders";
	//	if (std::filesystem::exists(sandboxShaders.parent_path()))
	//	{
	//		return sandboxShaders.string();
	//	}
	//
	//	// Fallback: just use a local directory
	//	LOG_WARN("Could not find Sandbox shaders directory, using local fallback");
	//	return (currentPath / "SandboxShaders").string();
	//}

	std::string EditorFileUtils::GetShadersDirectory()
	{
		return GetEditorShadersSourceDirectory();
	}

	std::string EditorFileUtils::GetAssetsDirectory()
	{
		std::filesystem::path currentPath = std::filesystem::current_path();
		std::filesystem::path assetsPath = currentPath / "Assets";
		return assetsPath.string();
	}

	bool EditorFileUtils::FileExists(const std::string& filepath)
	{
		return std::filesystem::exists(filepath);
	}

	bool EditorFileUtils::ReadTextFile(const std::string& filepath, std::string& outContent)
	{
		std::ifstream file(filepath);
		if (!file.is_open())
		{
			LOG_ERROR("Failed to open file: {}", filepath);
			return false;
		}

		std::stringstream buffer;
		buffer << file.rdbuf();
		outContent = buffer.str();
		file.close();

		return true;
	}
}
}