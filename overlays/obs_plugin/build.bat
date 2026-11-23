@echo off
rem Copyright (C) 2026 Anton Podolsky
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Builds SimOptix: Overlays, simoptix_overlays.dll: plugin.cpp registers the G-meter
rem (..\gmeter\gmeter_source.cpp: SimOptix: G-Meter Overlay, drawn by meter_chrono.cpp, its image layers
rem embedded by gmeter.rc from its design folder) and the performance overlay
rem (..\performance\performance_source.cpp: SimOptix: Performance Overlay, drawn by
rem performance_look.cpp, its image layers embedded by performance.rc, the frame times it reads from
rem ..\..\shared\performance). What any overlay uses is in ..\common: shapes
rem (meter_draw.cpp), labels (meter_text.cpp, their text rasterised by ..\..\shared\text_raster.cpp with
rem DirectWrite), image layers (meter_image.cpp), the box on the canvas (overlay_box.cpp) and the
rem Properties preview (properties_preview.cpp).
rem Telemetry is ..\..\shared\telemetry (the model, the one reader both plugins share and the games it
rem reads), listed by its sources.bat, which also finds LMU's headers (without them the plugin builds
rem without LMU and reads no game). The G-meter's demo lap is ..\..\shared\telemetry\demo_lap.cpp.
rem Plus the rest of ..\..\shared (logging, the view's delay the plugins share, the crash dump, the
rem files kept in %%LOCALAPPDATA%%\simoptix).
rem Its version comes from ..\..\shared\build\version.bat (version.txt at the repo's root, and git),
rem and goes into the DLL's version resource (version.rc).
rem Writes bin\manifest.json from manifest.json with the version filled in, for OBS 32 and later.
rem Optional argument "install": copies the DLL and manifest.json to OBS's per-machine plugin folder
rem (C:\ProgramData\obs-studio\plugins\simoptix_overlays: the DLL in bin\64bit, manifest.json in
rem data, where OBS reads it). Close OBS first.
rem It links against OBS through an import library made here from ..\..\shared\obs_lib\obs.def (the
rem functions OBS 32.2.2's obs.dll exports).
setlocal
set "NAME=simoptix_overlays"
call "%~dp0..\..\shared\build\vs_env.bat" || exit /b 1
cd /d "%~dp0"
if not exist obj mkdir obj
if not exist bin mkdir bin
call ..\..\shared\build\version.bat obj || exit /b 1
lib /nologo /def:..\..\shared\obs_lib\obs.def /machine:x64 /out:obj\obs.lib >nul || exit /b 1
call ..\..\shared\telemetry\sources.bat
rc /nologo /i ..\gmeter /fo obj\gmeter.res ..\gmeter\gmeter.rc || exit /b 1
rc /nologo /i ..\performance /fo obj\performance.res ..\performance\performance.rc || exit /b 1
rc /nologo /i ..\.. /i obj /fo obj\version.res version.rc || exit /b 1
set "G=..\gmeter"
set "C=..\common"
set "P=..\performance"
cl /nologo /std:c++17 /EHsc /O2 /W3 /MD /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Fo:obj\ ^
   /I..\.. /I%G% /I%C% /I%P% /Iobj ^
   /I..\..\third_party\obs-32.2.2\libobs %TELEMETRY_FLAGS% ^
   plugin.cpp %TELEMETRY_FILES% ..\..\shared\telemetry\demo_lap.cpp ^
   %G%\gmeter_source.cpp %G%\meter_chrono.cpp ^
   %P%\performance_source.cpp %P%\performance_look.cpp %P%\performance_view.cpp ^
   ..\..\shared\performance\performance_reader.cpp ..\..\shared\performance\performance_reading.cpp ^
   %C%\meter_draw.cpp %C%\meter_text.cpp %C%\meter_image.cpp %C%\overlay_box.cpp ^
   %C%\properties_preview.cpp ..\..\shared\text_raster.cpp ^
   ..\..\shared\log.cpp ..\..\shared\shared_memory.cpp ..\..\shared\program_delay.cpp ^
   ..\..\shared\crash_dump.cpp ..\..\shared\local_files.cpp ^
   /LD /Fe:bin\%NAME%.dll ^
   /link obj\gmeter.res obj\performance.res obj\version.res obj\obs.lib shlwapi.lib dwrite.lib d3d11.lib user32.lib ^
   gdi32.lib gdiplus.lib dbghelp.lib || exit /b 1
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
