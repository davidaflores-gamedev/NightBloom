//------------------------------------------------------------------------------
// Project.cpp
//------------------------------------------------------------------------------
#include "Engine/Core/Project.hpp"
#include "Engine/Core/Logger/Logger.hpp"

#include <nlohmann/json.hpp>
#include <fstream>

namespace Nightbloom
{
	using json = nlohmann::json;

	bool Project::Load(const std::filesystem::path& projectFile, Project& out)
	{
		std::ifstream file(projectFile);
		if (!file.is_open())
		{
			LOG_ERROR("Project: cannot open '{}'", projectFile.string());
			return false;
		}

		json j;
		try
		{
			file >> j;
		}
		catch (const std::exception& e)
		{
			LOG_ERROR("Project: parse error in '{}': {}", projectFile.string(), e.what());
			return false;
		}

		ProjectSettings s;
		// value() tolerates missing keys so older/newer .nbproj files still load.
		s.name        = j.value("name", s.name);
		s.version     = j.value("version", s.version);
		s.startScene  = j.value("startScene", s.startScene);
		s.windowTitle = j.value("windowTitle", s.windowTitle);
		s.assetsDir   = j.value("assetsDir", s.assetsDir);
		s.scenesDir   = j.value("scenesDir", s.scenesDir);

		out.m_Settings    = std::move(s);
		out.m_ProjectFile = std::filesystem::absolute(projectFile);
		out.m_Root        = out.m_ProjectFile.parent_path();

		LOG_INFO("Project: loaded '{}' at {}", out.m_Settings.name, out.m_Root.string());
		return true;
	}

	bool Project::Create(const std::filesystem::path& parentDir,
		const std::string& name, Project& out)
	{
		std::error_code ec;
		std::filesystem::path root = parentDir / name;

		if (std::filesystem::exists(root) && !std::filesystem::is_empty(root, ec))
		{
			LOG_ERROR("Project: '{}' already exists and is not empty", root.string());
			return false;
		}

		ProjectSettings s;
		s.name = name;

		std::filesystem::create_directories(root / s.scenesDir, ec);
		std::filesystem::create_directories(root / s.assetsDir, ec);
		if (ec)
		{
			LOG_ERROR("Project: failed to scaffold '{}': {}", root.string(), ec.message());
			return false;
		}

		out.m_Settings    = std::move(s);
		out.m_Root        = std::filesystem::absolute(root);
		out.m_ProjectFile = out.m_Root / (name + ".nbproj");

		if (!out.Save())
			return false;

		LOG_INFO("Project: created '{}' at {}", name, out.m_Root.string());
		return true;
	}

	bool Project::Save() const
	{
		if (m_ProjectFile.empty())
		{
			LOG_ERROR("Project::Save called before Load/Create");
			return false;
		}

		json j;
		j["name"]        = m_Settings.name;
		j["version"]     = m_Settings.version;
		j["startScene"]  = m_Settings.startScene;
		j["windowTitle"] = m_Settings.windowTitle;
		j["assetsDir"]   = m_Settings.assetsDir;
		j["scenesDir"]   = m_Settings.scenesDir;

		std::ofstream file(m_ProjectFile);
		if (!file.is_open())
		{
			LOG_ERROR("Project: cannot write '{}'", m_ProjectFile.string());
			return false;
		}
		file << j.dump(2);
		return true;
	}

} // namespace Nightbloom
