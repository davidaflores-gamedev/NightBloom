//------------------------------------------------------------------------------
// InputSystem.hpp
//
// Unified input system for game engine - polling-based with future event support
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#pragma once

#include <bitset>
#include <array>
#include <queue>
#include <functional>
#include <unordered_map>

namespace Nightbloom
{
	// Unified input code system - everything is just an "input"
	enum class InputCode : unsigned int
	{
		// Invalid
		None = 0,

		// Keyboard - Letters
		Key_A = 'A', Key_B = 'B', Key_C = 'C', Key_D = 'D', Key_E = 'E',
		Key_F = 'F', Key_G = 'G', Key_H = 'H', Key_I = 'I', Key_J = 'J',
		Key_K = 'K', Key_L = 'L', Key_M = 'M', Key_N = 'N', Key_O = 'O',
		Key_P = 'P', Key_Q = 'Q', Key_R = 'R', Key_S = 'S', Key_T = 'T',
		Key_U = 'U', Key_V = 'V', Key_W = 'W', Key_X = 'X', Key_Y = 'Y',
		Key_Z = 'Z',

		// Keyboard - Numbers
		Key_0 = '0', Key_1 = '1', Key_2 = '2', Key_3 = '3', Key_4 = '4',
		Key_5 = '5', Key_6 = '6', Key_7 = '7', Key_8 = '8', Key_9 = '9',

		// Keyboard - Function keys
		Key_F1 = 0x70, Key_F2, Key_F3, Key_F4, Key_F5, Key_F6,
		Key_F7, Key_F8, Key_F9, Key_F10, Key_F11, Key_F12,

		// Keyboard - Control keys
		Key_Escape = 0x1B,
		Key_Tab = 0x09,
		Key_CapsLock = 0x14,
		Key_Shift = 0x10,
		Key_Control = 0x11,
		Key_Alt = 0x12,
		Key_Space = 0x20,
		Key_Enter = 0x0D,
		Key_Backspace = 0x08,

		// Keyboard - Arrow keys
		Key_Left = 0x25,
		Key_Up = 0x26,
		Key_Right = 0x27,
		Key_Down = 0x28,

		// Mouse buttons (0x100 range)
		Mouse_Left = 0x100,
		Mouse_Right,
		Mouse_Middle,
		Mouse_X1,
		Mouse_X2,

		// Gamepad buttons (0x200 range) - Ready for XInput mapping
		Gamepad_A = 0x200,
		Gamepad_B,
		Gamepad_X,
		Gamepad_Y,
		Gamepad_LeftBumper,
		Gamepad_RightBumper,
		Gamepad_LeftTrigger,  // Will be treated as button when > 0.5
		Gamepad_RightTrigger, // Will be treated as button when > 0.5
		Gamepad_LeftStick,    // L3 - clicking the stick
		Gamepad_RightStick,   // R3 - clicking the stick
		Gamepad_DPadUp,
		Gamepad_DPadDown,
		Gamepad_DPadLeft,
		Gamepad_DPadRight,
		Gamepad_Start,
		Gamepad_Back,
		Gamepad_Guide,        // Xbox button

		// Reserve space for multiple gamepads (up to 4)
		Gamepad2_A = 0x220,
		// ... etc

		Count = 0x300  // Adjust as needed
	};

	// Axis codes for analog inputs
	enum class AxisCode : unsigned int
	{
		None = 0,

		// Mouse
		Mouse_X,
		Mouse_Y,
		Mouse_Wheel,

		// Gamepad - sticks
		Gamepad_LeftStickX,
		Gamepad_LeftStickY,
		Gamepad_RightStickX,
		Gamepad_RightStickY,

		// Gamepad - triggers (0.0 to 1.0)
		Gamepad_LeftTrigger,
		Gamepad_RightTrigger,

		// For multiple gamepads
		Gamepad2_LeftStickX,
		// ... etc

		Count
	};

	// Device types for future use
	enum class InputDevice : unsigned int
	{
		Keyboard = 0,
		Mouse = 1,
		Gamepad1 = 2,
		Gamepad2 = 3,
		Gamepad3 = 4,
		Gamepad4 = 5,
		Count
	};

	// Internal event types
	struct InputEvent
	{
		enum Type
		{
			ButtonPressed,
			ButtonReleased,
			AxisMoved,
			TextInput
		};

		Type type;
		union
		{
			struct { InputCode code; } button;
			struct { AxisCode axis; float value; float delta; } axis;
			struct { unsigned int character; } text;
		};

