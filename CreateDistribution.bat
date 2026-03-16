@echo off
echo ========================================
echo Creating Nightbloom Distribution
echo ========================================

set DIST_DIR=Distribution
set BUILD_CONFIG=Release

REM Clean old distribution
if exist "%DIST_DIR%" rd /s /q "%DIST_DIR%"
mkdir "%DIST_DIR%"

echo.
echo Step 1: Building Release...
echo ========================================

cd Build
cmake --build . --config %BUILD_CONFIG%
cd ..

echo.
echo Step 2: Copying files...
echo ========================================

REM Copy executable
copy "Build\Editor\%BUILD_CONFIG%\Editor.exe" "%DIST_DIR%\"

REM Copy shaders
mkdir "%DIST_DIR%\Shaders"
copy "Build\Editor\%BUILD_CONFIG%\Shaders\*.spv" "%DIST_DIR%\Shaders\"

REM Copy assets
if exist "Editor\Assets" (
    xcopy "Editor\Assets" "%DIST_DIR%\Assets\" /E /I /Y
)

echo.
echo ========================================
echo Distribution created in: %DIST_DIR%
echo ========================================
echo.
echo Contents:
dir /B "%DIST_DIR%"
echo.
pause