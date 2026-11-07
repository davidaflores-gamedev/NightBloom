//------------------------------------------------------------------------------
// EntryPoint.hpp
//
// The standard `main` for a NightBloom exe. Include it in exactly ONE .cpp of
// the exe target and define Nightbloom::CreateApplication() somewhere in it.
//
// Game/ and StolenOrbit/ each still carry their own identical Main.cpp; they
// can switch to this (a one-line Main.cpp) whenever they're next touched.
// Release builds use /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup, so `main` is the
// entry point in every configuration and no WinMain variant is needed.
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Core/Application.hpp"

int main(int, char**)
{
	Nightbloom::Application* app = Nightbloom::CreateApplication();
	app->Run();
	delete app;
	return 0;
}
