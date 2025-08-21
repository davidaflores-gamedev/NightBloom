//------------------------------------------------------------------------------
// InputSystem.cpp
//
// Unified input system implementation
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#include "InputSystem.hpp"
#include "Core/Logger/Logger.hpp"
#include <Windows.h>  // For VK_ codes
#include <algorithm>

namespace Nightbloom
{
    InputSystem::InputSystem()
        : m_IsShuttingDown(false)
    {
        // Mark keyboard and mouse as always connected
        m_DevicesConnected.set(static_cast<size_t>(InputDevice::Keyboard));
        m_DevicesConnected.set(static_cast<size_t>(InputDevice::Mouse));
    }

    InputSystem::~InputSystem()
    {
        Shutdown();
    }

    void InputSystem::Shutdown()
    {
        if (m_IsShuttingDown)
            return;

        m_IsShuttingDown = true;

        // Clear all state safely
        m_InputsDown.reset();
        m_InputsPressed.reset();
        m_InputsReleased.reset();

        for (auto& axis : m_Axes)
        {
            axis.value = 0.0f;
            axis.lastValue = 0.0f;
            axis.delta = 0.0f;
        }

        // Don't touch the queue during shutdown - just let it be destroyed
        LOG_TRACE("InputSystem shutting down");
    }

    void InputSystem::BeginFrame()
    {
        if (m_IsShuttingDown)
            return;

        // Clear single-frame states
        m_InputsPressed.reset();
        m_InputsReleased.reset();

        // Update axis deltas and save previous values
        for (auto& axis : m_Axes)
        {
            axis.lastValue = axis.value;
            axis.delta = 0.0f;  // Reset delta, will be recalculated during frame
        }

        // Reset mouse wheel (it's an event-based axis)
        m_Axes[static_cast<size_t>(AxisCode::Mouse_Wheel)].value = 0.0f;

        // Process any queued events
        ProcessEventQueue();
    }

    void InputSystem::EndFrame()
    {
        // Calculate final axis deltas
        for (auto& axis : m_Axes)
        {
            axis.delta = axis.value - axis.lastValue;
        }
    }

    //--------------------------------------------------------------------------
    // Keyboard input
    //--------------------------------------------------------------------------

    void InputSystem::OnKeyDown(unsigned int winKeyCode)
    {
        InputCode code = VirtualKeyToInputCode(winKeyCode);
        if (code == InputCode::None) return;

        size_t index = static_cast<size_t>(code);
        if (index >= MAX_INPUTS) return;

        // Queue event
        InputEvent event;
        event.type = InputEvent::ButtonPressed;
        event.button.code = code;
        event.device = InputDevice::Keyboard;
        QueueEvent(event);

        // Update state for polling
        if (!m_InputsDown[index])  // Only register if wasn't already down
        {
            m_InputsDown[index] = true;
            m_InputsPressed[index] = true;

            LOG_TRACE("Key pressed: {} (VK: 0x{:X})", static_cast<int>(code), winKeyCode);
        }
    }

    void InputSystem::OnKeyUp(unsigned int winKeyCode)
    {
        InputCode code = VirtualKeyToInputCode(winKeyCode);
        if (code == InputCode::None) return;

        size_t index = static_cast<size_t>(code);
        if (index >= MAX_INPUTS) return;

        // Queue event
        InputEvent event;
        event.type = InputEvent::ButtonReleased;
        event.button.code = code;
        event.device = InputDevice::Keyboard;
        QueueEvent(event);

        // Update state for polling
        if (m_InputsDown[index])  // Only register if was down
        {
            m_InputsDown[index] = false;
            m_InputsReleased[index] = true;

            LOG_TRACE("Key released: {} (VK: 0x{:X})", static_cast<int>(code), winKeyCode);
        }
    }

    void InputSystem::OnChar(unsigned int charCode)
    {
        // Queue text input event
        InputEvent event;
        event.type = InputEvent::TextInput;
        event.text.character = charCode;
        event.device = InputDevice::Keyboard;
        QueueEvent(event);

        LOG_TRACE("Character input: {}", static_cast<char>(charCode));
    }

    //--------------------------------------------------------------------------
    // Mouse input
    //--------------------------------------------------------------------------

    void InputSystem::OnMouseButtonDown(int button)
    {
        InputCode code = MouseButtonToInputCode(button);
        if (code == InputCode::None) return;

        size_t index = static_cast<size_t>(code);
        if (index >= MAX_INPUTS) return;

        // Queue event
        InputEvent event;
        event.type = InputEvent::ButtonPressed;
        event.button.code = code;
        event.device = InputDevice::Mouse;
        QueueEvent(event);

        // Update state
        if (!m_InputsDown[index])
        {
            m_InputsDown[index] = true;
            m_InputsPressed[index] = true;

            LOG_TRACE("Mouse button pressed: {}", button);
        }
    }

