@echo off
rem Copyright (C) 2026 Anton Podolsky
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Builds and runs the layout check (pvr_layout_check.cpp), which compares the project's own PVR
rem declarations (..\pvr_interface.h) with the headers of Pimax's public PC SDK. Only where that SDK is
rem installed: by default in C:\Program Files\Pimax\Sdk\Include; PVR_SDK names another folder.
rem Without it, says so and stops. Nothing else in the repo needs the SDK.
setlocal
if not defined PVR_SDK set "PVR_SDK=C:\Program Files\Pimax\Sdk\Include"
if not exist "%PVR_SDK%\PVR_Interface.h" (
   echo Pimax's SDK headers not found in "%PVR_SDK%": layout check skipped.
   exit /b 0
)
call "%~dp0..\..\..\..\shared\build\vs_env.bat" || exit /b 1
cd /d "%~dp0"
if not exist obj mkdir obj
if not exist bin mkdir bin
cl /nologo /std:c++17 /EHsc /W3 /DNOMINMAX /Fo:obj\ /I..\..\..\.. /I"%PVR_SDK%" ^
   pvr_layout_check.cpp /Fe:bin\pvr_layout_check.exe || exit /b 1
bin\pvr_layout_check.exe
