@echo off
setlocal

rem 始终从本脚本所在的项目根目录启动，避免 CMD 当前目录影响路径。
cd /d "%~dp0"

set "CLIENT_EXE=out\build\windows-debug-qt\client_qt\Debug\client_qt_example.exe"

if not exist "%CLIENT_EXE%" (
    echo [错误] 未找到 Qt 客户端程序：%CLIENT_EXE%
    echo 请先在 VS Code 或 CMake 中编译 client_qt_example 目标。
    pause
    exit /b 1
)

echo 正在启动 LanCloudDrive 客户端...
start "LanCloudDrive Client" /d "%CD%" "%CLIENT_EXE%"

endlocal
