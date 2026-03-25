@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 2>nul
cd /d "F:\8d chess"
if not exist build mkdir build
cd build
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto fail
nmake
if errorlevel 1 goto fail
echo === BUILD OK ===
for %%f in (eightd_bridge.exe bridge.exe chess_bridge.exe) do (
  if exist %%f copy /Y %%f "..\chess_engine.exe" && echo Copied %%f to chess_engine.exe
)
if exist puzzle_engine.exe copy /Y puzzle_engine.exe "..\puzzle_engine.exe" && echo Copied puzzle_engine.exe
goto end
:fail
echo === BUILD FAILED ===
exit /b 1
:end
