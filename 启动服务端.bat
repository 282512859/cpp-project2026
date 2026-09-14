@echo off
setlocal

rem ASCII-only batch file for reliable execution in cmd.exe.
cd /d "%~dp0"

rem Prefer the Release build: it contains the extended document-management features
rem and its PBKDF2 password hashing is fast enough for the client's 15s socket
rem timeout (a Qt Debug build can take far longer than 15s and makes the client
rem look like it cannot connect). The Debug build stays as a fallback.
set "SERVER_EXE=out\build\windows-release-qt\server\Release\cloud_server.exe"
if not exist "%SERVER_EXE%" set "SERVER_EXE=out\build\windows-debug-qt\server\Debug\cloud_server.exe"

if not exist "%SERVER_EXE%" (
    echo [ERROR] Server executable not found: %SERVER_EXE%
    echo Build the cloud_server target first.
    pause
    exit /b 1
)

netstat -ano -p tcp | findstr /r /c:":9000 .*LISTENING" >nul
if not errorlevel 1 (
    echo [INFO] TCP port 9000 is already in use.
    echo Stop the existing server before starting another one.
    pause
    exit /b 1
)

echo Starting LanCloudDrive server...
echo Executable: %SERVER_EXE%
echo Listening on: 0.0.0.0:9000
echo Runtime data: %CD%\runtime
echo.
echo Keep this window open. Press Ctrl+C to stop the server.
echo.
"%SERVER_EXE%" 9000 ".\runtime"

echo.
echo Server stopped. Exit code: %ERRORLEVEL%
pause