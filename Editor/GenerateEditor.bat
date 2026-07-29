@echo off
REM ---------------------------------------------------------------------------
REM GenerateEditor.bat - generate the Visual Studio solution for Nightbloom.
REM
REM WHY THIS CHANGED (2026-07-18):
REM This used to generate a STANDALONE editor project into Editor\Build. That
REM cannot work: Editor\CMakeLists.txt links NightbloomEngine as a direct target
REM reference, which only exists inside the root CMake project. Standalone, no
REM engine include directories propagated and every "Engine/..." header failed
REM to resolve. It also died earlier at configure time, because the shader
REM helper nightbloom_compile_shaders() is only included by the root.
REM
REM There is one solution now and it contains everything - engine, editor, game
REM and tests. Set NightbloomEditor as the startup project (the root CMakeLists
REM already does this by default).
REM ---------------------------------------------------------------------------
setlocal

set "GENERATOR=Visual Studio 18 2026"
set "ROOT=%~dp0.."

echo Generating the Nightbloom solution (engine + editor + game)...
REM No -A: the generator defaults to x64, and an explicit platform conflicts
REM with an existing cache generated without one.
cmake -S "%ROOT%" -B "%ROOT%\Build" -G "%GENERATOR%"

if errorlevel 1 (
    echo.
    echo CMake generation FAILED - see the output above.
    pause
    exit /b 1
)

echo.
echo Solution generated: Build\NightbloomWorkspace.slnx
echo Open it in Visual Studio, or build from the command line with:
echo    BuildAll.bat [Debug^|Release]
echo.
pause
