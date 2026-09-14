@echo off
setlocal

rem ASCII-only batch file for reliable execution in cmd.exe.
cd /d "%~dp0"

rem Prefer the Release build: it contains the extended document-management features.
set "CLIENT_EXE=out\build\windows-release-qt\client_qt\Release\client_qt_example.exe"
if not exist "%CLIENT_EXE%" set "CLIENT_EXE=out\build\windows-debug-qt\client_qt\Debug\client_qt_example.exe"

if not exist "%CLIENT_EXE%" (
    echo [ERROR] Qt client executable not found: %CLIENT_EXE%
    echo Build the client_qt_example target first.
    pause
    exit /b 1
)

rem The client must load the exact Qt it was built against. Other Qt copies on this
rem machine sit earlier on PATH; loading those ends in 0xC0000139
rem (STATUS_ENTRYPOINT_NOT_FOUND) before the window ever appears.
set "QT_DIR="
set "QT_BIN="
for /f "tokens=2 delims==" %%i in ('findstr /b /c:"Qt6_DIR:PATH=" "out\build\windows-release-qt\CMakeCache.txt" 2^>nul') do set "QT_DIR=%%i"
if defined QT_DIR set "QT_ROOT=%QT_DIR:/lib/cmake/Qt6=%"
if defined QT_ROOT set "QT_ROOT=%QT_ROOT:\lib\cmake\Qt6=%"
if defined QT_ROOT set "QT_BIN=%QT_ROOT%\bin"
if defined QT_BIN if not exist "%QT_BIN%\Qt6Widgets.dll" set "QT_BIN="
if not defined QT_BIN for /d %%D in ("D:\Qt\6.*") do if not defined QT_BIN if exist "%%~D\msvc2022_64\bin\Qt6Widgets.dll" set "QT_BIN=%%~D\msvc2022_64\bin"
if defined QT_BIN set "PATH=%QT_BIN%;%PATH%"
if defined QT_BIN echo Qt runtime: %QT_BIN%

echo Starting LanCloudDrive client...
echo Executable: %CLIENT_EXE%
start "LanCloudDrive Client" /d "%CD%" "%CD%\%CLIENT_EXE%"

endlocal