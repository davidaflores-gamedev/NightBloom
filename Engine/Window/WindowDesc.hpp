//------------------------------------------------------------------------------
// WindowDesc.hpp
//
// Window creation parameters
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------
#pragma once

#include <string>

namespace Nightbloom
{
	struct WindowDesc
	{
		std::string title = "NightBloom Engine";
		int width = 1280;
		int height = 720;
		int x = -1;  // -1 = centered
		int y = -1;  // -1 = centered
		bool fullscreen = false;
		bool resizable = true;
		bool vsync = true;
		bool maximized = false;
	};
}