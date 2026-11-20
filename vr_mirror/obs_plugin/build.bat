@echo off
rem Copyright (C) 2026 Anton Podolsky
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Builds SimOptix: VR Mirror, simoptix_vr_mirror.dll: the OBS source SimOptix: Pimax
rem Mirror (plugin.cpp, its settings as OBS saves them in source_settings.cpp; drawn by
rem mirror_view.cpp with the effect in warp_effect.cpp, its empty state by empty_state.cpp with
rem ..\..\shared\text_raster.cpp and the facts on it by setup_facts.cpp with video_memory.cpp, its frames
rem copied by frame_ring.cpp, its frame source started and stopped by session_worker.cpp, the audio kept
rem in step with its view by audio_sync.cpp), and its race
rem auto-record (race_auto_record\race_recorder.cpp decides when, race_auto_record\auto_record.cpp tells
rem OBS), the lap recorder for the demo (lap_recorder\: lap_finder.cpp finds the lap, lap_video.cpp encodes it,
rem lap_recorder.cpp runs both), plus the frame sources (..\frame_sources\pimax, openxr and demo, each
rem listed in its .rsp), the VR
rem Mirror's core (..\core, its sources listed in ..\core\build\*.rsp) and ..\..\shared (the view's
rem delay the plugins share, the crash dump, the files kept in %%LOCALAPPDATA%%\simoptix, and the game's
rem telemetry listed by
rem ..\..\shared\telemetry\sources.bat, which also finds LMU's headers: LMU_SDK; without them the plugin
rem builds without LMU, and race auto-record reads no game).
rem Its version comes from ..\..\shared\build\version.bat (version.txt at the repo's root, and git),
rem and goes into the DLL's version resource (version.rc).
rem Writes bin\manifest.json from manifest.json with the version filled in, for OBS 32 and later.
rem Optional argument "install": copies the DLL and manifest.json to OBS's per-machine plugin folder
rem (C:\ProgramData\obs-studio\plugins\simoptix_vr_mirror: the DLL in bin\64bit, manifest.json in
rem data, where OBS reads it). Close OBS first.
rem It links against OBS through an import library made here from ..\..\shared\obs_lib\obs.def (the
rem functions OBS 32.2.2's obs.dll exports).
setlocal
set "NAME=simoptix_vr_mirror"
call "%~dp0..\..\shared\build\vs_env.bat" || exit /b 1
cd /d "%~dp0"
if not exist obj mkdir obj
if not exist bin mkdir bin
call ..\..\shared\build\version.bat obj || exit /b 1
lib /nologo /def:..\..\shared\obs_lib\obs.def /machine:x64 /out:obj\obs.lib >nul || exit /b 1
call ..\..\shared\telemetry\sources.bat
rc /nologo /i ..\.. /i obj /fo obj\version.res version.rc || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File ..\core\build\embed_text.ps1 ^
   ..\core\pipeline\5_warp\warp.hlsli obj\warp_hlsli.h kWarpHlsli || exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /W3 /MD /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Fo:obj\ ^
   /I..\.. /I..\core /I..\..\third_party\obs-32.2.2\libobs /I..\..\third_party\openxr /Iobj %TELEMETRY_FLAGS% ^
   plugin.cpp source_settings.cpp mirror_view.cpp video_memory.cpp setup_facts.cpp warp_effect.cpp ^
   empty_state.cpp frame_ring.cpp session_worker.cpp ^
   audio_sync.cpp ^
   race_auto_record\race_recorder.cpp race_auto_record\auto_record.cpp ^
   lap_recorder\lap_finder.cpp lap_recorder\lap_video.cpp lap_recorder\lap_recorder.cpp ^
   ..\..\shared\shared_memory.cpp ..\..\shared\program_delay.cpp ..\..\shared\crash_dump.cpp ^
   ..\..\shared\local_files.cpp ..\..\shared\text_raster.cpp ^
   %TELEMETRY_FILES% ^
   @..\core\build\common.rsp @..\core\build\pipeline.rsp @..\core\build\adapters.rsp ^
   @..\frame_sources\pimax\pimax.rsp @..\frame_sources\openxr\openxr.rsp @..\frame_sources\demo\demo.rsp ^
   /LD /Fe:bin\%NAME%.dll ^
   /link obj\version.res obj\obs.lib d3d11.lib dxgi.lib winmm.lib user32.lib d3dcompiler.lib ^
   dbghelp.lib dwrite.lib mfplat.lib mfreadwrite.lib mfuuid.lib ole32.lib propsys.lib advapi32.lib shell32.lib || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File ..\..\shared\build\fill_version.ps1 ^
   manifest.json bin\manifest.json || exit /b 1
rem A line continued with ^ inside the block below runs as a command named " ", so each command
rem there stays on one line. An "exit /b 1" there reaches "cmd /c" as exit code 0, so a failed step
rem leaves the block by goto.
set "PLUGIN_FOLDER=C:\ProgramData\obs-studio\plugins\%NAME%"
if /i "%~1"=="install" (
   if not exist "%PLUGIN_FOLDER%\bin\64bit" mkdir "%PLUGIN_FOLDER%\bin\64bit" || goto :install_failed
   if not exist "%PLUGIN_FOLDER%\data" mkdir "%PLUGIN_FOLDER%\data" || goto :install_failed
   copy /y bin\%NAME%.dll "%PLUGIN_FOLDER%\bin\64bit\" >nul || goto :install_failed
   copy /y bin\manifest.json "%PLUGIN_FOLDER%\data\" >nul || goto :install_failed
   echo installed to %PLUGIN_FOLDER%
)
exit /b 0

:install_failed
echo cannot install to %PLUGIN_FOLDER%: close OBS, which holds the DLL. If SimOptix Setup
echo installed the plugin, its files belong to the administrators: run this as administrator.
exit /b 1
