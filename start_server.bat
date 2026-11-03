@echo off
title SmartCity Server
echo ===================================================
echo             Starting SmartCity Server
echo ===================================================

:: Ensure working directory is project root
cd /d "%~dp0"

:: Set MinGW GCC runtime path
set "PATH=C:\msys64\mingw64\bin;%PATH%"

:: Check if server binary exists
if not exist "build\src\smartcity_server.exe" (
    echo [ERROR] Server executable not found at build\src\smartcity_server.exe
    echo Attempting to build the binary using tools\build_native.py...
    python tools\build_native.py
    if errorlevel 1 (
        echo [ERROR] Build failed. Please check compiler setup.
        pause
        exit /b 1
    )
)

echo.
echo Server starting at: http://localhost:8080
echo Press Ctrl+C in this window to stop the server.
echo ===================================================
echo.

build\src\smartcity_server.exe

if errorlevel 1 (
    echo.
    echo [Server exited with an error code: %errorlevel%]
    pause
)

