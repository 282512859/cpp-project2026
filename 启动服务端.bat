@echo off
setlocal

rem 始终从本脚本所在的项目根目录启动，避免 CMD 当前目录影响路径。
cd /d "%~dp0"

set "SERVER_EXE=out\build\windows-debug-qt\server\Debug\cloud_server.exe"

if not exist "%SERVER_EXE%" (
    echo [错误] 未找到服务端程序：%SERVER_EXE%
    echo 请先在 VS Code 或 CMake 中编译 cloud_server 目标。
    pause
    exit /b 1
)

echo 正在启动 LanCloudDrive 服务端...
echo 监听地址：0.0.0.0:9000
echo 运行数据：%CD%\runtime
echo.
echo 请保持此窗口打开。按 Ctrl+C 可停止服务端。
echo.
"%SERVER_EXE%" 9000 ".\runtime"

echo.
echo 服务端已停止，退出代码：%ERRORLEVEL%
pause
