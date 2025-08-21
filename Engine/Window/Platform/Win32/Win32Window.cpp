//------------------------------------------------------------------------------
// Win32Window.cpp
//
// Windows platform window implementation
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#include "Engine/Window/Platform/Win32/Win32Window.hpp"
#include "Engine/Input/InputSystem.hpp" 
#include "Core/Logger/Logger.hpp"
//#include "Core/Assert.hpp"  // For GUARANTEE_OR_DIE
#include <windowsx.h>

#include <imgui.h>

// Forward declaration for ImGui
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

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

		InputSystem* input = window->GetInputSystem();

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
			// Clear input state when losing focus
			if (input)
				input->ClearState();

			if (window->m_FocusCallback)
				window->m_FocusCallback(false);
			return 0;
		}

		case WM_KEYDOWN:
		{
			// Check for repeat (bit 30 of lParam)
			bool isRepeat = (lParam & 0x40000000) != 0;
			if (!isRepeat && input)  // Ignore key repeats for now
			{
				input->OnKeyDown(static_cast<unsigned int>(wParam));
			}
			else if (!input)
			{
				// Fallback logging if no input system
				LOG_TRACE("Key pressed: {}", wParam);
			}
			return 0;
		}

		case WM_KEYUP:
		{
			if (input)
				input->OnKeyUp(static_cast<unsigned int>(wParam));
			else
				LOG_TRACE("Key released: {}", wParam);
			return 0;
		}

		case WM_SYSKEYDOWN:  // Handle Alt+ combinations
		{
			bool isRepeat = (lParam & 0x40000000) != 0;
			if (!isRepeat && input)
			{
				input->OnKeyDown(static_cast<unsigned int>(wParam));
			}
			return 0;
		}

		case WM_SYSKEYUP:
		{
			if (input)
				input->OnKeyUp(static_cast<unsigned int>(wParam));
			return 0;
		}

		case WM_CHAR:
		{
			if (input)
				input->OnChar(static_cast<unsigned int>(wParam));
			else
				LOG_TRACE("Character input: {}", (char)wParam);
			return 0;
		}

		case WM_LBUTTONDOWN:
		{
			if (input)
			{
				input->OnMouseButtonDown(0);  // 0 = left button
				SetCapture(windowHandle);  // Capture mouse even if it leaves window
			}
			else
			{
				LOG_TRACE("Mouse button event: Left Down");
			}
			return 0;
		}

		case WM_LBUTTONUP:
		{
			if (input)
			{
				input->OnMouseButtonUp(0);  // 0 = left button
				ReleaseCapture();
			}
			else
			{
				LOG_TRACE("Mouse button event: Left Up");
			}
			return 0;
		}

		case WM_RBUTTONDOWN:
		{
			if (input)
			{
				input->OnMouseButtonDown(1);  // 1 = right button
				SetCapture(windowHandle);
			}
			else
			{
				LOG_TRACE("Mouse button event: Right Down");
			}
			return 0;
		}

		case WM_RBUTTONUP:
		{
			if (input)
			{
				input->OnMouseButtonUp(1);  // 1 = right button
				ReleaseCapture();
			}
			else
			{
				LOG_TRACE("Mouse button event: Right Up");
			}
			return 0;
		}

		case WM_MBUTTONDOWN:
		{
			if (input)
			{
				input->OnMouseButtonDown(2);  // 2 = middle button
				SetCapture(windowHandle);
			}
			else
			{
				LOG_TRACE("Mouse button event: Middle Down");
			}
			return 0;
		}

		case WM_MBUTTONUP:
		{
			if (input)
			{
				input->OnMouseButtonUp(2);  // 2 = middle button
				ReleaseCapture();
			}
			else
			{
				LOG_TRACE("Mouse button event: Middle Up");
			}
			return 0;
		}

		case WM_XBUTTONDOWN:
		{
			UINT button = GET_XBUTTON_WPARAM(wParam);
			if (input)
			{
				if (button == XBUTTON1)
					input->OnMouseButtonDown(3);  // 3 = X1
				else if (button == XBUTTON2)
					input->OnMouseButtonDown(4);  // 4 = X2
				SetCapture(windowHandle);
			}
			else
			{
				LOG_TRACE("Mouse button event: X{} Down", button);
			}
			return TRUE;  // Must return TRUE for X buttons
		}

		case WM_XBUTTONUP:
		{
			UINT button = GET_XBUTTON_WPARAM(wParam);
			if (input)
			{
				if (button == XBUTTON1)
					input->OnMouseButtonUp(3);  // 3 = X1
				else if (button == XBUTTON2)
					input->OnMouseButtonUp(4);  // 4 = X2
				ReleaseCapture();
			}
			else
			{
				LOG_TRACE("Mouse button event: X{} Up", button);
			}
			return TRUE;  // Must return TRUE for X buttons
		}


		case WM_MOUSEWHEEL:
		{
			short wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
			if (input)
			{
				float delta = static_cast<float>(wheelDelta) / WHEEL_DELTA;
				input->OnMouseWheel(delta);
			}
			else
			{
				LOG_TRACE("Mouse wheel: {}", wheelDelta);
			}
			return 0;
		}

		case WM_MOUSEMOVE:
		{
			int x = GET_X_LPARAM(lParam);
			int y = GET_Y_LPARAM(lParam);
			if (input)
			{
				input->OnMouseMove(x, y);
			}
			// Only log if no input system and uncommenting the original logs
			// else
			// {
			//     LOG_TRACE("Mouse move: ({}, {})", x, y);
			// }
			return 0;
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
		, m_InputSystem(nullptr)  // Initialize the input system pointer
	{
		s_MainWindow = this;
		CreateOSWindow(desc);
	}

	Win32Window::~Win32Window()
	{
		m_InputSystem = nullptr;

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
		// TEMPORARY: Clear the window with GDI to test the rendering loop
		if (m_DisplayContext)
		{
			// Get the client rect
			RECT clientRect;
			GetClientRect(m_Hwnd, &clientRect);

			// Create a brush with a dark blue color (to match your clear color)
			HBRUSH brush = CreateSolidBrush(RGB(25, 25, 51)); // Dark blue (0.1, 0.1, 0.2 in RGB)

			// Fill the window
			FillRect(m_DisplayContext, &clientRect, brush);

			// Clean up
			DeleteObject(brush);

			// Force a redraw
			InvalidateRect(m_Hwnd, nullptr, FALSE);
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