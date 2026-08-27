@echo off
setlocal EnableExtensions

if defined VCPKG_ROOT if exist "%VCPKG_ROOT%\vcpkg.exe" goto :ready

set "VCPKG_ROOT=%~dp0vcpkg"
if exist "%VCPKG_ROOT%\vcpkg.exe" goto :ready

where git >nul 2>nul
if errorlevel 1 (
  echo [ERROR] Git was not found. Install Git for Windows or set VCPKG_ROOT.
  exit /b 1
)

if not exist "%VCPKG_ROOT%\.git" (
  echo [INFO] Downloading vcpkg into "%VCPKG_ROOT%"...
  git clone --depth 1 https://github.com/microsoft/vcpkg "%VCPKG_ROOT%"
  if errorlevel 1 exit /b 1
)

call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
if errorlevel 1 exit /b 1

:ready
endlocal & set "VCPKG_ROOT=%VCPKG_ROOT%"
exit /b 0
