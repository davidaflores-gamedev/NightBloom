@echo off
echo Building NightBloom Engine and Editor...

echo.
echo ========================================
echo Step 1: Building Engine
echo ========================================
cd NightBloom\Scripts
call GenerateProject.bat
cd ..\Build
cmake --build . --config Debug

echo.
echo ========================================
echo Step 2: Building Editor
echo ========================================
cd ..\..\Editor
call GenerateEditor.bat
cd Build
cmake --build . --config Debug

echo.
echo ========================================
echo All builds complete!
echo ========================================
echo.
echo You can now run:
echo   - Editor\Build\bin\Debug\Editor.exe
echo.
pause