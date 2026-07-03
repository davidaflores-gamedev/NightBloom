//------------------------------------------------------------------------------
// Project.hpp
//
// A "project" is the unit the editor opens/switches and the Game runtime boots
// (see .claude/PACKAGING_ROADMAP.md, Milestone 1). On disk it's a folder holding:
//   <root>/<name>.nbproj   -- this settings file (versioned JSON)
//   <root>/Scenes/         -- .nbscene files (SceneSerializer)
//   <root>/Assets/         -- per-project assets (reserved; global Assets still
//                             used in M1 — per-project asset resolution is M3)
//
// Paths stored inside the .nbproj are RELATIVE to the project root so the folder
// is movable; Project resolves them against the root it was loaded from.
//------------------------------------------------------------------------------
#pragma once

#include <string>
#include <filesystem>

namespace Nightbloom
{
	// Serialized project settings (the contents of the .nbproj file).
	struct ProjectSettings
	{
		std::string name        = "Untitled";
		int         version     = 1;
		std::string startScene  = "Scenes/main.nbscene";  // relative to root
		std::string windowTitle = "Nightbloom";
		std::string assetsDir   = "Assets";               // relative to root
		std::string scenesDir   = "Scenes";               // relative to root
	};

	class Project
	{
	public:
		// Load a .nbproj file into 'out' (root/file set from projectFile's location).
		// Returns false on IO/parse error.
		static bool Load(const std::filesystem::path& projectFile, Project& out);

		// Scaffold a new project on disk: creates <parentDir>/<name>/ with a
		// <name>.nbproj, Scenes/ and Assets/ subfolders, then fills 'out'. Does not
		// create a scene file — the caller saves the start scene. Returns false on
		// IO error (e.g. the folder already exists and isn't empty).
		static bool Create(const std::filesystem::path& parentDir,
			const std::string& name, Project& out);

		// Write m_Settings back to the .nbproj file. Requires a prior Load/Create.
		bool Save() const;

		const std::filesystem::path& Root() const { return m_Root; }
		const std::filesystem::path& File() const { return m_ProjectFile; }

		std::filesystem::path ScenesPath() const { return m_Root / m_Settings.scenesDir; }
		std::filesystem::path AssetsPath() const { return m_Root / m_Settings.assetsDir; }
		std::filesystem::path StartScenePath() const { return m_Root / m_Settings.startScene; }

		ProjectSettings&       Settings()       { return m_Settings; }
		const ProjectSettings& Settings() const { return m_Settings; }
		bool IsValid() const { return !m_ProjectFile.empty(); }

	private:
		ProjectSettings       m_Settings;
		std::filesystem::path m_Root;         // directory containing the .nbproj
		std::filesystem::path m_ProjectFile;  // the .nbproj path itself
	};

} // namespace Nightbloom
