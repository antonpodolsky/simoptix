@echo off
rem Copyright (C) 2026 Anton Podolsky
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Double-click to install the OpenXR layer for Le Mans Ultimate. Builds it first (so it never
rem installs an old build), then runs install.ps1 as administrator (Windows asks for permission) in a
rem window that stays open, so the result can be read. Close LMU first.
setlocal
cd /d "%~dp0"
call build.bat || (echo The build failed. & pause & exit /b 1)
powershell -NoProfile -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -NoExit -File \"%~dp0install.ps1\"'"
