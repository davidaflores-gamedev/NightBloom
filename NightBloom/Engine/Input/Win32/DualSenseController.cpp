//------------------------------------------------------------------------------
// DualSenseController.cpp
//
// See DualSenseController.hpp for the why. This file owns the Win32 HID plumbing
// (enumerate -> open -> overlapped read) and the DualSense USB report parse.
//------------------------------------------------------------------------------

#include "Engine/Input/Win32/DualSenseController.hpp"
#include "Engine/Input/InputSystem.hpp"
#include "Core/Logger/Logger.hpp"

#include <windows.h>
extern "C" {
#include <hidsdi.h>   // HidD_GetHidGuid, HidD_GetAttributes, HIDD_ATTRIBUTES
}
#include <SetupAPI.h>

#include <vector>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "hid.lib")       // HidD_* — links without a CMake edit (matches XInput's pragma)
#pragma comment(lib, "setupapi.lib")  // SetupDi* device enumeration

namespace Nightbloom
{
	namespace
	{
		// Sony vendor ID and the DualSense product IDs we accept.
		constexpr unsigned short kSonyVID = 0x054C;
		constexpr unsigned short kDualSensePID = 0x0CE6;   // DualSense (PS5)
		constexpr unsigned short kDualSenseEdgePID = 0x0DF2;   // DualSense Edge

		// Our contiguous Gamepad_* button offsets (must match the order in
		// InputSystem.hpp's InputCode enum, same table the XInput poller uses). The
		// PlayStation face buttons map to their POSITION-equivalent Xbox slots so a game
		// polling Gamepad_A works with either pad:
		//   Cross->A, Circle->B, Square->X, Triangle->Y.
		enum Offset
		{
			OFF_CROSS = 0,   // A
			OFF_CIRCLE = 1,   // B
			OFF_SQUARE = 2,   // X
			OFF_TRIANGLE = 3,   // Y
			OFF_L1 = 4,   // LeftBumper
			OFF_R1 = 5,   // RightBumper
			OFF_L2 = 6,   // LeftTrigger  (digital, derived from analog > 0.5)
			OFF_R2 = 7,   // RightTrigger (digital, derived from analog > 0.5)
			OFF_L3 = 8,   // LeftStick click
			OFF_R3 = 9,   // RightStick click
			OFF_DPAD_UP = 10,
			OFF_DPAD_DOWN = 11,
			OFF_DPAD_LEFT = 12,
			OFF_DPAD_RIGHT = 13,
			OFF_OPTIONS = 14,   // Start   (Options is the right/menu button)
			OFF_CREATE = 15,   // Back    (Create/Share is the left/select button)
			OFF_PS = 16,   // Guide   (PS/home button)
			OFF_MAX = 16
		};

		// Normalize a raw 0-255 stick pair to [-1,1] with up = +1 (matches the XInput
		// path's convention), applying a radial deadzone rescaled so there's no snap at
		// the edge. DualSense sticks are clean, so a smaller deadzone than XInput's.
		constexpr float kStickDeadzone = 0.08f;

		void NormalizeStick(unsigned char rawX, unsigned char rawY, float& outX, float& outY)
		{
			float x = (static_cast<float>(rawX) - 128.0f) / 127.0f;
			float y = (static_cast<float>(rawY) - 128.0f) / 127.0f;
			x = std::clamp(x, -1.0f, 1.0f);
			y = std::clamp(y, -1.0f, 1.0f);

			float mag = std::sqrt(x * x + y * y);
			if (mag <= kStickDeadzone)
			{
				outX = outY = 0.0f;
				return;
			}
			float capped = std::min(mag, 1.0f);
			float scaled = (capped - kStickDeadzone) / (1.0f - kStickDeadzone);  // 0..1 past deadzone
			float inv = scaled / mag;
			outX = std::clamp(x * inv, -1.0f, 1.0f);
			outY = std::clamp(-(y * inv), -1.0f, 1.0f);  // invert so up = +1
		}
	}

	DualSenseController::~DualSenseController()
	{
		Shutdown();
	}

	void DualSenseController::Shutdown()
	{
		// No InputSystem here — just free the OS handles; don't emit a disconnect edge.
		Close(nullptr);
	}