    void InputSystem::OnMouseButtonUp(int button)
    {
        InputCode code = MouseButtonToInputCode(button);
        if (code == InputCode::None) return;

        size_t index = static_cast<size_t>(code);
        if (index >= MAX_INPUTS) return;

        // Queue event
        InputEvent event;
        event.type = InputEvent::ButtonReleased;
        event.button.code = code;
        event.device = InputDevice::Mouse;
        QueueEvent(event);

        // Update state
        if (m_InputsDown[index])
        {
            m_InputsDown[index] = false;
            m_InputsReleased[index] = true;

            LOG_TRACE("Mouse button released: {}", button);
        }
    }

    void InputSystem::OnMouseMove(int x, int y)
    {
        auto& xAxis = m_Axes[static_cast<size_t>(AxisCode::Mouse_X)];
        auto& yAxis = m_Axes[static_cast<size_t>(AxisCode::Mouse_Y)];

        // Calculate deltas
        float deltaX = static_cast<float>(x) - xAxis.value;
        float deltaY = static_cast<float>(y) - yAxis.value;

        // Update values
        xAxis.value = static_cast<float>(x);
        yAxis.value = static_cast<float>(y);
        xAxis.delta = deltaX;
        yAxis.delta = deltaY;

        // Queue event
        InputEvent event;
        event.type = InputEvent::AxisMoved;
        event.axis.axis = AxisCode::Mouse_X;
        event.axis.value = xAxis.value;
        event.axis.delta = deltaX;
        event.device = InputDevice::Mouse;
        QueueEvent(event);

        // Queue Y axis event too
        event.axis.axis = AxisCode::Mouse_Y;
        event.axis.value = yAxis.value;
        event.axis.delta = deltaY;
        QueueEvent(event);
    }

    void InputSystem::OnMouseWheel(float delta)
    {
        auto& wheelAxis = m_Axes[static_cast<size_t>(AxisCode::Mouse_Wheel)];

        // Mouse wheel is additive during the frame
        wheelAxis.value += delta;
        wheelAxis.delta = delta;

        // Queue event
        InputEvent event;
        event.type = InputEvent::AxisMoved;
        event.axis.axis = AxisCode::Mouse_Wheel;
        event.axis.value = wheelAxis.value;
        event.axis.delta = delta;
        event.device = InputDevice::Mouse;
        QueueEvent(event);

        LOG_TRACE("Mouse wheel: {}", delta);
    }

    //--------------------------------------------------------------------------
    // Gamepad input (placeholder for future implementation)
    //--------------------------------------------------------------------------

    void InputSystem::OnGamepadConnected(int gamepadIndex)
    {
        if (gamepadIndex < 0 || gamepadIndex > 3) return;

        InputDevice device = static_cast<InputDevice>(
            static_cast<int>(InputDevice::Gamepad1) + gamepadIndex);
        m_DevicesConnected.set(static_cast<size_t>(device));

        LOG_INFO("Gamepad {} connected", gamepadIndex);
    }

    void InputSystem::OnGamepadDisconnected(int gamepadIndex)
    {
        if (gamepadIndex < 0 || gamepadIndex > 3) return;

        InputDevice device = static_cast<InputDevice>(
            static_cast<int>(InputDevice::Gamepad1) + gamepadIndex);
        m_DevicesConnected.reset(static_cast<size_t>(device));

        // Clear any pressed buttons for this gamepad
        ClearDevice(device);

        LOG_INFO("Gamepad {} disconnected", gamepadIndex);
    }

    void InputSystem::OnGamepadButton(int gamepadIndex, int button, bool pressed)
    {
        // TODO: Map button index to InputCode based on gamepad index
        // For now, this is a placeholder
        (void)gamepadIndex;
        (void)button;
        (void)pressed;
    }

    void InputSystem::OnGamepadAxis(int gamepadIndex, int axis, float value)
    {
        // TODO: Map axis index to AxisCode based on gamepad index
        // Apply deadzone, normalize, etc.
        (void)gamepadIndex;
        (void)axis;
        (void)value;
    }

    //--------------------------------------------------------------------------
    // Polling API
    //--------------------------------------------------------------------------

    bool InputSystem::IsDown(InputCode code) const
    {
        size_t index = static_cast<size_t>(code);
        if (index >= MAX_INPUTS) return false;
        return m_InputsDown[index];
    }

    bool InputSystem::IsPressed(InputCode code) const
    {
        size_t index = static_cast<size_t>(code);
        if (index >= MAX_INPUTS) return false;
        return m_InputsPressed[index];
    }

    bool InputSystem::IsReleased(InputCode code) const
    {
        size_t index = static_cast<size_t>(code);
        if (index >= MAX_INPUTS) return false;
        return m_InputsReleased[index];
    }

    float InputSystem::GetAxis(AxisCode axis) const
    {
        size_t index = static_cast<size_t>(axis);
        if (index >= static_cast<size_t>(AxisCode::Count)) return 0.0f;
        return m_Axes[index].value;
    }

    float InputSystem::GetAxisDelta(AxisCode axis) const
    {
        size_t index = static_cast<size_t>(axis);
        if (index >= static_cast<size_t>(AxisCode::Count)) return 0.0f;
        return m_Axes[index].delta;
    }

    bool InputSystem::IsDeviceConnected(InputDevice device) const
    {
        size_t index = static_cast<size_t>(device);
        if (index >= static_cast<size_t>(InputDevice::Count)) return false;
        return m_DevicesConnected[index];
    }

