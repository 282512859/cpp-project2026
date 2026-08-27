@echo off
REM 负责人：成员4：客户端界面
cd /d "%~dp0"
if not exist runtime\storage mkdir runtime\storage
echo Starting LanCloudDrive server on TCP port 9000...
echo Keep this window open while clients are connected.
"%~dp0bin\cloud_server.exe" 9000 "%~dp0runtime"
echo.
echo The server stopped. Press any key to close this window.
pause >nul
