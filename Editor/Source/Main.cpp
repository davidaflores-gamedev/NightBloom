//------------------------------------------------------------------------------
// Main.cpp
//
// Entry point for the Nightbloom Sandbox
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#include "Engine/Core/Application.hpp"

Nightbloom::Application* Nightbloom::CreateApplication();

#ifdef NIGHTBLOOM_PLATFORM_WINDOWS
#ifdef NIGHTBLOOM_DIST
#include <Windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	auto app = Nightbloom::CreateApplication();
	app->Run();
	delete app;
	return 0;
}
#else
int main(int argc, char** argv)
{
	auto app = Nightbloom::CreateApplication();
	app->Run();
	delete app;
	return 0;
}
#endif
#else
int main(int argc, char** argv)
{
	auto app = Nightbloom::CreateApplication();
	app->Run();
	delete app;
	return 0;
}
#endif