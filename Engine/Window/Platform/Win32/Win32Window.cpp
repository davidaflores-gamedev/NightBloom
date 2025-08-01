//------------------------------------------------------------------------------
// Win32Window.cpp
//
// Windows platform window implementation
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#include "Engine/Window/Platform/Win32/Win32Window.hpp"
#include "Core/Logger/Logger.hpp"
//#include "Core/Assert.hpp"  // For GUARANTEE_OR_DIE
#include <windowsx.h>

// Forward declaration for ImGui
//extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Nightbloom
{
	Win32Window* Win32Window::s_MainWindow = nullptr;

	// Windows message handling procedure
	LRESULT CALLBACK WindowsMessageHandlingProcedure(HWND windowHandle, UINT wmMessageCode, WPARAM wParam, LPARAM lParam)
	{
		// ImGui handling (if you're using ImGui)
#ifdef IMGUI_VERSION
		if (ImGui_ImplWin32_WndProcHandler(windowHandle, wmMessageCode, wParam, lParam))
			return true;
#endif

		// Get the window instance
		Win32Window* window = Win32Window::GetMainWindowInstance();
		if (!window)
		{
			return DefWindowProc(windowHandle, wmMessageCode, wParam, lParam);
		}

		switch (wmMessageCode)
		{
		case WM_CLOSE:
		{
			window->m_ShuttingDown = true;
			if (window->m_CloseCallback)
				window->m_CloseCallback();
			return 0;
		}

		case WM_SIZE:
		{
			UINT width = LOWORD(lParam);
			UINT height = HIWORD(lParam);
			window->m_ClientDimensionsX = width;
			window->m_ClientDimensionsY = height;

			if (window->m_ResizeCallback)
				window->m_ResizeCallback(width, height);
			return 0;
		}

		case WM_SETFOCUS:
		{
			if (window->m_FocusCallback)
				window->m_FocusCallback(true);
			return 0;
		}

		case WM_KILLFOCUS:
		{
			if (window->m_FocusCallback)
				window->m_FocusCallback(false);
			return 0;
		}

		case WM_KEYDOWN:
		{
			// You can add input handling here or fire events
			LOG_TRACE("Key pressed: {}", wParam);
			break;
		}

		case WM_KEYUP:
		{
			LOG_TRACE("Key released: {}", wParam);
			break;
		}

		case WM_CHAR:
		{
			LOG_TRACE("Character input: {}", (char)wParam);
			break;
		}

		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		{
			LOG_TRACE("Mouse button event");
			break;
		}

		case WM_MOUSEWHEEL:
		{
			short wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
			LOG_TRACE("Mouse wheel: {}", wheelDelta);
			break;
		}

		case WM_MOUSEMOVE:
		{
			int x = GET_X_LPARAM(lParam);
			int y = GET_Y_LPARAM(lParam);
			LOG_TRACE("Mouse move: ({}, {})", x, y);
			break;
		}
		}

		return DefWindowProc(windowHandle, wmMessageCode, wParam, lParam);
	}

	Win32Window::Win32Window(const WindowDesc& desc)
		: m_Hwnd(nullptr)
		, m_DisplayContext(nullptr)
		, m_Instance(GetModuleHandle(nullptr))
		, m_IsOpen(true)
		, m_VSync(desc.vsync)
		, m_ShuttingDown(false)
		, m_ClientDimensionsX(desc.width), m_ClientDimensionsY(desc.height)
		, m_Title(desc.title)
	{
		s_MainWindow = this;
		CreateOSWindow(desc);
	}

	Win32Window::~Win32Window()
	{
		if (m_DisplayContext)
		{
			ReleaseDC(m_Hwnd, m_DisplayContext);
		}

		if (m_Hwnd)
		{
			DestroyWindow(m_Hwnd);
		}

		UnregisterClass(TEXT("NightbloomWindowClass"), m_Instance);

		if (s_MainWindow == this)
		{
			s_MainWindow = nullptr;
		}
	}

	void Win32Window::CreateOSWindow(const WindowDesc& desc)
	{
		// Define window class
		WNDCLASSEX windowClassDescription = {};
		windowClassDescription.cbSize = sizeof(windowClassDescription);
		windowClassDescription.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
		windowClassDescription.lpfnWndProc = static_cast<WNDPROC>(WindowsMessageHandlingProcedure);
		windowClassDescription.hInstance = m_Instance;
		windowClassDescription.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
		windowClassDescription.hCursor = LoadCursor(nullptr, IDC_ARROW);
		windowClassDescription.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
		windowClassDescription.lpszClassName = TEXT("NightbloomWindowClass");
		windowClassDescription.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

		ATOM result = RegisterClassEx(&windowClassDescription);
		if (!result)
		{
			DWORD error = GetLastError();
			if (error != ERROR_CLASS_ALREADY_EXISTS)
			{
				LOG_ERROR("Failed to register window class! Error: {}", error);
				m_IsOpen = false;
				return;
			}
		}

		// Window style
		DWORD windowStyleFlags = WS_OVERLAPPEDWINDOW;
		if (!desc.resizable)
		{
			windowStyleFlags &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
		}

		// Calculate window rect
		RECT clientRect = { 0, 0, desc.width, desc.height };

		if (desc.fullscreen)
		{
			windowStyleFlags = WS_POPUP | WS_VISIBLE;
			clientRect.right = GetSystemMetrics(SM_CXSCREEN);
			clientRect.bottom = GetSystemMetrics(SM_CYSCREEN);
		}

		AdjustWindowRectEx(&clientRect, windowStyleFlags, FALSE, WS_EX_APPWINDOW);

		// Calculate position
		int windowWidth = clientRect.right - clientRect.left;
		int windowHeight = clientRect.bottom - clientRect.top;
		int x = desc.x;
		int y = desc.y;

		if (x < 0 || y < 0)
		{
			int screenWidth = GetSystemMetrics(SM_CXSCREEN);
			int screenHeight = GetSystemMetrics(SM_CYSCREEN);
			if (x < 0) x = (screenWidth - windowWidth) / 2;
			if (y < 0) y = (screenHeight - windowHeight) / 2;
		}
		
		LPCSTR title = desc.title.c_str();

		// Try sometime wide strings
		//WCHAR windowTitle[1024];
		//MultiByteToWideChar(CP_UTF8, 0, desc.title.c_str(), -1, windowTitle, sizeof(windowTitle) / sizeof(windowTitle[0]));

		// Create window
		m_Hwnd = CreateWindowExA(
			WS_EX_APPWINDOW,
			windowClassDescription.lpszClassName,
			title,
			windowStyleFlags,
			x, y,
			windowWidth, windowHeight,
			nullptr,
			nullptr,
			m_Instance,
			nullptr
		);

		if (!m_Hwnd)
		{
			LOG_ERROR("Failed to create window! Error: {}", GetLastError());
			m_IsOpen = false;
			return;
		}

		// Show and update window
		ShowWindow(m_Hwnd, desc.maximized ? SW_MAXIMIZE : SW_SHOW);
		SetForegroundWindow(m_Hwnd);
		SetFocus(m_Hwnd);
		UpdateWindow(m_Hwnd);

		// Get display context
		m_DisplayContext = GetDC(m_Hwnd);

		// Get actual client dimensions
		RECT finalRect;
		GetClientRect(m_Hwnd, &finalRect);
		m_ClientDimensionsX = finalRect.right - finalRect.left;
		m_ClientDimensionsY = finalRect.bottom - finalRect.top;

		LOG_INFO("Win32 window created successfully: {} ({}x{}) at ({}, {})",
			desc.title, m_ClientDimensionsX, m_ClientDimensionsY, x, y);
	}

	void Win32Window::PollEvents()
	{
		RunMessagePump();
	}

	void Win32Window::RunMessagePump()
	{
		MSG queuedMessage;
		while (PeekMessage(&queuedMessage, nullptr, 0, 0, PM_REMOVE))
		{
			if (queuedMessage.message == WM_QUIT)
			{
				m_ShuttingDown = true;
				break;
			}

			TranslateMessage(&queuedMessage);
			DispatchMessage(&queuedMessage);
		}
	}

	void Win32Window::SwapBuffers()
	{
		// This will be implemented when we add the graphics context
		// For now, just call SwapBuffers if we have a display context
		if (m_DisplayContext)
		{
			//::SwapBuffers(m_DisplayContext);
		}
	}

	void Win32Window::SetTitle(const std::string& title)
	{
		m_Title = title;
		SetWindowTextA(m_Hwnd, title.c_str());
	}

	void Win32Window::SetPosition(int x, int y)
	{
		SetWindowPos(m_Hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
	}

	void Win32Window::SetSize(int width, int height)
	{
		RECT rect = { 0, 0, width, height };
		AdjustWindowRect(&rect, GetWindowLong(m_Hwnd, GWL_STYLE), FALSE);
		SetWindowPos(m_Hwnd, nullptr, 0, 0,
			rect.right - rect.left,
			rect.bottom - rect.top,
			SWP_NOMOVE | SWP_NOZORDER);
	}

	void Win32Window::Show()
	{
		ShowWindow(m_Hwnd, SW_SHOW);
	}

	void Win32Window::Hide()
	{
		ShowWindow(m_Hwnd, SW_HIDE);
	}

	void Win32Window::Focus()
	{
		SetForegroundWindow(m_Hwnd);
		SetFocus(m_Hwnd);
	}

	void Win32Window::Maximize()
	{
		ShowWindow(m_Hwnd, SW_MAXIMIZE);
	}

	void Win32Window::Minimize()
	{
		ShowWindow(m_Hwnd, SW_MINIMIZE);
	}

	void Win32Window::Restore()
	{
		ShowWindow(m_Hwnd, SW_RESTORE);
	}

	//std::pair<int, int> Win32Window::GetNormalizedCursorPosition() const
	//{
	//	POINT cursorCoords;
	//	RECT clientRect;
	//	::GetCursorPos(&cursorCoords);
	//	::ScreenToClient(m_Hwnd, &cursorCoords);
	//	::GetClientRect(m_Hwnd, &clientRect);
	//
	//	float cursorX = float(cursorCoords.x) / float(clientRect.right);
	//	float cursorY = float(cursorCoords.y) / float(clientRect.bottom);
	//
	//	return Vec2(cursorX, 1.0f - cursorY);
	//}

	float Win32Window::GetAspect() const
	{
		return static_cast<float>(m_ClientDimensionsX) / static_cast<float>(m_ClientDimensionsY);
	}
}

// Global helper function
bool IsMousePresent()
{
	return GetSystemMetrics(SM_MOUSEPRESENT) != 0;
}