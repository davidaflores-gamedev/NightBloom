//------------------------------------------------------------------------------
// GLFWWindow.hpp
//
// GLFW-based window implementation (macOS — Vulkan via MoltenVK).
// The whole class is guarded by NIGHTBLOOM_PLATFORM_MACOS so the Windows build
// (which globs all engine sources) compiles this translation unit to nothing.
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Window/Window.hpp"

#ifdef NIGHTBLOOM_PLATFORM_MACOS

struct GLFWwindow;

namespace Nightbloom
{
	class GLFWWindow : public Window
	{
	public:
		explicit GLFWWindow(const WindowDesc& desc);
		virtual ~GLFWWindow();

		// Window interface implementation
		bool IsOpen() const override { return m_IsOpen && !m_ShuttingDown; }
		void PollEvents() override;
		void SwapBuffers() override {}  // Vulkan presents via the swapchain

		// Reported in PIXELS (framebuffer size), which is what the Vulkan
		// swapchain wants. On Retina displays this is commonly 2x the logical
		// window size that GLFW and ImGui use for input/UI coordinates.
		int GetWidth() const override { return m_FramebufferW; }
		int GetHeight() const override { return m_FramebufferH; }
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

		// GLFWwindow* — consumed by VulkanSwapchain::CreateSurface
		// (glfwCreateWindowSurface) and UIManager (ImGui_ImplGlfw_InitForVulkan).
		void* GetNativeHandle() const override { return static_cast<void*>(m_Window); }

		// Always-on mouselook support (see Window.hpp). GLFW's disabled-cursor
		// mode natively provides unbounded virtual coordinates, so the delta is a
		// simple frame-to-frame difference — no recentering dance like Win32.
		void SetRelativeMouseMode(bool enabled) override;
		bool IsRelativeMouseMode() const override { return m_RelativeMouse; }
		void GetRelativeMouseDelta(float& dx, float& dy) const override { dx = m_RelMouseDX; dy = m_RelMouseDY; }

	private:
		void InstallCallbacks();

		GLFWwindow* m_Window = nullptr;
		std::string m_Title;
		int  m_FramebufferW = 0;
		int  m_FramebufferH = 0;
		bool m_IsOpen = false;
		bool m_ShuttingDown = false;
		bool m_VSync = false;

		// Relative mouse mode state
		bool   m_RelativeMouse = false;
		bool   m_RelSeeded = false;   // first frame after enabling reports ~0 delta
		double m_RelLastX = 0.0;
		double m_RelLastY = 0.0;
		float  m_RelMouseDX = 0.0f;
		float  m_RelMouseDY = 0.0f;
	};
}

#endif // NIGHTBLOOM_PLATFORM_MACOS
