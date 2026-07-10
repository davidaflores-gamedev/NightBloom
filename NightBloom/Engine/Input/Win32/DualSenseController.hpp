//------------------------------------------------------------------------------
// DualSenseController.hpp
//
// Native HID backend for the Sony DualSense (PS5) controller over USB.
//
// XInput does NOT surface PlayStation pads (it only enumerates Xbox-compatible
// XUSB devices), so a DualSense plugged in directly appears to Windows as a raw
// HID device. This backend opens that HID device, parses its input reports, and
// feeds the exact same generic InputSystem::OnGamepad* path the XInput poller in
// Win32Window uses — so game code sees one unified gamepad API and a DualSense's
// Cross/Circle/Square/Triangle land on the position-equivalent Gamepad_A/B/X/Y
// slots automatically.
//
// Scope of this first pass (deliberately "basic"): USB only, digital buttons +
// analog sticks + analog triggers. The class is structured so the following can be
// layered on later WITHOUT touching InputSystem:
//   - Bluetooth (same parser at a +1 byte offset, plus the 0x05 feature-report
//     handshake and CRC-32 append on output reports)
//   - Gyro / accelerometer (report bytes ~16-31), touchpad (bytes ~33+), battery
//   - Output reports: rumble, adaptive-trigger effects, lightbar/player LEDs
//     (the device is already opened GENERIC_READ|GENERIC_WRITE for this)
//------------------------------------------------------------------------------
#pragma once

namespace Nightbloom
{
	class InputSystem;

	class DualSenseController
	{
	public:
		DualSenseController() = default;
		~DualSenseController();

		// Called once per frame from Win32Window::PollEvents (right after the XInput
		// poll). Opens the device if needed, drains any pending HID input reports
		// (keeping the freshest), and pushes button/axis edges into `input`. Never
		// blocks the frame — reads are overlapped and checked non-blockingly.
		void Poll(InputSystem* input);

		// Release the HID handle. Safe to call repeatedly.
		void Shutdown();

		bool IsConnected() const { return m_Connected; }

	private:
		bool TryOpen();
		void Close(InputSystem* input);   // input may be null (shutdown path: no disconnect event)
		void ProcessReport(const unsigned char* report, unsigned long len, InputSystem* input);

	private:
		// Kept as void* so this header stays free of <windows.h>; the .cpp casts them
		// back to HANDLE / OVERLAPPED*.
		void* m_Handle = nullptr;       // HID device HANDLE (null when closed)
		void* m_Overlapped = nullptr;   // OVERLAPPED* for the non-blocking read

		bool  m_Connected = false;
		bool  m_ReadPending = false;    // an overlapped ReadFile is in flight into m_ReadBuf
		unsigned int m_RescanCounter = 0;

		static constexpr int kReportSize = 64;   // DualSense USB input report length
		unsigned char m_ReadBuf[kReportSize] = {};

		// Previous button state as a bitmask indexed by our Gamepad_* offset (see the
		// offset table in the .cpp), for edge detection like the keyboard/XInput paths.
		unsigned int m_PrevButtons = 0;

		// Which InputSystem gamepad slot this pad feeds. 0 for now: a lone DualSense
		// takes slot 0 cleanly. If an Xbox pad also occupies XInput index 0 at the same
		// time they collide on this slot — acceptable for the single-controller first
		// pass; proper multi-device slot assignment is deferred.
		int m_PadIndex = 0;
	};
}