	bool DualSenseController::TryOpen()
	{
		GUID hidGuid;
		HidD_GetHidGuid(&hidGuid);

		HDEVINFO devInfo = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
			DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
		if (devInfo == INVALID_HANDLE_VALUE)
			return false;

		SP_DEVICE_INTERFACE_DATA iface{};
		iface.cbSize = sizeof(iface);

		bool opened = false;
		for (DWORD idx = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, idx, &iface); ++idx)
		{
			DWORD required = 0;
			SetupDiGetDeviceInterfaceDetailW(devInfo, &iface, nullptr, 0, &required, nullptr);
			if (required == 0)
				continue;

			std::vector<unsigned char> detailBuf(required);
			auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuf.data());
			detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
			if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &iface, detail, required, nullptr, nullptr))
				continue;

			// Open read+write (future rumble/lightbar/trigger output reports), overlapped
			// for non-blocking reads. Fall back to read-only if write access is denied.
			HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
				FILE_FLAG_OVERLAPPED, nullptr);
			if (h == INVALID_HANDLE_VALUE)
			{
				h = CreateFileW(detail->DevicePath, GENERIC_READ,
					FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
					FILE_FLAG_OVERLAPPED, nullptr);
				if (h == INVALID_HANDLE_VALUE)
					continue;
			}

			HIDD_ATTRIBUTES attrs{};
			attrs.Size = sizeof(attrs);
			if (HidD_GetAttributes(h, &attrs) &&
				attrs.VendorID == kSonyVID &&
				(attrs.ProductID == kDualSensePID || attrs.ProductID == kDualSenseEdgePID))
			{
				OVERLAPPED* ov = new OVERLAPPED{};
				ov->hEvent = CreateEventW(nullptr, TRUE /*manual reset*/, FALSE, nullptr);

				m_Handle = h;
				m_Overlapped = ov;
				m_ReadPending = false;
				opened = true;
				LOG_INFO("DualSense connected (HID, USB) VID=0x{:04X} PID=0x{:04X}",
					attrs.VendorID, attrs.ProductID);
				break;
			}

			CloseHandle(h);
		}

		SetupDiDestroyDeviceInfoList(devInfo);
		return opened;
	}

	void DualSenseController::Close(InputSystem* input)
	{
		HANDLE h = static_cast<HANDLE>(m_Handle);
		OVERLAPPED* ov = static_cast<OVERLAPPED*>(m_Overlapped);

		if (h && h != INVALID_HANDLE_VALUE)
		{
			if (m_ReadPending && ov)
				CancelIoEx(h, ov);
			CloseHandle(h);
		}
		if (ov)
		{
			if (ov->hEvent)
				CloseHandle(ov->hEvent);
			delete ov;
		}
		m_Handle = nullptr;
		m_Overlapped = nullptr;
		m_ReadPending = false;

		// Release everything we were holding so nothing sticks "down" after unplug.
		if (m_Connected && input)
		{
			for (int off = 0; off <= OFF_MAX; ++off)
			{
				if (m_PrevButtons & (1u << off))
					input->OnGamepadButton(m_PadIndex, off, false);
			}
			for (int a = 0; a < 6; ++a)
				input->OnGamepadAxis(m_PadIndex, a, 0.0f);
			input->OnGamepadDisconnected(m_PadIndex);
			LOG_INFO("DualSense disconnected");
		}
		m_Connected = false;
		m_PrevButtons = 0;
	}

	void DualSenseController::Poll(InputSystem* input)
	{
		if (!input)
			return;

		if (!m_Connected)
		{
			// Scan for the device only occasionally (enumeration is not free), like the
			// XInput empty-slot rescan.
			if ((m_RescanCounter++ % 120u) != 0u)
				return;
			if (!TryOpen())
				return;
			m_Connected = true;
			m_PrevButtons = 0;
			input->OnGamepadConnected(m_PadIndex);
		}

		HANDLE h = static_cast<HANDLE>(m_Handle);
		OVERLAPPED* ov = static_cast<OVERLAPPED*>(m_Overlapped);

		// Drain queued reports, keeping the freshest. The guard caps work per frame; the
		// loop exits as soon as a read goes asynchronous (nothing more buffered yet).
		for (int guard = 0; guard < 32; ++guard)
		{
			if (!m_ReadPending)
			{
				ResetEvent(ov->hEvent);
				DWORD read = 0;
				if (ReadFile(h, m_ReadBuf, kReportSize, &read, ov))
				{
					// Completed synchronously — data already in m_ReadBuf.
					ProcessReport(m_ReadBuf, read, input);
					continue;
				}
				if (GetLastError() == ERROR_IO_PENDING)
				{
					m_ReadPending = true;
					break;  // will finish on a later frame
				}
				Close(input);  // device likely removed
				return;
			}
			else
			{
				DWORD read = 0;
				if (GetOverlappedResult(h, ov, &read, FALSE /*don't wait*/))
				{
					m_ReadPending = false;
					ProcessReport(m_ReadBuf, read, input);
					continue;
				}
				if (GetLastError() == ERROR_IO_INCOMPLETE)
					break;  // still in flight; check again next frame
				Close(input);
				return;
			}
		}
	}

	void DualSenseController::ProcessReport(const unsigned char* r, unsigned long len, InputSystem* input)
	{
		// USB input report: ID 0x01, at least 11 bytes for the fields we read.
		//   [0] report id (0x01)
		//   [1] LX  [2] LY  [3] RX  [4] RY        sticks, 0-255, 128 center
		//   [5] L2  [6] R2                          analog triggers, 0-255
		//   [7] sequence counter
		//   [8] dpad(bits0-3) Square(0x10) Cross(0x20) Circle(0x40) Triangle(0x80)
		//   [9] L1(0x01) R1(0x02) L2d(0x04) R2d(0x08) Create(0x10) Options(0x20) L3(0x40) R3(0x80)
		//   [10] PS(0x01) TouchpadClick(0x02) Mute(0x04)
		// (gyro/accel, touchpad points, battery follow — parsed later.)
		if (len < 11 || r[0] != 0x01)
			return;

		const unsigned char b0 = r[8];
		const unsigned char b1 = r[9];
		const unsigned char b2 = r[10];

		// D-pad is a hat switch (0=N, clockwise to 7=NW, 8=neutral).
		const unsigned char hat = b0 & 0x0F;
		const bool up = (hat == 0 || hat == 1 || hat == 7);
		const bool right = (hat == 1 || hat == 2 || hat == 3);
		const bool down = (hat == 3 || hat == 4 || hat == 5);
		const bool left = (hat == 5 || hat == 6 || hat == 7);

		const float l2 = r[5] / 255.0f;
		const float r2 = r[6] / 255.0f;

		unsigned int cur = 0;
		auto set = [&cur](int off, bool on) { if (on) cur |= (1u << off); };
		set(OFF_CROSS, (b0 & 0x20) != 0);
		set(OFF_CIRCLE, (b0 & 0x40) != 0);
		set(OFF_SQUARE, (b0 & 0x10) != 0);
		set(OFF_TRIANGLE, (b0 & 0x80) != 0);
		set(OFF_L1, (b1 & 0x01) != 0);
		set(OFF_R1, (b1 & 0x02) != 0);
		set(OFF_L3, (b1 & 0x40) != 0);
		set(OFF_R3, (b1 & 0x80) != 0);
		set(OFF_CREATE, (b1 & 0x10) != 0);
		set(OFF_OPTIONS, (b1 & 0x20) != 0);
		set(OFF_PS, (b2 & 0x01) != 0);
		set(OFF_DPAD_UP, up);
		set(OFF_DPAD_DOWN, down);
		set(OFF_DPAD_LEFT, left);
		set(OFF_DPAD_RIGHT, right);
		// Digital triggers from the analog value, matching the XInput path's 0.5 threshold
		// (rather than the DualSense's early hardware detent bit) for cross-pad consistency.
		set(OFF_L2, l2 > 0.5f);
		set(OFF_R2, r2 > 0.5f);

		// Emit edges only (the poller equivalent of OnKeyDown/Up).
		const unsigned int changed = cur ^ m_PrevButtons;
		if (changed)
		{
			for (int off = 0; off <= OFF_MAX; ++off)
			{
				if (changed & (1u << off))
					input->OnGamepadButton(m_PadIndex, off, (cur & (1u << off)) != 0);
			}
			m_PrevButtons = cur;
		}

		float lx, ly, rx, ry;
		NormalizeStick(r[1], r[2], lx, ly);
		NormalizeStick(r[3], r[4], rx, ry);
		input->OnGamepadAxis(m_PadIndex, 0, lx);
		input->OnGamepadAxis(m_PadIndex, 1, ly);
		input->OnGamepadAxis(m_PadIndex, 2, rx);
		input->OnGamepadAxis(m_PadIndex, 3, ry);
		input->OnGamepadAxis(m_PadIndex, 4, l2);
		input->OnGamepadAxis(m_PadIndex, 5, r2);
	}
}
