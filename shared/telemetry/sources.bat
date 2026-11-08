@echo off
rem Copyright (C) 2026 Anton Podolsky
rem SPDX-License-Identifier: GPL-2.0-or-later
rem The telemetry's sources, for the builds of both plugins (vr_mirror\obs_plugin\build.bat and
rem overlays\obs_plugin\build.bat call this) and of the fake LMU. Sets:
rem   TELEMETRY_FILES  the telemetry's source files, with the games whose SDK was found
rem   TELEMETRY_FLAGS  those games' compiler options (/DHAVE_<GAME> and their SDK's folder)
rem   LMU_SDK          Le Mans Ultimate's Support\SharedMemoryInterface folder, if found
rem LMU's headers stay in the game's folder (their licence doesn't allow copying them into this repo):
rem LMU_SDK, if set, names the folder; otherwise games\lmu\find_lmu_sdk.ps1 looks for the game in every
rem Steam library. Without them the plugin builds without LMU (HAVE_LMU undefined) and reads no game.
set "T=%~dp0"
if not defined LMU_SDK (
   for /f "usebackq delims=" %%d in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%T%games\lmu\find_lmu_sdk.ps1"`) do set "LMU_SDK=%%d"
)
set TELEMETRY_FILES="%T%telemetry.cpp" "%T%telemetry_ring.cpp" "%T%telemetry_area.cpp"
set TELEMETRY_FILES=%TELEMETRY_FILES% "%T%telemetry_reader.cpp" "%T%game_registry.cpp"
set "TELEMETRY_FLAGS="
if exist "%LMU_SDK%\SharedMemoryInterface.hpp" (
   echo LMU's telemetry headers: %LMU_SDK%
   set TELEMETRY_FLAGS=/DHAVE_LMU /I"%LMU_SDK%"
   set TELEMETRY_FILES=%TELEMETRY_FILES% "%T%games\lmu\lmu_telemetry.cpp"
) else (
   set "LMU_SDK="
   echo LMU's telemetry headers not found in any Steam library: building without LMU. Set LMU_SDK to the game's Support\SharedMemoryInterface folder.
)
set "T="
