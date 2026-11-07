//------------------------------------------------------------------------------
// Application.hpp
//
// Parent file for applications made using the Nightbloom Engine
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#pragma once
#include <memory>
#include <cstdint>
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
		// maxMsaaSamples: scene MSAA cap (1/2/4/8). Passed here because the
		// renderer is created in this constructor, before a subclass can act.
		Application(const std::string& name = "NightBloom Application", uint32_t maxMsaaSamples = 2);
		virtual ~Application();

		//Called by main()
		void Run();
		void Quit() { m_Running = false; }

		// Background color the scene clears to each frame (linear HDR, before
		// tonemapping). There is no sky renderer, so this IS the sky.
		void SetClearColor(float r, float g, float b) { m_ClearColor[0] = r; m_ClearColor[1] = g; m_ClearColor[2] = b; }

		// Override these methods in game
		virtual void OnStartup() {}
		virtual void OnUpdate(float deltaTime) { (void)(deltaTime); }
		virtual void OnRender() {}
		virtual void OnShutdown() {}

		//----------------------------------------------------------------------
		// Fixed-timestep simulation (opt-in).
		//
		// Off by default (rate 0): the loop is exactly what it always was —
		// one OnUpdate(deltaTime) per frame — so existing apps are unaffected.
		//
		// SetFixedUpdateRate(60) turns it on. Each frame then runs, in order:
		//   1. OnFixedUpdate(1/60) zero or more times, from an accumulator
		//   2. OnUpdate(deltaTime) once (camera, UI, anything per-frame)
		//   3. OnRender()
		// Render code blends the last two sim states with
		// GetInterpolationAlpha() (0..1, how far into the next tick we are).
		//
		// INPUT EDGES: InputSystem::IsPressed is per *frame*. A frame can run
		// 0, 1 or several ticks, so a press read directly inside OnFixedUpdate
		// can be seen twice or missed. Latch presses in OnPreFixedUpdate (called
		// once per frame, before any tick; OnUpdate runs *after* the ticks) and
		// consume them on the next tick that runs.
		//
		// If the sim falls behind by more than kMaxFixedStepsPerFrame ticks, the
		// backlog is dropped rather than spiralling (the game slows down instead
		// of freezing).
		//----------------------------------------------------------------------
		virtual void OnPreFixedUpdate(float deltaTime) { (void)(deltaTime); }
		virtual void OnFixedUpdate(float fixedDeltaTime) { (void)(fixedDeltaTime); }

		void  SetFixedUpdateRate(float hz) { m_FixedDeltaTime = (hz > 0.0f) ? 1.0f / hz : 0.0f; m_FixedAccumulator = 0.0f; }
		bool  IsFixedUpdateEnabled() const { return m_FixedDeltaTime > 0.0f; }
		float GetFixedDeltaTime() const { return m_FixedDeltaTime; }
		float GetInterpolationAlpha() const { return m_InterpolationAlpha; }
		uint64_t GetFixedTickCount() const { return m_FixedTickCount; }

		static constexpr int kMaxFixedStepsPerFrame = 8;

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
		float m_ClearColor[3] = { 0.1f, 0.1f, 0.2f };   // dark blue default
		float m_LastFrameTime = 0.0f;

		// Fixed-timestep state (see SetFixedUpdateRate). 0 = disabled.
		float    m_FixedDeltaTime = 0.0f;
		float    m_FixedAccumulator = 0.0f;
		float    m_InterpolationAlpha = 0.0f;
		uint64_t m_FixedTickCount = 0;
	};

	//To be defined by the client
	Application* CreateApplication();
}