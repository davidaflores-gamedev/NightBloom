//------------------------------------------------------------------------------
// Main.cpp
//
// Entry point for the standalone Nightbloom Game runtime (Milestone 2). Mirrors
// the editor's entry point; the client-provided CreateApplication() returns a
// GameApplication (see GameApp.cpp).
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
	(void)argc; (void)argv;
	auto app = Nightbloom::CreateApplication();
	app->Run();
	delete app;
	return 0;
}
#endif
#else
int main(int argc, char** argv)
{
	(void)argc; (void)argv;
	auto app = Nightbloom::CreateApplication();
	app->Run();
	delete app;
	return 0;
}
#endif
