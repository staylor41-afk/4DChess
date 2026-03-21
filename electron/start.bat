@echo off
cd /d "%~dp0"
if not exist node_modules (
    echo Installing Electron...
    npm install
)
echo Starting 8D Chess...
npx electron .
