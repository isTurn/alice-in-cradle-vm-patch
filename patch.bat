@echo off
rem ===========================================================================
rem  patch.bat - convenience wrapper around PatchAIC.exe
rem
rem  Drag unsafeAssem.dll (or the whole game folder) onto this file,
rem  or run it with a path:
rem
rem      patch.bat "D:\Game\AliceInCradle_Data\Managed\unsafeAssem.dll"
rem      patch.bat "D:\Game"
rem
rem  Run it with no argument and PatchAIC.exe searches for the DLL by itself:
rem  next to the exe, then ..\AliceInCradle_Data\Managed\, then recursively
rem  below its own folder.  Any extra arguments (--check, --restore, an output
rem  path) are passed straight through.
rem ===========================================================================
setlocal
set "SCRIPT_DIR=%~dp0"
set "TOOL=%SCRIPT_DIR%PatchAIC.exe"

if not exist "%TOOL%" (
    echo [ERROR] PatchAIC.exe not found next to this script.
    echo         Expected: "%TOOL%"
    echo.
    echo         Build it from source with: src\build.bat
    pause
    exit /b 1
)

"%TOOL%" %*

set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" if not "%RC%"=="2" (
    echo.
    echo PatchAIC exited with code %RC%.
    echo   2 = already patched, nothing to do
    echo   3 = unsafeAssem.dll or the env-check pattern was not found
    echo   1 = read/write failure
)

exit /b %RC%