		InputDevice device = InputDevice::Keyboard;
		// float timestamp;  // For later
	};

	class InputSystem
	{
	public:
		InputSystem();
		~InputSystem();

		// Shutdown the input system (called automatically in destructor)
		void Shutdown();

		//----------------------------------------------------------------------
		// Called by Window/Platform layer to feed raw input
		//----------------------------------------------------------------------

		// Keyboard
		void OnKeyDown(unsigned int winKeyCode);
		void OnKeyUp(unsigned int winKeyCode);
		void OnChar(unsigned int charCode);

		// Mouse
		void OnMouseButtonDown(int button);  // 0=left, 1=right, 2=middle, etc
		void OnMouseButtonUp(int button);
		void OnMouseMove(int x, int y);
		void OnMouseWheel(float delta);

		// Gamepad (for later implementation)
		void OnGamepadConnected(int gamepadIndex);
		void OnGamepadDisconnected(int gamepadIndex);
		void OnGamepadButton(int gamepadIndex, int button, bool pressed);
		void OnGamepadAxis(int gamepadIndex, int axis, float value);

		//----------------------------------------------------------------------
		// Frame management
		//----------------------------------------------------------------------
		void BeginFrame();
		void EndFrame();

		//----------------------------------------------------------------------
		// Unified polling API
		//----------------------------------------------------------------------

		// Digital inputs (buttons/keys)
		bool IsDown(InputCode code) const;      // Currently down
		bool IsPressed(InputCode code) const;   // Went down this frame
		bool IsReleased(InputCode code) const;  // Went up this frame

		// Analog inputs (axes)
		float GetAxis(AxisCode axis) const;
		float GetAxisDelta(AxisCode axis) const;  // Change since last frame

		// Mouse helpers (convenience wrappers)
		int GetMouseX() const { return static_cast<int>(GetAxis(AxisCode::Mouse_X)); }
		int GetMouseY() const { return static_cast<int>(GetAxis(AxisCode::Mouse_Y)); }
		int GetMouseDeltaX() const { return static_cast<int>(GetAxisDelta(AxisCode::Mouse_X)); }
		int GetMouseDeltaY() const { return static_cast<int>(GetAxisDelta(AxisCode::Mouse_Y)); }
		float GetMouseWheel() const { return GetAxis(AxisCode::Mouse_Wheel); }

		// Device queries
		bool IsDeviceConnected(InputDevice device) const;
		bool IsAnyDown() const;
		bool IsAnyPressed() const;

		// State management
		void ClearState();
		void ClearDevice(InputDevice device);

		//----------------------------------------------------------------------
		// Future: Action mapping
		//----------------------------------------------------------------------
		// void MapAction(const std::string& action, InputCode code);
		// void MapAxis(const std::string& axis, AxisCode code, float scale = 1.0f);
		// bool IsActionDown(const std::string& action) const;
		// float GetMappedAxis(const std::string& axis) const;

		//----------------------------------------------------------------------
		// Future: Event callbacks
		//----------------------------------------------------------------------
		// using InputCallback = std::function<bool(const InputEvent&)>;
		// void RegisterCallback(InputCallback callback);

	private:
		InputCode VirtualKeyToInputCode(unsigned int vk) const;
		InputCode MouseButtonToInputCode(int button) const;
		void ProcessEventQueue();
		void QueueEvent(const InputEvent& event);

	private:
		// Digital state (buttons/keys)
		static constexpr size_t MAX_INPUTS = 512;  // Enough for all input codes
		std::bitset<MAX_INPUTS> m_InputsDown;
		std::bitset<MAX_INPUTS> m_InputsPressed;
		std::bitset<MAX_INPUTS> m_InputsReleased;

		// Analog state (axes)
		struct AxisState
		{
			float value = 0.0f;
			float lastValue = 0.0f;
			float delta = 0.0f;
		};
		std::array<AxisState, static_cast<size_t>(AxisCode::Count)> m_Axes;

		// Device state
		std::bitset<static_cast<size_t>(InputDevice::Count)> m_DevicesConnected;

		// Event queue for future event system
		std::queue<InputEvent> m_EventQueue;

		// Shutdown flag
		bool m_IsShuttingDown = false;

		// Future: Action mapping
		// std::unordered_map<std::string, std::vector<InputCode>> m_ActionMappings;
		// std::unordered_map<std::string, std::vector<std::pair<AxisCode, float>>> m_AxisMappings;
	};
}