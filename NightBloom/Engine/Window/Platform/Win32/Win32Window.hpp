//------------------------------------------------------------------------------
// Win32Window.hpp
//
// Windows platform window implementation
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Window/Window.hpp"
// #include "Math/IntVec2.hpp"

#include "Engine/Input/Win32/DualSenseController.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace Nightbloom
{


	class Win32Window : public Window
	{
	public:
		Win32Window(const WindowDesc& desc);
		virtual ~Win32Window();

		// Window interface implementation
		bool IsOpen() const override { return m_IsOpen && !m_ShuttingDown; }
		void PollEvents() override;
		void SwapBuffers() override;

		int GetWidth() const override { return m_ClientDimensionsX; }
		int GetHeight() const override { return m_ClientDimensionsY; }
		void SetTitle(const std::string& title) override;
		void SetVSync(bool enabled) override { m_VSync = enabled; }

		void SetPosition(int x, int y) override;
		void SetSize(int width, int height) override;
		void Show() override;
		void Hide() override;
		void Focus() override;
		void Maximize() override;
		void Minimize() override;
		void Restore() override;

		void* GetNativeHandle() const override { return static_cast<void*>(m_Hwnd); }

		// Always-on mouselook support (see Window.hpp).
		void SetRelativeMouseMode(bool enabled) override;
		bool IsRelativeMouseMode() const override { return m_RelativeMouse; }
		void GetRelativeMouseDelta(float& dx, float& dy) const override { dx = m_RelMouseDX; dy = m_RelMouseDY; }

		// Additional methods from your old code
		//Vec2 GetNormalizedCursorPosition() const;
		//std::pair<int, int> GetClientDimensions() const { return std::pair<int, int>(m_ClientDimensionsX, m_ClientDimensionsY); }
		void* GetDisplayContext() const { return m_DisplayContext; }
		float GetAspect() const;

		// Static instance access (for legacy compatibility)
		static Win32Window* GetMainWindowInstance() { return s_MainWindow; }

	private:
		void CreateOSWindow(const WindowDesc& desc);
		void RunMessagePump();
		POINT ClientCenterScreen() const;  // client-area center in screen coords

		// Friend function for Windows message handling
		friend LRESULT CALLBACK WindowsMessageHandlingProcedure(HWND windowHandle, UINT wmMessageCode, WPARAM wParam, LPARAM lParam);

	private:
		HWND m_Hwnd;
		HDC m_DisplayContext;
		HINSTANCE m_Instance;
		bool m_IsOpen;
		bool m_VSync;
		bool m_ShuttingDown;
		int m_ClientDimensionsX, m_ClientDimensionsY;
		std::string m_Title;

		// Relative ("captured") mouse state for always-on mouselook.
		bool  m_RelativeMouse = false;
		bool  m_CursorHidden = false;
		float m_RelMouseDX = 0.0f, m_RelMouseDY = 0.0f;

		static Win32Window* s_MainWindow;
		// NOTE: the input system lives in the base class (Window::m_InputSystem, set via
		// SetInputSystem, read via GetInputSystem). Do NOT redeclare it here — a shadowing
		// member would stay null and silently break anything that reads it (it did: the
		// XInput poller). Use GetInputSystem().

		// XInput gamepad polling. XInput is level-state (polled), so we keep the
		// previous frame's state per pad to emit press/release edges, and only probe
		// empty slots occasionally (probing a disconnected slot every frame is slow).
		void PollGamepads();
		unsigned short m_PadPrevButtons[4] = { 0, 0, 0, 0 };
		bool           m_PadPrevLT[4] = { false, false, false, false };
		bool           m_PadPrevRT[4] = { false, false, false, false };
		bool           m_PadConnected[4] = { false, false, false, false };
		unsigned int   m_PadRescanCounter = 0;

		// PlayStation DualSense over raw HID (XInput can't see it). Polled alongside the
		// XInput pads in PollEvents; feeds the same InputSystem::OnGamepad* path.
		DualSenseController m_DualSense;
	};
}