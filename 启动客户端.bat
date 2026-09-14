@echo off
setlocal

rem ASCII-only batch file for reliable execution in cmd.exe.
cd /d "%~dp0"

set "CLIENT_EXE=out\build\windows-debug-qt\client_qt\Debug\client_qt_example.exe"

if not exist "%CLIENT_EXE%" (
    echo [ERROR] Qt client executable not found: %CLIENT_EXE%
    echo Build the client_qt_example target first.
    pause
    exit /b 1
)

echo Starting LanCloudDrive client...
start "LanCloudDrive Client" /d "%CD%" "%CD%\%CLIENT_EXE%"

endlocal
