//------------------------------------------------------------------------------
// Window.cpp
//
// Window factory implementation
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#include "Engine/Window/Window.hpp"
#include "Core/Logger/Logger.hpp"
#include <iostream>
#include <memory>
#include "Engine/Window/Platform/Win32/Win32Window.hpp"

namespace Nightbloom
{
	// Forward declare instead of including the header
#ifdef NIGHTBLOOM_PLATFORM_WINDOWS
	//class Win32Window;
#endif

	std::unique_ptr<Window> Window::Create(const WindowDesc& desc)
	{
#ifdef NIGHTBLOOM_PLATFORM_WINDOWS


		LOG_INFO("Creating Win32 window: {} ({}x{})", desc.title, desc.width, desc.height);
		// We need to include the header ONLY here, inside the function

		try
		{
			auto window = std::make_unique<Win32Window>(desc);
			LOG_INFO("Win32Window created successfully");
			return window;
		}
		catch (const std::exception& e)
		{
			LOG_ERROR("Exception creating window: {}", e.what());
			return nullptr;
		}
#else
		LOG_ERROR("Unsupported platform for window creation!");
		return nullptr;
#endif
	}
	void Window::SetInputSystem(InputSystem* inputSystem)
	{
		m_InputSystem = inputSystem;
		if (m_InputSystem)
		{
			LOG_INFO("Input system connected to Win32 window");
		}
	}
}

//namespace Nightbloom
//{
//	std::unique_ptr<Window> Window::Create(const WindowDesc& desc)
//	{
//#ifdef NIGHTBLOOM_PLATFORM_WINDOWS
//		LOG_INFO("Creating Win32 window: {} ({}x{})", desc.title, desc.width, desc.height);
//		return std::make_unique<Win32Window>(desc);
//#elif defined(NIGHTBLOOM_PLATFORM_LINUX)
//		LOG_INFO("Creating X11 window: {} ({}x{})", desc.title, desc.width, desc.height);
//		//return std::make_unique<X11Window>(desc);
//#elif defined(NIGHTBLOOM_PLATFORM_MACOS)
//		LOG_INFO("Creating Cocoa window: {} ({}x{})", desc.title, desc.width, desc.height);
//		//return std::make_unique<CocoaWindow>(desc);
//#else
//		LOG_ERROR("Unsupported platform for window creation");
//		return nullptr;
//#endif
//	}
//}