//------------------------------------------------------------------------------
// Win32Window.hpp
//
// Windows platform window implementation
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Window/Window.hpp"
// #include "Math/IntVec2.hpp"

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

		static Win32Window* s_MainWindow;
	};
}