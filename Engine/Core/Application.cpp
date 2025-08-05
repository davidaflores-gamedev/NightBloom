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

		LOG_INFO("Initializing Renderer...");
		m_Renderer = std::make_unique<Renderer>();
		if (!m_Renderer->Initialize(m_Window->GetNativeHandle(), desc.width, desc.height))
		{
			LOG_ERROR("Failed to initialize Renderer");
			throw std::runtime_error("Renderer initialization failed");
			//return;
		}

		LOG_INFO("Renderer initialized successfully");
	}

	Application::~Application()
	{
		LOG_INFO("Application shutting down");

		// ADD THIS BLOCK
		if (m_Renderer) {
			m_Renderer->Shutdown();
			m_Renderer.reset();
		}

		EngineShutdown();

		//OnShutdown();
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
			
			// RENDER WITH THE NEW RENDERER
			if (m_Renderer && m_Renderer->IsInitialized()) {
				m_Renderer->BeginFrame();
				m_Renderer->Clear(0.1f, 0.1f, 0.2f, 1.0f);  // Dark blue background
				OnRender();  // Your app can override this
				m_Renderer->EndFrame();
			}
			
			m_Window->SwapBuffers();
			lastTime = currentTime;

			// Sleep to limit frame rate to ~40 FPS for now
			//std::this_thread::sleep_for(std::chrono::milliseconds(16));
		}

		OnShutdown();
	}
}