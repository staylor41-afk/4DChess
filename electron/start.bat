@echo off
title 8D Chess
cd /d "%~dp0"

if not exist node_modules (
    echo Installing Electron (first run, takes ~1 min)...
    npm install
    echo.
)

echo Starting 8D Chess...
echo  - Game served at http://127.0.0.1:3000
echo  - C++ engine on http://127.0.0.1:8765 (if chess_engine.exe exists)
echo  - Press F12 in the window for DevTools
echo.
npx electron . --enable-logging