    bool InputSystem::IsAnyDown() const
    {
        return m_InputsDown.any();
    }

    bool InputSystem::IsAnyPressed() const
    {
        return m_InputsPressed.any();
    }

    void InputSystem::ClearState()
    {
        if (m_IsShuttingDown)
            return;

        m_InputsDown.reset();
        m_InputsPressed.reset();
        m_InputsReleased.reset();

        for (auto& axis : m_Axes)
        {
            axis.value = 0.0f;
            axis.lastValue = 0.0f;
            axis.delta = 0.0f;
        }

        // Clear event queue safely
        if (!m_IsShuttingDown)
        {
            std::queue<InputEvent> emptyQueue;
            std::swap(m_EventQueue, emptyQueue);
        }

        LOG_TRACE("Input state cleared");
    }

    void InputSystem::ClearDevice(InputDevice device)
    {
        // This would clear inputs specific to a device
        // For now, simplified implementation
        if (device == InputDevice::Mouse)
        {
            // Clear mouse buttons
            m_InputsDown[static_cast<size_t>(InputCode::Mouse_Left)] = false;
            m_InputsDown[static_cast<size_t>(InputCode::Mouse_Right)] = false;
            m_InputsDown[static_cast<size_t>(InputCode::Mouse_Middle)] = false;
            m_InputsDown[static_cast<size_t>(InputCode::Mouse_X1)] = false;
            m_InputsDown[static_cast<size_t>(InputCode::Mouse_X2)] = false;
        }
        // TODO: Add gamepad clearing when implemented
    }

    //--------------------------------------------------------------------------
    // Internal helpers
    //--------------------------------------------------------------------------

    InputCode InputSystem::VirtualKeyToInputCode(unsigned int vk) const
    {
        // Direct mapping for letters and numbers
        if (vk >= 'A' && vk <= 'Z')
        {
            return static_cast<InputCode>(vk);  // Maps to Key_A through Key_Z
        }
        if (vk >= '0' && vk <= '9')
        {
            return static_cast<InputCode>(vk);  // Maps to Key_0 through Key_9
        }

        // Special keys
        switch (vk)
        {
        case VK_ESCAPE: return InputCode::Key_Escape;
        case VK_TAB: return InputCode::Key_Tab;
        case VK_CAPITAL: return InputCode::Key_CapsLock;
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT: return InputCode::Key_Shift;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL: return InputCode::Key_Control;
        case VK_MENU:    // Alt key
        case VK_LMENU:
        case VK_RMENU: return InputCode::Key_Alt;
        case VK_SPACE: return InputCode::Key_Space;
        case VK_RETURN: return InputCode::Key_Enter;
        case VK_BACK: return InputCode::Key_Backspace;
        case VK_LEFT: return InputCode::Key_Left;
        case VK_UP: return InputCode::Key_Up;
        case VK_RIGHT: return InputCode::Key_Right;
        case VK_DOWN: return InputCode::Key_Down;

            // Function keys
        case VK_F1: return InputCode::Key_F1;
        case VK_F2: return InputCode::Key_F2;
        case VK_F3: return InputCode::Key_F3;
        case VK_F4: return InputCode::Key_F4;
        case VK_F5: return InputCode::Key_F5;
        case VK_F6: return InputCode::Key_F6;
        case VK_F7: return InputCode::Key_F7;
        case VK_F8: return InputCode::Key_F8;
        case VK_F9: return InputCode::Key_F9;
        case VK_F10: return InputCode::Key_F10;
        case VK_F11: return InputCode::Key_F11;
        case VK_F12: return InputCode::Key_F12;

            // Add more mappings as needed
        default:
            // Unknown key - you might want to handle this differently
            LOG_TRACE("Unknown VK code: 0x{:X}", vk);
            return InputCode::None;
        }
    }

    InputCode InputSystem::MouseButtonToInputCode(int button) const
    {
        switch (button)
        {
        case 0: return InputCode::Mouse_Left;
        case 1: return InputCode::Mouse_Right;
        case 2: return InputCode::Mouse_Middle;
        case 3: return InputCode::Mouse_X1;
        case 4: return InputCode::Mouse_X2;
        default: return InputCode::None;
        }
    }

    void InputSystem::ProcessEventQueue()
    {
        // For now, we just clear the queue since we're not using events yet
        // Later, this is where we'd dispatch events to registered callbacks

        /*
        while (!m_EventQueue.empty())
        {
            const InputEvent& event = m_EventQueue.front();

            // Future: Dispatch to callbacks here
            // for (auto& callback : m_Callbacks) {
            //     if (callback(event))
            //         break;  // Event consumed
            // }

            m_EventQueue.pop();
        }
        */

        // For now, just clear it
        while (!m_EventQueue.empty())
        {
            m_EventQueue.pop();
        }
    }

    void InputSystem::QueueEvent(const InputEvent& event)
    {
        m_EventQueue.push(event);

        // Later we might want to limit queue size or process immediately
        // if (m_EventQueue.size() > MAX_EVENTS) { /* handle overflow */ }
    }
}