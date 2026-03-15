taskkill /IM mspdbsrv.exe /F
del /f /q Build\lib\Debug\NightbloomEngine.pdb
cmake --build Build --config Debug --target NightbloomEngine