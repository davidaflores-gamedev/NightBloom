//------------------------------------------------------------------------------
// GameSettings.cpp
//------------------------------------------------------------------------------
#include "Engine/Core/GameSettings.hpp"
#include "Engine/Core/Logger/Logger.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <algorithm>

namespace Nightbloom
{
	using json = nlohmann::json;

	std::string GameSettings::ConfigPath()
	{
		namespace fs = std::filesystem;
		const char* appData = std::getenv("APPDATA");
		fs::path dir = (appData && *appData) ? fs::path(appData) / "Nightbloom" : fs::path(".");
		return (dir / "game.json").string();
	}

	uint32_t GameSettings::ShadowResolutionFor(int quality)
	{
		switch (quality)
		{
			case 0:  return 1024;
			case 2:  return 4096;
			default: return 2048;
		}
	}

	void GameSettings::Load()
	{
		std::ifstream f(ConfigPath());
		if (!f.is_open())
			return;  // no file yet — keep defaults

		try
		{
			json j;
			f >> j;
			mouseSensitivity = j.value("mouseSensitivity", mouseSensitivity);
			invertY          = j.value("invertY", invertY);
			vsync            = j.value("vsync", vsync);
			shadowQuality    = j.value("shadowQuality", shadowQuality);
			width            = j.value("width", width);
			height           = j.value("height", height);
		}
		catch (const std::exception& e)
		{
			LOG_WARN("GameSettings: failed to parse '{}': {} (using defaults)", ConfigPath(), e.what());
			return;
		}

		// Clamp to sane ranges so a hand-edited file can't wedge the game.
		mouseSensitivity = std::clamp(mouseSensitivity, 0.05f, 5.0f);
		shadowQuality    = std::clamp(shadowQuality, 0, 2);
		width            = std::max(width, 640);
		height           = std::max(height, 480);
	}

	bool GameSettings::Save() const
	{
		namespace fs = std::filesystem;
		try
		{
			fs::path p(ConfigPath());
			if (p.has_parent_path())
				fs::create_directories(p.parent_path());

			json j;
			j["mouseSensitivity"] = mouseSensitivity;
			j["invertY"]          = invertY;
			j["vsync"]            = vsync;
			j["shadowQuality"]    = shadowQuality;
			j["width"]            = width;
			j["height"]           = height;

			std::ofstream f(ConfigPath());
			if (!f.is_open())
				return false;
			f << j.dump(2);
			return true;
		}
		catch (const std::exception& e)
		{
			LOG_WARN("GameSettings: failed to save '{}': {}", ConfigPath(), e.what());
			return false;
		}
	}
}
