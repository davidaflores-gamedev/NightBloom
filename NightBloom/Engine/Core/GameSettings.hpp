//------------------------------------------------------------------------------
// GameSettings.hpp
//
// Persisted runtime settings for the standalone Game (video + controls). Stored
// as JSON at %APPDATA%/Nightbloom/game.json (falls back to ./game.json). Lives in
// the engine because it uses the engine-private nlohmann/json; the header stays
// dependency-free so the Game can include it. There is deliberately no audio
// section — the engine has no audio system yet.
//------------------------------------------------------------------------------
#pragma once

#include <string>
#include <cstdint>

namespace Nightbloom
{
	struct GameSettings
	{
		// --- Controls ---
		float mouseSensitivity = 1.0f;   // multiplier applied to the raw look delta
		bool  invertY          = false;  // flip pitch direction

		// --- Video ---
		bool  vsync         = false;     // false = uncapped (Mailbox/Immediate), true = FIFO
		int   shadowQuality = 1;         // 0=Low(1024) 1=Medium(2048) 2=High(4096)
		int   width         = 1280;      // windowed resolution
		int   height        = 720;

		// %APPDATA%/Nightbloom/game.json (or ./game.json if APPDATA is unset).
		static std::string ConfigPath();

		// Best-effort: a missing or invalid file leaves the defaults untouched.
		void Load();
		bool Save() const;

		// Map the shadowQuality index to a shadow-map resolution.
		static uint32_t ShadowResolutionFor(int quality);
	};
}
