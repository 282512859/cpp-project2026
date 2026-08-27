@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo [1/7] Locating Visual Studio 2022...
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [ERROR] Visual Studio Installer was not found.
  echo Install Visual Studio 2022 with "Desktop development with C++".
  exit /b 1
)
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
  echo [ERROR] Visual Studio 2022 C++ tools were not found.
  exit /b 1
)
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1

echo [2/7] Preparing the static SQLite dependency...
call "%~dp0tools\ensure_vcpkg.bat"
if errorlevel 1 exit /b 1

echo [3/7] Generating the Visual Studio 2022 solution...
cmake --preset windows-release
if errorlevel 1 exit /b 1

echo [4/7] Building static Release executables...
cmake --build --preset windows-release --parallel
if errorlevel 1 exit /b 1

echo [5/7] Running unit tests...
ctest --preset windows-release
if errorlevel 1 exit /b 1

echo [6/7] Creating and inspecting the portable package...
set "PACKAGE_ROOT=%CD%\dist\LanCloudDrive_v0.1_windows_x64"
cmake --install out\build\windows-release --config Release --prefix "%PACKAGE_ROOT%"
if errorlevel 1 exit /b 1
if not exist "%PACKAGE_ROOT%\runtime\storage" mkdir "%PACKAGE_ROOT%\runtime\storage"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\verify_windows_package.ps1" -PackageRoot "%PACKAGE_ROOT%"
if errorlevel 1 exit /b 1
echo [7/7] Running the Windows end-to-end smoke test...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\windows_smoke_test.ps1" -PackageRoot "%PACKAGE_ROOT%"
if errorlevel 1 exit /b 1
powershell -NoProfile -Command "Compress-Archive -Path '%PACKAGE_ROOT%\*' -DestinationPath '%CD%\dist\LanCloudDrive_v0.1_windows_x64_portable.zip' -Force"
if errorlevel 1 exit /b 1

echo.
echo [SUCCESS] Portable package:
echo   %CD%\dist\LanCloudDrive_v0.1_windows_x64_portable.zip
echo [SUCCESS] Visual Studio solution:
echo   %CD%\out\build\windows-release\LanCloudDrive.sln
exit /b 0
