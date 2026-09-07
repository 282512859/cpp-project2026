@echo off
REM 负责人：成员1：服务端架构/组长
setlocal EnableExtensions

set "VCPKG_BASELINE=b1b19307e2d2ec1eefbdb7ea069de7d4bcd31f01"
set "VCPKG_ROOT=%~dp0vcpkg"

where git >nul 2>nul
if errorlevel 1 (
  echo [ERROR] Git was not found. Install Git for Windows or set VCPKG_ROOT.
  exit /b 1
)

if not exist "%VCPKG_ROOT%\.git" (
  echo [INFO] Downloading vcpkg into "%VCPKG_ROOT%"...
  git clone --no-checkout --filter=blob:none https://github.com/microsoft/vcpkg "%VCPKG_ROOT%"
  if errorlevel 1 exit /b 1
)

git -C "%VCPKG_ROOT%" cat-file -e "%VCPKG_BASELINE%^{commit}" >nul 2>nul
if errorlevel 1 (
  echo [INFO] Downloading the pinned vcpkg baseline...
  git -C "%VCPKG_ROOT%" fetch --depth 1 origin "%VCPKG_BASELINE%"
  if errorlevel 1 exit /b 1
)
git -C "%VCPKG_ROOT%" checkout --quiet --detach "%VCPKG_BASELINE%"
if errorlevel 1 exit /b 1

"%VCPKG_ROOT%\vcpkg.exe" version 2>nul | findstr /c:"2025-07-21" >nul
if not errorlevel 1 goto :ready
call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
if errorlevel 1 exit /b 1

:ready
endlocal & set "VCPKG_ROOT=%VCPKG_ROOT%"
exit /b 0
