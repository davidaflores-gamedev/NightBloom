//------------------------------------------------------------------------------
// GLFWWindow.cpp
//
// GLFW-based window implementation (macOS — Vulkan via MoltenVK).
// Feeds the same InputSystem entry points as Win32Window: keys are translated
// to Win32 virtual-key codes (the currency InputSystem::VirtualKeyToInputCode
// expects), mouse buttons already share the 0..4 convention.
//------------------------------------------------------------------------------

#include "Engine/Core/Platform.hpp"

#ifdef NIGHTBLOOM_PLATFORM_MACOS

#include "Engine/Window/Platform/GLFW/GLFWWindow.hpp"
#include "Engine/Input/InputSystem.hpp"
#include "Core/Logger/Logger.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace Nightbloom
{
	namespace
	{
		int s_GlfwWindowCount = 0;

		// GLFW keycodes -> Win32 virtual-key codes. Letters and digits are ASCII
		// uppercase in both worlds; everything else the game uses is a small set.
		unsigned int GlfwKeyToVirtualKey(int key)
		{
			if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) return static_cast<unsigned int>(key);
			if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) return static_cast<unsigned int>(key);
			if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12)
				return 0x70 + static_cast<unsigned int>(key - GLFW_KEY_F1);  // VK_F1..VK_F12

			switch (key)
			{
			case GLFW_KEY_ESCAPE:        return 0x1B;  // VK_ESCAPE
			case GLFW_KEY_TAB:           return 0x09;  // VK_TAB
			case GLFW_KEY_CAPS_LOCK:     return 0x14;  // VK_CAPITAL
			case GLFW_KEY_LEFT_SHIFT:
			case GLFW_KEY_RIGHT_SHIFT:   return 0x10;  // VK_SHIFT
			case GLFW_KEY_LEFT_CONTROL:
			case GLFW_KEY_RIGHT_CONTROL: return 0x11;  // VK_CONTROL
			case GLFW_KEY_LEFT_SUPER:
			case GLFW_KEY_RIGHT_SUPER:   return 0x11;  // Cmd also acts as Control on mac
			case GLFW_KEY_LEFT_ALT:
			case GLFW_KEY_RIGHT_ALT:     return 0x12;  // VK_MENU
			case GLFW_KEY_SPACE:         return 0x20;  // VK_SPACE
			case GLFW_KEY_ENTER:         return 0x0D;  // VK_RETURN
			case GLFW_KEY_BACKSPACE:     return 0x08;  // VK_BACK
			case GLFW_KEY_LEFT:          return 0x25;  // VK_LEFT
			case GLFW_KEY_UP:            return 0x26;  // VK_UP
			case GLFW_KEY_RIGHT:         return 0x27;  // VK_RIGHT
			case GLFW_KEY_DOWN:          return 0x28;  // VK_DOWN
			default:                     return 0;
			}
		}

		GLFWWindow* Self(GLFWwindow* w)
		{
			return static_cast<GLFWWindow*>(glfwGetWindowUserPointer(w));
		}
	}

	GLFWWindow::GLFWWindow(const WindowDesc& desc)
		: m_Title(desc.title)
	{
		if (s_GlfwWindowCount == 0 && !glfwInit())
		{
			LOG_ERROR("glfwInit failed");
			return;
		}
		s_GlfwWindowCount++;

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // Vulkan, not OpenGL
		glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_MAXIMIZED, desc.maximized ? GLFW_TRUE : GLFW_FALSE);
		// Render at the real framebuffer extent. ImGui keeps logical coordinates
		// and supplies DisplayFramebufferScale for its Vulkan backend.
		glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);

		GLFWmonitor* monitor = desc.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
		m_Window = glfwCreateWindow(desc.width, desc.height, desc.title.c_str(), monitor, nullptr);
		if (!m_Window)
		{
			LOG_ERROR("glfwCreateWindow failed");
			return;
		}

		if (!desc.fullscreen && desc.x >= 0 && desc.y >= 0)
			glfwSetWindowPos(m_Window, desc.x, desc.y);

		glfwGetFramebufferSize(m_Window, &m_FramebufferW, &m_FramebufferH);
		glfwSetWindowUserPointer(m_Window, this);
		InstallCallbacks();

		m_IsOpen = true;
		LOG_INFO("GLFW window created successfully: {} ({}x{})", desc.title, m_FramebufferW, m_FramebufferH);
	}

	GLFWWindow::~GLFWWindow()
	{
		m_InputSystem = nullptr;
		if (m_Window)
		{
			glfwDestroyWindow(m_Window);
			m_Window = nullptr;
		}
		if (--s_GlfwWindowCount == 0)
			glfwTerminate();
	}

	void GLFWWindow::InstallCallbacks()
	{
		// ImGui's GLFW backend (installed later, during renderer init) replaces
		// these callbacks and chains to whatever was installed before it — i.e.
		// to these. So input keeps flowing to the game with ImGui on top.

		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* w)
		{
			GLFWWindow* self = Self(w);
			self->m_ShuttingDown = true;
			if (self->m_CloseCallback) self->m_CloseCallback();
		});

		glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow* w, int width, int height)
		{
			GLFWWindow* self = Self(w);
			self->m_FramebufferW = width;
			self->m_FramebufferH = height;
			if (self->m_ResizeCallback) self->m_ResizeCallback(width, height);
		});

		glfwSetWindowFocusCallback(m_Window, [](GLFWwindow* w, int focused)
		{
			GLFWWindow* self = Self(w);
			if (!focused)
			{
				// Mirror Win32: drop stale input and release the captured cursor so
				// Cmd-Tab doesn't strand an invisible pointer.
				if (InputSystem* input = self->GetInputSystem()) input->ClearState();
				if (self->m_RelativeMouse) self->SetRelativeMouseMode(false);
			}
			if (self->m_FocusCallback) self->m_FocusCallback(focused != 0);
		});

		glfwSetKeyCallback(m_Window, [](GLFWwindow* w, int key, int scancode, int action, int mods)
		{
			(void)scancode; (void)mods;
			if (action == GLFW_REPEAT) return;  // ignore key repeats, like Win32
			InputSystem* input = Self(w)->GetInputSystem();
			if (!input) return;
			unsigned int vk = GlfwKeyToVirtualKey(key);
			if (vk == 0) return;
			if (action == GLFW_PRESS) input->OnKeyDown(vk);
			else                      input->OnKeyUp(vk);
		});

		glfwSetCharCallback(m_Window, [](GLFWwindow* w, unsigned int codepoint)
		{
			if (InputSystem* input = Self(w)->GetInputSystem())
				input->OnChar(codepoint);
		});

		glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* w, int button, int action, int mods)
		{
			(void)mods;
			if (button < 0 || button > 4) return;  // same 0..4 convention as InputSystem
			InputSystem* input = Self(w)->GetInputSystem();
			if (!input) return;
			if (action == GLFW_PRESS) input->OnMouseButtonDown(button);
			else if (action == GLFW_RELEASE) input->OnMouseButtonUp(button);
		});

		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* w, double x, double y)
		{
			GLFWWindow* self = Self(w);
			// In relative mode the coordinates are virtual/unbounded — the game
			// reads GetRelativeMouseDelta instead, so don't feed them as absolute.
			if (self->m_RelativeMouse) return;
			if (InputSystem* input = self->GetInputSystem())
				input->OnMouseMove(static_cast<int>(x), static_cast<int>(y));
		});

		glfwSetScrollCallback(m_Window, [](GLFWwindow* w, double dx, double dy)
		{
			(void)dx;
			if (InputSystem* input = Self(w)->GetInputSystem())
				input->OnMouseWheel(static_cast<float>(dy));
		});
	}

	void GLFWWindow::PollEvents()
	{
		glfwPollEvents();

		if (m_Window && glfwWindowShouldClose(m_Window))
			m_ShuttingDown = true;

		// Relative mouse: with GLFW_CURSOR_DISABLED the cursor position is a
		// virtual, unbounded coordinate — the frame-to-frame difference IS the
		// look delta.
		if (m_RelativeMouse && !m_ShuttingDown)
		{
			double x = 0.0, y = 0.0;
			glfwGetCursorPos(m_Window, &x, &y);
			if (m_RelSeeded)
			{
				m_RelMouseDX = static_cast<float>(x - m_RelLastX);
				m_RelMouseDY = static_cast<float>(y - m_RelLastY);
			}
			else
			{
				m_RelMouseDX = 0.0f;
				m_RelMouseDY = 0.0f;
				m_RelSeeded = true;
			}
			m_RelLastX = x;
			m_RelLastY = y;
		}
	}

	void GLFWWindow::SetRelativeMouseMode(bool enabled)
	{
		if (enabled == m_RelativeMouse || !m_Window)
			return;

		m_RelativeMouse = enabled;
		m_RelSeeded = false;
		m_RelMouseDX = 0.0f;
		m_RelMouseDY = 0.0f;

		if (enabled)
		{
			glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			if (glfwRawMouseMotionSupported())
				glfwSetInputMode(m_Window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
		}
		else
		{
			glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
	}

	void GLFWWindow::SetTitle(const std::string& title)
	{
		m_Title = title;
		if (m_Window) glfwSetWindowTitle(m_Window, title.c_str());
	}

	void GLFWWindow::SetPosition(int x, int y)
	{
		if (m_Window) glfwSetWindowPos(m_Window, x, y);
	}

	void GLFWWindow::SetSize(int width, int height)
	{
		if (m_Window) glfwSetWindowSize(m_Window, width, height);
	}

	void GLFWWindow::Show()     { if (m_Window) glfwShowWindow(m_Window); }
	void GLFWWindow::Hide()     { if (m_Window) glfwHideWindow(m_Window); }
	void GLFWWindow::Focus()    { if (m_Window) glfwFocusWindow(m_Window); }
	void GLFWWindow::Maximize() { if (m_Window) glfwMaximizeWindow(m_Window); }
	void GLFWWindow::Minimize() { if (m_Window) glfwIconifyWindow(m_Window); }
	void GLFWWindow::Restore()  { if (m_Window) glfwRestoreWindow(m_Window); }
}

#endif // NIGHTBLOOM_PLATFORM_MACOS
