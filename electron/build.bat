@echo off
REM Build ND Chess Windows installer
REM electron-builder cache lives on F drive to keep C drive free
set ELECTRON_BUILDER_CACHE=F:\8d chess\electron-builder-cache
echo Building ND Chess (cache: %ELECTRON_BUILDER_CACHE%)
call npx electron-builder --win --x64
