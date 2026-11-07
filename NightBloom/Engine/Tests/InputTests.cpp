//------------------------------------------------------------------------------
// InputTests.cpp
//
// Per-pad gamepad input (local multiplayer). Before 2026-11-07, OnGamepadAxis
// dropped every pad but the first, so a second controller could press buttons
// but never move a stick, and an unplugged pad left its buttons stuck down.
//------------------------------------------------------------------------------
#include <gtest/gtest.h>
#include "Engine/Input/InputSystem.hpp"

using namespace Nightbloom;

TEST(GamepadInput, SecondPadStickIsReadable)
{
	InputSystem in;
	in.OnGamepadConnected(1);
	in.OnGamepadAxis(1, static_cast<int>(GamepadAxis::LeftX), 0.75f);
	in.OnGamepadAxis(1, static_cast<int>(GamepadAxis::LeftY), -0.5f);

	EXPECT_FLOAT_EQ(in.GetGamepadAxis(1, GamepadAxis::LeftX), 0.75f);
	EXPECT_FLOAT_EQ(in.GetGamepadAxis(1, GamepadAxis::LeftY), -0.5f);
	EXPECT_TRUE(in.IsGamepadConnected(1));
}

TEST(GamepadInput, PadsDoNotShareAxes)
{
	InputSystem in;
	in.OnGamepadAxis(0, static_cast<int>(GamepadAxis::LeftX), 1.0f);
	in.OnGamepadAxis(1, static_cast<int>(GamepadAxis::LeftX), -1.0f);

	EXPECT_FLOAT_EQ(in.GetGamepadAxis(0, GamepadAxis::LeftX), 1.0f);
	EXPECT_FLOAT_EQ(in.GetGamepadAxis(1, GamepadAxis::LeftX), -1.0f);
	// The legacy single-pad API still reads pad 0 only.
	EXPECT_FLOAT_EQ(in.GetAxis(AxisCode::Gamepad_LeftStickX), 1.0f);
}

TEST(GamepadInput, ButtonsAreReadPerPad)
{
	InputSystem in;
	in.OnGamepadButton(1, 0, true);  // offset 0 = A, on pad 2

	EXPECT_TRUE(in.IsGamepadDown(1, InputCode::Gamepad_A));
	EXPECT_TRUE(in.IsGamepadPressed(1, InputCode::Gamepad_A));
	EXPECT_FALSE(in.IsGamepadDown(0, InputCode::Gamepad_A));
	EXPECT_FALSE(in.IsDown(InputCode::Gamepad_A));
}

TEST(GamepadInput, GamepadButtonMapsIntoEachPadsBlock)
{
	EXPECT_EQ(InputSystem::GamepadButton(0, InputCode::Gamepad_A), InputCode::Gamepad_A);
	EXPECT_EQ(InputSystem::GamepadButton(1, InputCode::Gamepad_A), InputCode::Gamepad2_A);
	EXPECT_EQ(InputSystem::GamepadButton(4, InputCode::Gamepad_A), InputCode::None);
	EXPECT_EQ(InputSystem::GamepadButton(0, InputCode::Key_A), InputCode::None);
}

TEST(GamepadInput, DisconnectReleasesButtonsAndSticks)
{
	InputSystem in;
	in.OnGamepadConnected(1);
	in.OnGamepadButton(1, 0, true);
	in.OnGamepadAxis(1, static_cast<int>(GamepadAxis::LeftX), 1.0f);

	in.OnGamepadDisconnected(1);

	EXPECT_FALSE(in.IsGamepadDown(1, InputCode::Gamepad_A));
	EXPECT_FLOAT_EQ(in.GetGamepadAxis(1, GamepadAxis::LeftX), 0.0f);
	EXPECT_FALSE(in.IsGamepadConnected(1));
}

TEST(GamepadInput, OutOfRangeIsHarmless)
{
	InputSystem in;
	in.OnGamepadAxis(7, 0, 1.0f);
	in.OnGamepadAxis(0, 99, 1.0f);
	EXPECT_FLOAT_EQ(in.GetGamepadAxis(7, GamepadAxis::LeftX), 0.0f);
	EXPECT_FALSE(in.IsGamepadDown(-1, InputCode::Gamepad_A));
}

TEST(GamepadSlots, AcquireHandsOutLowestFreeSlot)
{
	InputSystem in;
	EXPECT_EQ(in.AcquireGamepadSlot(), 0);   // e.g. an Xbox pad
	EXPECT_EQ(in.AcquireGamepadSlot(), 1);   // e.g. a DualSense: its own slot, no collision
	EXPECT_TRUE(in.IsGamepadConnected(0));
	EXPECT_TRUE(in.IsGamepadConnected(1));
	EXPECT_FALSE(in.IsGamepadConnected(2));
}

TEST(GamepadSlots, ReleasedSlotIsReused)
{
	InputSystem in;
	const int a = in.AcquireGamepadSlot();
	const int b = in.AcquireGamepadSlot();
	in.OnGamepadDisconnected(a);
	EXPECT_EQ(in.AcquireGamepadSlot(), a);   // replugged pad takes the gap, not slot 2
	EXPECT_TRUE(in.IsGamepadConnected(b));
}

TEST(GamepadSlots, FullReturnsMinusOne)
{
	InputSystem in;
	for (int i = 0; i < InputSystem::kMaxGamepads; ++i)
		EXPECT_EQ(in.AcquireGamepadSlot(), i);
	EXPECT_EQ(in.AcquireGamepadSlot(), -1);
}
