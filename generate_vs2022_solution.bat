@echo off
REM 负责人：成员1：服务端架构/组长
setlocal EnableExtensions
cd /d "%~dp0"
call "%~dp0tools\ensure_vcpkg.bat"
if errorlevel 1 exit /b 1
cmake --preset windows-debug
if errorlevel 1 exit /b 1
echo Visual Studio solution generated at:
echo   %CD%\out\build\windows-debug\LanCloudDrive.sln
start "" "%CD%\out\build\windows-debug\LanCloudDrive.sln"
