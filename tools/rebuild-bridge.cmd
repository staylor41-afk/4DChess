@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
if errorlevel 1 exit /b 1
cl /std:c++20 /EHsc /O2 /I include src\engine.cpp src\bridge_main.cpp /Fe:build-cl\eightd_bridge_hotfix.exe ws2_32.lib
