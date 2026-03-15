//------------------------------------------------------------------------------
// Application.hpp
//
// Parent file for applications made using the Nightbloom Engine
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#pragma once
#include <memory>
#include "Engine/Window/Window.hpp"
#include "Engine/Input/InputSystem.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/PipelineInterface.hpp"  // ADD THIS for PipelineType enum
#include "Engine/Renderer/DrawCommandSystem.hpp" // For DrawCommand and related types
#include "Engine/Core/Scene.hpp"

#include <glm/glm.hpp> // ToDo: remove this if reg mathclass is better

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
		void Quit() { m_Running = false; }

		// Override these methods in game
		virtual void OnStartup() {}
		virtual void OnUpdate(float deltaTime) { (void)(deltaTime); }
		virtual void OnRender() {}
		virtual void OnShutdown() {}

		//saving for later when i add in an event system
		virtual void OnEvent(/* Event& e */) {}

		//Accessors for engine systems
		Window* GetWindow() const { return m_Window.get(); }
		Renderer* GetRenderer() const { return m_Renderer.get(); }
		InputSystem* GetInput() const { return m_Input.get(); }

		// ADD THIS - convenience method for editor
		IPipelineManager* GetPipelineManager() const
		{
			return m_Renderer ? m_Renderer->GetPipelineManager() : nullptr;
		}

		//MeshDrawable* GetTestCube() const { return m_TestCube.get(); }
		//void SetTestCube(std::unique_ptr<MeshDrawable> cube) { m_TestCube = std::move(cube); }
		//
		//glm::mat4 GetViewMatrix() const { return m_ViewMatrix; }
		//void SetViewMatrix(const glm::mat4& view) { m_ViewMatrix = view; }
		//glm::mat4 GetProjectionMatrix() const { return m_ProjectionMatrix; }
		//void SetProjectionMatrix(const glm::mat4& proj) { m_ProjectionMatrix = proj; }

	public:

		// Camera
		glm::mat4 m_ViewMatrix;
		glm::mat4 m_ProjectionMatrix;

		PipelineType m_CurrentTestPipeline = PipelineType::Mesh;
	private:
		// Engine Components
		std::unique_ptr<Window> m_Window;
		std::unique_ptr<Renderer> m_Renderer;
		std::unique_ptr<InputSystem> m_Input;

		bool m_Running = true;
		float m_LastFrameTime = 0.0f;
	};

	//To be defined by the client
	Application* CreateApplication();
}