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
	Application::Application(const std::string& name, uint32_t maxMsaaSamples)
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

		if (!m_Window)
		{
			LOG_ERROR("Failed to create window!");
			throw std::runtime_error("Window creation failed");
		}
		
		m_Input = std::make_unique<InputSystem>();

		// Connect them
		m_Window->SetInputSystem(m_Input.get());

		// Set up window callbacks
		m_Window->SetCloseCallback([this]() {
			LOG_INFO("Window close requested");
			Quit();
		});

		LOG_INFO("Initializing Renderer...");
		m_Renderer = std::make_unique<Renderer>();
		if (!m_Renderer->Initialize(m_Window->GetNativeHandle(), desc.width, desc.height, maxMsaaSamples))
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

		if (m_Input) {
			m_Input->Shutdown();
			m_Input.reset();
		}

		if (m_Window) {
			//m_Window->Shutdown();
			m_Window.reset();
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
			
			m_Input->BeginFrame();
			m_Window->PollEvents();
			m_Input->EndFrame();

			if (m_Window->GetWidth() == 0 || m_Window->GetHeight() == 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}

			// NOTE: the engine used to hard-bind P (TogglePipeline) and R
			// (ReloadShaders) here, for every app. That stole keys from games
			// (NightbloomGame's R = restart also recompiled every shader) and
			// double-fired with the editor's own P binding, cancelling it out.
			// Those are editor tools now: EditorApp::HandleEditorShortcuts
			// (Ctrl+R, P) plus the Debug/Viewport panel buttons.

			if (m_FixedDeltaTime > 0.0f)
			{
				OnPreFixedUpdate(deltaTime);

				m_FixedAccumulator += deltaTime;
				int steps = 0;
				while (m_FixedAccumulator >= m_FixedDeltaTime && steps < kMaxFixedStepsPerFrame)
				{
					OnFixedUpdate(m_FixedDeltaTime);
					m_FixedAccumulator -= m_FixedDeltaTime;
					++m_FixedTickCount;
					++steps;
				}
				// Fell behind (breakpoint, hitch, window drag): drop the backlog
				// instead of trying to catch up and spiralling.
				if (steps == kMaxFixedStepsPerFrame && m_FixedAccumulator >= m_FixedDeltaTime)
					m_FixedAccumulator = 0.0f;

				m_InterpolationAlpha = m_FixedAccumulator / m_FixedDeltaTime;
			}

			OnUpdate(deltaTime);
			
			// RENDER WITH THE NEW RENDERER
			if (m_Renderer && m_Renderer->IsInitialized())
			{
				m_Renderer->BeginFrame();

				if (m_Renderer->IsFrameValid())
				{
					m_Renderer->Clear(m_ClearColor[0], m_ClearColor[1], m_ClearColor[2], 1.0f);
					OnRender();  // Your app can override this
					m_Renderer->FinalizeFrame();
					m_Renderer->EndFrame();
				}
			}
			
			//m_Window->SwapBuffers();
			lastTime = currentTime;

			// Sleep to limit frame rate to ~40 FPS for now
			//std::this_thread::sleep_for(std::chrono::milliseconds(16));
		}

		OnShutdown();
	}
}