@echo off
REM =============================================================================
REM Setup.bat - Run this ONCE after cloning the repository
REM =============================================================================
REM Generates Visual Studio solution for both Engine and Editor
REM =============================================================================

echo.
echo ========================================
echo    Nightbloom Engine Setup
echo ========================================
echo.

REM Check for CMake
where cmake >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] CMake not found in PATH!
    echo.
    echo Please install CMake from: https://cmake.org/download/
    echo Make sure to check "Add CMake to system PATH" during installation.
    pause
    exit /b 1
)
echo [OK] CMake found

REM Check for Vulkan SDK
if "%VULKAN_SDK%"=="" (
    echo [WARNING] VULKAN_SDK environment variable not set
    echo          Shader compilation may fail
) else (
    echo [OK] Vulkan SDK found: %VULKAN_SDK%
)

REM Create build directory
if not exist "Build" mkdir Build
cd Build

REM Generate Visual Studio solution
echo.
echo Generating Visual Studio 2022 solution...
cmake .. -G "Visual Studio 17 2022" -A x64

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] CMake generation failed!
    echo.
    echo Common fixes:
    echo   - Install Visual Studio 2022 with "Desktop development with C++"
    echo   - Install CMake 3.20+
    echo   - Install Vulkan SDK
    cd ..
    pause
    exit /b %ERRORLEVEL%
)

cd ..

echo.
echo ========================================
echo    Setup Complete!
echo ========================================
echo.
echo Open this solution in Visual Studio:
echo   Build\NightbloomWorkspace.sln
echo.
echo Or run this command:
echo   start Build\NightbloomWorkspace.sln
echo.
echo Editor will be set as startup project.
echo Press F5 to build and run!
echo.
pause
