//------------------------------------------------------------------------------
// Application.hpp
//
// Parent file for applications made using the Nightbloom Engine
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#pragma once
#include <memory>
#include "Engine/Window/Window.hpp"

namespace Nightbloom
{
	class Application
	{
	public:
		// ToDo: probably need to replace with an application desc later. once i implement xml
		Application(const std::string& name = "NightBloom Application");
		virtual ~Application();

		//Called by main()
		void Run();

		// Override these methods in game
		virtual void OnStartup() {}
		virtual void OnUpdate(float deltaTime) {}
		virtual void OnRender() {}
		virtual void OnShutdown() {}

		//saving for later when i add in an event system
		virtual void OnEvent(/* Event& e */) {}

		//Accessors for engine systems
		Window* GetWindow() const { return m_Window.get(); }

		void Quit() { m_Running = false; }

	private:
		std::unique_ptr<Window> m_Window;
		bool m_Running = true;
		float m_LastFrameTime = 0.0f;
	};

	//To be defined by the client
	Application* CreateApplication();
}