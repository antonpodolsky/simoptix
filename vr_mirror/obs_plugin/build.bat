@echo off
rem Copyright (C) 2026 Anton Podolsky
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Builds SimOptix: VR Mirror, simoptix_vr_mirror.dll: the OBS source SimOptix: Pimax
rem Mirror (plugin.cpp; drawn by mirror_view.cpp with the effect in warp_effect.cpp, its frames
rem copied by frame_ring.cpp, its frame source started and stopped by session_worker.cpp, the audio kept
rem in step with its picture by audio_sync.cpp), and its race
rem auto-record (race_auto_record\race_recorder.cpp decides when, race_auto_record\auto_record.cpp tells
rem OBS), plus the Pimax frame source (..\frame_sources\pimax, listed in its pimax.rsp), the VR
rem Mirror's core (..\core, its sources listed in ..\core\build\*.rsp) and ..\..\shared (the picture's
rem delay the plugins share, the crash dump, and the game's telemetry listed by
rem ..\..\shared\telemetry\sources.bat, which also finds LMU's headers: LMU_SDK; without them the plugin
rem builds without LMU, and race auto-record reads no game).
rem Optional argument "install": copies it to OBS's per-machine plugin folder
rem (C:\ProgramData\obs-studio\plugins\simoptix_vr_mirror\bin\64bit). Close OBS first.
rem It links against OBS through an import library made here from ..\..\shared\obs_lib\obs.def (the
rem functions OBS 32.2.2's obs.dll exports).
setlocal
set "NAME=simoptix_vr_mirror"
call "%~dp0..\..\shared\build\vs_env.bat" || exit /b 1
cd /d "%~dp0"
if not exist obj mkdir obj
if not exist bin mkdir bin
lib /nologo /def:..\..\shared\obs_lib\obs.def /machine:x64 /out:obj\obs.lib >nul || exit /b 1
call ..\..\shared\telemetry\sources.bat
powershell -NoProfile -ExecutionPolicy Bypass -File ..\core\build\embed_text.ps1 ^
   ..\core\pipeline\5_warp\warp.hlsli obj\warp_hlsli.h kWarpHlsli || exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /W3 /MD /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Fo:obj\ ^
   /I..\.. /I..\core /I..\..\third_party\obs-32.2.2\libobs /Iobj %TELEMETRY_FLAGS% ^
   plugin.cpp mirror_view.cpp warp_effect.cpp frame_ring.cpp session_worker.cpp audio_sync.cpp ^
   race_auto_record\race_recorder.cpp race_auto_record\auto_record.cpp ^
   ..\..\shared\shared_memory.cpp ..\..\shared\program_delay.cpp ..\..\shared\crash_dump.cpp ^
   %TELEMETRY_FILES% ^
   @..\core\build\common.rsp @..\core\build\pipeline.rsp @..\core\build\adapters.rsp ^
   @..\frame_sources\pimax\pimax.rsp @..\frame_sources\openxr\openxr.rsp ^
   /LD /Fe:bin\%NAME%.dll ^
   /link obj\obs.lib d3d11.lib dxgi.lib winmm.lib user32.lib d3dcompiler.lib dbghelp.lib || exit /b 1
if /i "%~1"=="install" (
   if not exist "C:\ProgramData\obs-studio\plugins\%NAME%\bin\64bit" ^
      mkdir "C:\ProgramData\obs-studio\plugins\%NAME%\bin\64bit"
   copy /y bin\%NAME%.dll "C:\ProgramData\obs-studio\plugins\%NAME%\bin\64bit\" >nul || exit /b 1
   echo installed to C:\ProgramData\obs-studio\plugins\%NAME%\bin\64bit
)
