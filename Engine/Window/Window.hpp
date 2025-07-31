//------------------------------------------------------------------------------
// Window.hpp
//
// Abstract window interface
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------
#pragma once

#include "WindowDesc.hpp"
#include <memory>
#include <functional>

namespace Nightbloom
{
	using WindowCloseCallback = std::function<void()>;
	using WindowResizeCallback = std::function<void(int width, int height)>;
	using WindowFocusCallback = std::function<void(bool focused)>;

	class Window
	{
	public:
		// Factory method to create a window instance
		static std::unique_ptr<Window> Create(const WindowDesc& desc);

		virtual ~Window() = default;

		//Core functionality
		virtual bool IsOpen() const = 0;
		virtual void PollEvents() = 0;
		virtual void SwapBuffers() = 0;

		// Properties
		virtual int GetWidth() const = 0;
		virtual int GetHeight() const = 0;
		virtual void SetTitle(const std::string& title) = 0;
		virtual void SetVSync(bool enabled) = 0;

		//Window manipulation
		virtual void SetPosition(int x, int y) = 0;
		virtual void SetSize(int width, int height) = 0;
		virtual void Show() = 0;
		virtual void Hide() = 0;
		virtual void Focus() = 0;
		virtual void Maximize() = 0;
		virtual void Minimize() = 0;
		virtual void Restore() = 0;

		// Platform-specific handle
		virtual void* GetNativeHandle() const = 0;

		//Event callbacks
		void SetCloseCallback(WindowCloseCallback callback) { m_CloseCallback = callback; }
		void SetResizeCallback(WindowResizeCallback callback) { m_ResizeCallback = callback; }
		void SetFocusCallback(WindowFocusCallback callback) { m_FocusCallback = callback; }

		// try sometime std::move(callback) to avoid copies

	protected:
		// Callbacks that derived classes should call
		WindowCloseCallback m_CloseCallback;
		WindowResizeCallback m_ResizeCallback;
		WindowFocusCallback m_FocusCallback;
	};
}