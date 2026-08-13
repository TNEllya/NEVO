@echo off
chcp 65001 >nul 2>&1
title NEVO Web Client
setlocal

:: Default port (override with: set NEVO_WEB_PORT=9000 && start_webclient.bat)
if "%NEVO_WEB_PORT%"=="" set NEVO_WEB_PORT=8088
if "%NEVO_WEB_HOST%"=="" set NEVO_WEB_HOST=127.0.0.1

echo.
echo ================================================
echo   NEVO Web Client (Discord-style frontend)
echo ================================================
echo.
echo   Web UI:      http://%NEVO_WEB_HOST%:%NEVO_WEB_PORT%
echo   WebSocket:   ws://%NEVO_WEB_HOST%:%NEVO_WEB_PORT%/ws
echo   NEVO server: tcp://127.0.0.1:24430 (control)
echo.
echo   Make sure nevo_server.exe is running first!
echo   (The gateway reuses NevoClient from
echo    src\client\gui_python\nevo_client.py)
echo ================================================
echo.

:: Verify python is on PATH
where python >nul 2>&1
if errorlevel 1 (
    echo   [ERROR] Python not found on PATH.
    echo          Install Python 3.10+ or add it to PATH, then retry.
    echo.
    pause
    exit /b 1
)

cd /d "%~dp0"

:: Launch the Python WebSocket gateway (also serves static files)
python gateway.py

echo.
echo   Gateway stopped.
pause
