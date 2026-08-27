@echo off
cd /d "%~dp0"
echo Connecting to a server on this computer at 127.0.0.1:9000...
"%~dp0bin\cloud_client.exe" 127.0.0.1 9000
echo.
echo The client stopped. Press any key to close this window.
pause >nul
