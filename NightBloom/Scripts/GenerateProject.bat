@echo off
echo Generating Nightbloom Engine project files...

cd ..
if not exist "Build" mkdir Build
cd Build

cmake .. -G "Visual Studio 17 2022" -A x64

if %ERRORLEVEL% NEQ 0 (
    echo CMake generation failed!
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo Engine project generated successfully!
echo Next: Build Sandbox project
pause