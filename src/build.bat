@echo off
rem ===========================================================================
rem  Build PatchAIC (x64) - requires Visual Studio 2022 or Build Tools
rem  The resulting exe is statically linked: it imports KERNEL32.dll only,
rem  so it runs on Windows and under Wine / box64 (Winlator) with no .NET.
rem ===========================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "VCVARS="

rem --- 1) already inside a developer prompt ---------------------------------
where cl >nul 2>nul
if not errorlevel 1 goto :havecl

rem --- 2) ask vswhere ------------------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "!VSWHERE!" (
    for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
)
if defined VSDIR if exist "!VSDIR!\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=!VSDIR!\VC\Auxiliary\Build\vcvars64.bat"

rem --- 3) brute force the usual layouts ------------------------------------
if not defined VCVARS (
    for %%e in (Community Professional Enterprise BuildTools) do (
        for %%v in (2022 2019) do (
            if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\%%v\%%e\VC\Auxiliary\Build\vcvars64.bat" (
                set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\%%v\%%e\VC\Auxiliary\Build\vcvars64.bat"
            )
            if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\%%v\%%e\VC\Auxiliary\Build\vcvars64.bat" (
                set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\%%v\%%e\VC\Auxiliary\Build\vcvars64.bat"
            )
        )
    )
)

if not defined VCVARS (
    echo [ERROR] MSVC not found.
    echo         Install Visual Studio 2022 with "Desktop development with C++",
    echo         or run this script from a "x64 Native Tools Command Prompt".
    exit /b 1
)

call "!VCVARS!" >nul
if errorlevel 1 (
    echo [ERROR] failed to initialise the MSVC environment from
    echo         !VCVARS!
    exit /b 1
)

:havecl
if not exist "..\_out" mkdir "..\_out"

cl /nologo /O2 /MT /GS- /W3 /D_CRT_SECURE_NO_WARNINGS ^
   /Fo:..\_out\ /Fe:..\_out\PatchAIC.exe PatchAIC.c ^
   /link /SUBSYSTEM:CONSOLE /MACHINE:X64

if errorlevel 1 exit /b 1
echo.
echo built: %~dp0..\_out\PatchAIC.exe
