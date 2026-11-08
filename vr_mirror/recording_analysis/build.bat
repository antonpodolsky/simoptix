@echo off
rem Copyright (C) 2026 Anton Podolsky
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Builds patch_shift.exe (used by window.py) and grid_shift.exe (used by analyse_recording.py).
setlocal
call "%~dp0..\..\shared\build\vs_env.bat" || exit /b 1
cd /d "%~dp0"
if not exist obj mkdir obj
cl /nologo /std:c++17 /EHsc /O2 /W3 /Fo:obj\ patch_shift.cpp /Fe:patch_shift.exe || exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /W3 /openmp /Fo:obj\ grid_shift.cpp /Fe:grid_shift.exe || exit /b 1
