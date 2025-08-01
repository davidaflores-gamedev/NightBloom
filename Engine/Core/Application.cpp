//------------------------------------------------------------------------------
// Application.cpp
//
// Parent file for applications made using the Nightbloom Engine
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#include "Core/Application.hpp"
#include "Core/Engine.hpp"
#include "Core/Logger/Logger.hpp"
#include <chrono>

#include <iostream>

namespace Nightbloom
{
	Application::Application(const std::string& name)
	{
		//Engine handles all system initialization
		EngineInit();

		//Create window
		WindowDesc desc;
		desc.title = name;
		desc.width = 1280;
		desc.height = 720;
		desc.resizable = true;

		m_Window = Window::Create(desc);

		// Set up window callbacks
		m_Window->SetCloseCallback([this]() {
			LOG_INFO("Window close requested");
			Quit();
			});
	}

	Application::~Application()
	{
		EngineShutdown();

		//OnShutdown();
		//LOG_INFO("Application shutting down");
	}

	void Application::Run()
	{
		OnStartup();

		using Clock = std::chrono::high_resolution_clock;
		auto lastTime = Clock::now();

		while (m_Running && m_Window->IsOpen())
		{
			auto currentTime = Clock::now();
			float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
			
			m_Window->PollEvents();
			
			OnUpdate(deltaTime);
			OnRender();
			
			m_Window->SwapBuffers();

			lastTime = currentTime;

			// Sleep to limit frame rate to ~40 FPS for now
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
		}

		OnShutdown();
	}
}