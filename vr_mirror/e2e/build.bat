@echo off
rem Copyright (C) 2026 Anton Podolsky
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Builds the fake PVR runtime (bin\libPVRClient64.dll), the fake game with the OpenXR layer's
rem frames (bin\fake_openxr.exe), the e2e runner (bin\e2e_runner.exe), the local files test
rem (bin\local_files_e2e.exe) with its fake game that loads the layer's DLL
rem (bin\fake_layer_game\Le Mans Ultimate.exe), and
rem the stage replay (bin\stage_replay.exe: the pipeline and its stages offline, the drawn pose's
rem replay simulation included).
setlocal
call "%~dp0..\..\shared\build\vs_env.bat" || exit /b 1
cd /d "%~dp0"
if not exist bin mkdir bin
if not exist obj mkdir obj

cl /nologo /std:c++17 /EHsc /O2 /W3 /DNOMINMAX /LD /Fo:obj\ ^
   /I..\.. /I..\core /Ifake_pvr /Ifake_game ^
   fake_pvr\fake_pvr.cpp fake_pvr\fake_pvr_headset.cpp fake_game\fake_game_picture.cpp ^
   /Fe:bin\libPVRClient64.dll ^
   /link d3d11.lib dxgi.lib d3dcompiler.lib winmm.lib ole32.lib windowscodecs.lib || exit /b 1

if not exist obj\fake_openxr mkdir obj\fake_openxr
cl /nologo /std:c++17 /EHsc /O2 /W3 /DNOMINMAX /Fo:obj\fake_openxr\ ^
   /I..\.. /I..\core /Ifake_game /I..\..\third_party\openxr ^
   fake_openxr\fake_openxr.cpp fake_openxr\fake_openxr_layer.cpp fake_game\fake_game_picture.cpp ^
   ..\frame_sources\openxr\openxr_layer_frames.cpp ..\frame_sources\openxr\openxr_publisher.cpp ^
   ..\..\shared\log.cpp /Fe:bin\fake_openxr.exe ^
   /link d3d11.lib dxgi.lib d3dcompiler.lib winmm.lib || exit /b 1

cl /nologo /std:c++17 /EHsc /O2 /Zi /W3 /DNOMINMAX /DUNICODE /D_UNICODE /Fo:obj\ /Fd:obj\ ^
   /I..\.. /I..\core ^
   runner\runner.cpp runner\scenarios.cpp runner\analysis.cpp runner\compare.cpp runner\tool_process.cpp ^
   /Fe:bin\e2e_runner.exe ^
   /link /DEBUG /MAP:obj\e2e_runner.map d3d11.lib dxgi.lib dwmapi.lib windowsapp.lib user32.lib || exit /b 1

if not exist obj\local_files mkdir obj\local_files
cl /nologo /std:c++17 /EHsc /O2 /W3 /DNOMINMAX /DUNICODE /D_UNICODE /Fo:obj\local_files\ ^
   /I..\.. /I..\core /Irunner ^
   local_files\local_files_e2e.cpp local_files\local_files_obs.cpp local_files\local_files_folder.cpp ^
   runner\tool_process.cpp ^
   /Fe:bin\local_files_e2e.exe /link user32.lib || exit /b 1

if not exist obj\fake_layer_game mkdir obj\fake_layer_game
if not exist bin\fake_layer_game mkdir bin\fake_layer_game
cl /nologo /std:c++17 /EHsc /O2 /W3 /DNOMINMAX /DUNICODE /D_UNICODE /Fo:obj\fake_layer_game\ ^
   /I..\..\third_party\openxr ^
   fake_layer_game\fake_layer_game.cpp /Fe:"bin\fake_layer_game\Le Mans Ultimate.exe" || exit /b 1

if not exist obj\stage_replay mkdir obj\stage_replay
cl /nologo /std:c++17 /EHsc /O2 /W3 /DNOMINMAX /Fo:obj\stage_replay\ ^
   /I..\.. /I..\core /Ifake_pvr /Ifake_game ^
   stage_replay\stage_replay.cpp stage_replay\golden.cpp stage_replay\runs_pipeline.cpp ^
   stage_replay\scores.cpp ^
   stage_replay\runs_frame_picker.cpp stage_replay\runs_arrival.cpp stage_replay\runs_drawn_pose.cpp ^
   @..\core\build\pipeline.rsp ^
   /Fe:bin\stage_replay.exe /link ole32.lib windowscodecs.lib || exit /b 1
