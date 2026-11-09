@echo off
rem Copyright (C) 2026 Anton Podolsky
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Builds SimOptix: Overlays, simoptix_overlays.dll: plugin.cpp registers the G-meter
rem (..\gmeter\gmeter_source.cpp: SimOptix: G-Meter Overlay, drawn by meter_chrono.cpp with
rem meter_draw.cpp and meter_text.cpp; its text rasterised by ..\common\text_raster.cpp with
rem DirectWrite, its picture layers by meter_image.cpp, embedded by gmeter.rc from its design folder).
rem Telemetry is ..\..\shared\telemetry (the model, the one reader both plugins share and the games it
rem reads), listed by its sources.bat, which also finds LMU's headers (without them the plugin builds
rem without LMU and reads no game). The G-meter's demo lap is ..\..\shared\telemetry\demo_lap.cpp.
rem Plus the rest of ..\..\shared (logging, the picture's delay the plugins share, the crash dump, the
rem files kept in %%LOCALAPPDATA%%\simoptix).
rem Its version comes from ..\..\shared\build\version.bat (VERSION at the repo's root, and git).
rem Optional argument "install": copies it to OBS's per-machine plugin folder
rem (C:\ProgramData\obs-studio\plugins\simoptix_overlays\bin\64bit). Close OBS first.
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
set "G=..\gmeter"
set "C=..\common"
cl /nologo /std:c++17 /EHsc /O2 /W3 /MD /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Fo:obj\ ^
   /I..\.. /I%G% /I%C% ^
   /I..\..\third_party\obs-32.2.2\libobs %TELEMETRY_FLAGS% ^
   plugin.cpp %TELEMETRY_FILES% ..\..\shared\telemetry\demo_lap.cpp ^
   %G%\gmeter_source.cpp %G%\meter_chrono.cpp %G%\meter_draw.cpp %G%\meter_text.cpp ^
   %C%\text_raster.cpp %C%\meter_image.cpp %C%\properties_preview.cpp ^
   ..\..\shared\log.cpp ..\..\shared\shared_memory.cpp ..\..\shared\program_delay.cpp ^
   ..\..\shared\crash_dump.cpp ..\..\shared\local_files.cpp ^
   /LD /Fe:bin\%NAME%.dll ^
   /link obj\gmeter.res obj\obs.lib shlwapi.lib dwrite.lib d3d11.lib user32.lib gdi32.lib ^
   gdiplus.lib dbghelp.lib || exit /b 1
if /i "%~1"=="install" (
   if not exist "C:\ProgramData\obs-studio\plugins\%NAME%\bin\64bit" ^
      mkdir "C:\ProgramData\obs-studio\plugins\%NAME%\bin\64bit"
   copy /y bin\%NAME%.dll "C:\ProgramData\obs-studio\plugins\%NAME%\bin\64bit\" >nul || exit /b 1
   echo installed to C:\ProgramData\obs-studio\plugins\%NAME%\bin\64bit
)
