@echo off
chcp 65001 >nul 2>&1
title NEVO Web Management
echo.
echo ================================================
echo   NEVO Server Management Web Interface
echo ================================================
echo.
echo   Starting web proxy on http://127.0.0.1:8090
echo   Connecting to ControlServer on tcp://127.0.0.1:24432
echo.
echo   Make sure nevo_server.exe is running first!
echo ================================================
echo.
cd /d "%~dp0"

:: Launch the Python web proxy in a new minimized window
start "NEVO Web Proxy" /MIN python server.py

:: Wait for the server to start listening
echo   Waiting for web server to start...
:wait_loop
timeout /t 1 /nobreak >nul
curl -s http://127.0.0.1:8090/api/health >nul 2>&1
if errorlevel 1 goto wait_loop

:: Open default browser
echo   Opening browser...
start "" http://127.0.0.1:8090

echo.
echo   Web UI is running. Close the NEVO Web Proxy window to stop.
echo ================================================
pause