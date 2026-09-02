@echo off
setlocal
set "SCRIPT_DIR=%~dp0"

if not exist "%SCRIPT_DIR%PatchTool.exe" (
    echo [ERROR] PatchTool.exe not found. It must sit in the same folder as this script.
    pause
    exit /b 1
)

if "%~1"=="" ( set "DLL=" ) else ( set "DLL=%~1" )

if "%DLL%"=="" (
    echo Drag ^& drop unsafeAssem.dll onto this file, or type the full path:
    echo   example: %SCRIPT_DIR%patch.bat "C:\Game\AliceInCradle_Data\Managed\unsafeAssem.dll"
    echo.
    set /p "DLL=Full path to unsafeAssem.dll: "
)

if not exist "%DLL%" (
    echo [ERROR] File not found: %DLL%
    pause
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%patch.ps1" -DllPath "%DLL%"
pause
