@echo off
rem Copyright (C) 2026 Anton Podolsky
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Builds the OpenXR layer XR_APILAYER_SIMOPTIX_frame_source.dll into bin\, with its manifest next to it,
rem and the check program bin\check\Le Mans Ultimate.exe (check\openxr_check.cpp). The check program
rem carries the game's file name because the layer only wakes up in a program of that name; it is
rem not the game.
rem The DLL links the C runtime in (/MT), so it needs nothing else to load.
rem Installing it for the game: install.bat (install.ps1 as administrator; README.md).
setlocal
call "%~dp0..\..\..\shared\build\vs_env.bat" || exit /b 1
cd /d "%~dp0"
if not exist obj mkdir obj
if not exist obj\check mkdir obj\check
if not exist bin mkdir bin
if not exist bin\check mkdir bin\check
cl /nologo /std:c++17 /EHsc /O2 /W3 /MT /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Fo:obj\ ^
   /I..\..\.. /I..\..\..\third_party\openxr ^
   openxr_layer_entry.cpp openxr_layer_calls.cpp openxr_layer_frames.cpp openxr_publisher.cpp ^
   openxr_layer_log.cpp openxr_frame_layout.cpp ^
   ..\..\..\shared\log.cpp ^
   /LD /Fe:bin\XR_APILAYER_SIMOPTIX_frame_source.dll ^
   /link /DEF:frame_source.def d3d11.lib dxgi.lib || exit /b 1
copy /y XR_APILAYER_SIMOPTIX_frame_source.json bin\ >nul || exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /W3 /MT /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS ^
   /Fo:obj\check\ ^
   /I. /I..\..\.. /I..\..\..\third_party\openxr check\openxr_check.cpp check\openxr_check_copies.cpp ^
   /Fe:"bin\check\Le Mans Ultimate.exe" /link d3d11.lib dxgi.lib || exit /b 1
