# SmartCity PowerShell Startup Script
$ErrorActionPreference = "Stop"

Set-Location -Path $PSScriptRoot

Write-Host "===================================================" -ForegroundColor Cyan
Write-Host "            Starting SmartCity Server              " -ForegroundColor Cyan
Write-Host "===================================================" -ForegroundColor Cyan

# Ensure MinGW runtime DLLs are on PATH
$env:PATH = "C:\msys64\mingw64\bin;" + $env:PATH

# Check if binary exists; compile if missing
if (-not (Test-Path "build\src\smartcity_server.exe")) {
    Write-Host "[INFO] Binary missing. Compiling with tools/build_native.py..." -ForegroundColor Yellow
    python tools\build_native.py
}

Write-Host "`nServer running at: http://localhost:8080" -ForegroundColor Green
Write-Host "Press Ctrl+C to stop the server.`n" -ForegroundColor DarkGray

& ".\build\src\smartcity_server.exe"

