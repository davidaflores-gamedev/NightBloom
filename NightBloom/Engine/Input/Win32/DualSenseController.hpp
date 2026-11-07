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
// Multiple pads: DualSensePool owns one DualSenseController per physical device
// (keyed by HID device path). Each controller claims its InputSystem slot with
// AcquireGamepadSlot() when it opens and releases it on unplug, so two DualSenses,
// or a DualSense plus Xbox pads, each get their own player slot.
//
// Scope (deliberately "basic"): USB only, digital buttons + analog sticks +
// analog triggers. The class is structured so the following can be layered on
// later WITHOUT touching InputSystem:
//   - Bluetooth (same parser at a +1 byte offset, plus the 0x05 feature-report
//     handshake and CRC-32 append on output reports)
//   - Gyro / accelerometer (report bytes ~16-31), touchpad (bytes ~33+), battery
//   - Output reports: rumble, adaptive-trigger effects, lightbar/player LEDs
//     (the device is already opened GENERIC_READ|GENERIC_WRITE for this)
//------------------------------------------------------------------------------
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace Nightbloom
{
	class InputSystem;

	// One physical DualSense.
	class DualSenseController
	{
	public:
		DualSenseController() = default;
		~DualSenseController();
		DualSenseController(const DualSenseController&) = delete;
		DualSenseController& operator=(const DualSenseController&) = delete;

		// Open the HID device at `devicePath`, confirm it is a DualSense, and claim
		// an InputSystem gamepad slot. Returns false (and holds nothing) if the
		// device can't be opened, isn't a DualSense, or every slot is taken.
		bool Open(const std::wstring& devicePath, InputSystem* input);

		// Drain pending HID input reports (keeping the freshest) and push
		// button/axis edges into `input`. Never blocks the frame: reads are
		// overlapped and checked non-blockingly. Returns false once the device is
		// gone, after releasing its slot.
		bool Poll(InputSystem* input);

		// Release the HID handle without emitting a disconnect. Safe to repeat.
		void Shutdown();

		const std::wstring& DevicePath() const { return m_Path; }
		int Slot() const { return m_Slot; }

	private:
		void Close(InputSystem* input);   // input may be null (shutdown path: no disconnect event)
		void ProcessReport(const unsigned char* report, unsigned long len, InputSystem* input);

	private:
		// Kept as void* so this header stays free of <windows.h>; the .cpp casts them
		// back to HANDLE / OVERLAPPED*.
		void* m_Handle = nullptr;       // HID device HANDLE (null when closed)
		void* m_Overlapped = nullptr;   // OVERLAPPED* for the non-blocking read

		std::wstring m_Path;
		int  m_Slot = -1;               // InputSystem gamepad slot (-1 = none)
		bool m_ReadPending = false;     // an overlapped ReadFile is in flight into m_ReadBuf

		static constexpr int kReportSize = 64;   // DualSense USB input report length
		unsigned char m_ReadBuf[kReportSize] = {};

		// Previous button state as a bitmask indexed by our Gamepad_* offset (see the
		// offset table in the .cpp), for edge detection like the keyboard/XInput paths.
		unsigned int m_PrevButtons = 0;
	};

	// Every connected DualSense. Win32Window polls it each frame and tells it when
	// Windows reports a device change (WM_DEVICECHANGE); it re-enumerates HID
	// devices only then (plus once at startup), not on a timer.
	class DualSensePool
	{
	public:
		void Poll(InputSystem* input);

		// A device was added or removed somewhere. Schedules a few rescans spread
		// over the next couple of seconds, because the HID interface can show up a
		// little after the device-change message.
		void NotifyDevicesChanged();

		void Shutdown();

	private:
		void Scan(InputSystem* input);

		std::vector<std::unique_ptr<DualSenseController>> m_Pads;
		int m_ScansPending = 1;          // 1 = scan on the first poll
		int m_FramesUntilScan = 0;
	};
}
