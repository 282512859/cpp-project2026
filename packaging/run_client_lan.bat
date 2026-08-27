@echo off
setlocal
cd /d "%~dp0"
set /p "SERVER_IP=Enter the server computer IPv4 address: "
if not defined SERVER_IP exit /b 1
"%~dp0bin\cloud_client.exe" "%SERVER_IP%" 9000
echo.
echo The client stopped. Press any key to close this window.
pause >nul
