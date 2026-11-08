@echo off
rem Copyright (C) 2026 Anton Podolsky
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Builds lmu_fake.exe: publishes a made-up lap (and with --race a race's start and finish) on LMU's
rem telemetry interface, for checking the G-meter and race auto-record without the game. The lap is
rem the G-meter's demo lap (..\..\..\demo_lap.cpp). Needs LMU's telemetry headers (..\..\..\sources.bat
rem finds them, or LMU_SDK names their folder).
setlocal
cd /d "%~dp0"
call ..\..\..\sources.bat
if not defined LMU_SDK exit /b 1
call "%~dp0..\..\..\..\build\vs_env.bat" || exit /b 1
cd /d "%~dp0"
if not exist obj mkdir obj
cl /nologo /std:c++17 /EHsc /O2 /W3 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Fo:obj\ ^
   /I"%LMU_SDK%" /I..\..\..\..\.. ^
   lmu_fake.cpp ..\..\..\demo_lap.cpp /Fe:lmu_fake.exe /link winmm.lib || exit /b 1
