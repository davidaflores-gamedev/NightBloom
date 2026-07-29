@echo off
REM ---------------------------------------------------------------------------
REM BuildAll.bat - build everything (engine + editor + game + tests) from the
REM unified ROOT CMake project.
REM
REM WHY THIS CHANGED (2026-07-18):
REM This script used to generate and build the Engine and Editor separately,
REM into NightBloom\Build and Editor\Build. That path is dead and cannot work:
REM Editor\CMakeLists.txt links NightbloomEngine as a DIRECT TARGET REFERENCE,
REM which only exists when the root CMakeLists has already added the engine via
REM add_subdirectory. Generated standalone, CMake treated "NightbloomEngine" as
REM a bare library name, no include directories propagated, and every engine
REM header failed to resolve. A second breakage sat on top of it: the shaders
REM helper nightbloom_compile_shaders() is included by the root CMakeLists only,
REM so a standalone Editor generate died at configure time.
REM
REM The root project builds engine, editor and game together and is the
REM documented workflow - see .claude\CLAUDE.md "Build Workflow".
REM
REM Usage:  BuildAll.bat [Debug|Release]     (defaults to Debug)
REM ---------------------------------------------------------------------------
setlocal

set "GENERATOR=Visual Studio 18 2026"
set "CONFIG=Debug"
if not "%~1"=="" set "CONFIG=%~1"

echo ========================================
echo Configuring  (%GENERATOR%, %CONFIG%)
echo ========================================
REM No -A: this generator already defaults to x64, and passing an explicit
REM platform conflicts with an existing cache that was generated without one.
cmake -S "%~dp0." -B "%~dp0Build" -G "%GENERATOR%"
if errorlevel 1 goto :failed

echo.
echo ========================================
echo Building engine + editor + game
echo ========================================
cmake --build "%~dp0Build" --config %CONFIG%
if errorlevel 1 goto :failed

echo.
echo ========================================
echo Build complete  (%CONFIG%)
echo ========================================
echo   Editor: Build\Editor\%CONFIG%\Editor.exe
echo   Game:   Build\Game\%CONFIG%\NightbloomGame.exe
echo   Tests:  Build\bin\%CONFIG%\NightbloomTests.exe
echo.
pause
exit /b 0

:failed
echo.
echo BUILD FAILED - see the output above.
pause
exit /b 1
