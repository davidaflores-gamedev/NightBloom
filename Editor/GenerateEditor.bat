@echo off
echo Generating Nightbloom Editor project files...

REM First check if engine is built
if not exist "..\NightBloom\Build\NightbloomEngineConfig.cmake" (
    echo ERROR: Engine must be built first!
    echo Please run GenerateProject.bat in NightBloom folder
    pause
    exit /b 1
)

if not exist "Build" mkdir Build
cd Build

cmake .. -G "Visual Studio 18 2026" -A x64

if %ERRORLEVEL% NEQ 0 (
    echo CMake generation failed!
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo Editor project generated successfully!
echo Open Build\NightbloomEditor.sln in Visual Studio
pause