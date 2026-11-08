@echo off
rem Copyright (C) 2026 Anton Podolsky
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Sets up the 64-bit MSVC compiler (cl, lib, rc) for the build script that calls it, in that
rem script's own environment. Every build.bat in the repo calls it first.
rem Finds the newest Visual Studio 2019 or later (version 16.0 and up, any edition, the Build Tools
rem included) that has the C++ x64 tools, by asking vswhere.exe, which the Visual Studio Installer
rem always puts in the same place. Exits with 1 and says what it looked for when there is none.
rem Knows nothing about what is built.
rem (No labels: they can fail in batch files checked out with LF line endings. Paths are quoted in
rem the messages because "Program Files (x86)" would end a parenthesised block.)

set "SIMOPTIX_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%SIMOPTIX_VSWHERE%" (
   echo error: the Visual Studio Installer was not found: "%SIMOPTIX_VSWHERE%" is missing.
   echo Install Visual Studio 2019 or later, or its Build Tools, with "Desktop development with C++".
   exit /b 1
)
set "SIMOPTIX_VS="
set "SIMOPTIX_VS_QUERY=-latest -products * -version 16.0"
set "SIMOPTIX_VS_QUERY=%SIMOPTIX_VS_QUERY% -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
set "SIMOPTIX_VS_QUERY=%SIMOPTIX_VS_QUERY% -property installationPath"
for /f "usebackq delims=" %%i in (`"%SIMOPTIX_VSWHERE%" %SIMOPTIX_VS_QUERY%`) do set "SIMOPTIX_VS=%%i"
rem vswhere leaves out an installation the Installer marks incomplete (an update or registration
rem that didn't finish), though its compiler can still work: take one if there is nothing else. The
rem compiler is checked below either way.
if not defined SIMOPTIX_VS (
   for /f "usebackq delims=" %%i in (`"%SIMOPTIX_VSWHERE%" -all %SIMOPTIX_VS_QUERY%`) do set "SIMOPTIX_VS=%%i"
)
if not defined SIMOPTIX_VS (
   echo error: no Visual Studio 2019 or later with the C++ x64 tools was found.
   echo Install Visual Studio 2019 or later, or its Build Tools, with "Desktop development with C++".
   exit /b 1
)
set "SIMOPTIX_VCVARS=%SIMOPTIX_VS%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%SIMOPTIX_VCVARS%" (
   echo error: "%SIMOPTIX_VCVARS%" is missing: add "Desktop development with C++" to that installation.
   exit /b 1
)
call "%SIMOPTIX_VCVARS%" >nul 2>&1
where cl >nul 2>&1
if errorlevel 1 (
   echo error: "%SIMOPTIX_VCVARS%" did not set up the compiler.
   exit /b 1
)
exit /b 0
