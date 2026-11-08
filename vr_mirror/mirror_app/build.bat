@echo off
rem Copyright (C) 2026 Anton Podolsky
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Builds simoptix_vr_mirror.exe: the window host (main.cpp: the loop; window_output.cpp: the device, swap
rem chain and warp shader; mirror_app_settings.cpp: simoptix_vr_mirror.ini; frame_hold.cpp: the held frames;
rem precise_wait.cpp: the timer waits) plus the Pimax frame source (..\frame_sources\pimax, listed in its
rem pimax.rsp), the VR Mirror's core (..\core, its sources listed in ..\core\build\*.rsp) and ..\..\shared.
rem Optional argument: another exe name, e.g. "build.bat simoptix_vr_mirror_dev.exe" to build a test
rem version while simoptix_vr_mirror.exe is running.
setlocal
set OUT=%~1
if "%OUT%"=="" set OUT=simoptix_vr_mirror.exe
call "%~dp0..\..\shared\build\vs_env.bat" || exit /b 1
cd /d "%~dp0"
if not exist obj mkdir obj
powershell -NoProfile -ExecutionPolicy Bypass -File ..\core\build\embed_text.ps1 ^
   ..\core\pipeline\5_warp\warp.hlsli obj\warp_hlsli.h kWarpHlsli || exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /W3 /DUNICODE /D_UNICODE /DNOMINMAX /Fo:obj\ ^
   /I..\.. /I..\core /Iobj ^
   main.cpp window_output.cpp mirror_app_settings.cpp frame_hold.cpp precise_wait.cpp ^
   @..\core\build\common.rsp @..\core\build\pipeline.rsp @..\core\build\adapters.rsp ^
   @..\frame_sources\pimax\pimax.rsp @..\frame_sources\openxr\openxr.rsp /Fe:%OUT% ^
   /link d3d11.lib dxgi.lib d3dcompiler.lib winmm.lib user32.lib dwmapi.lib || exit /b 1
