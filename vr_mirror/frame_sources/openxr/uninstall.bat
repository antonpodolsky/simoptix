@echo off
rem Copyright (C) 2026 Anton Podolsky
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Double-click to remove the OpenXR layer: runs uninstall.ps1 as administrator (Windows asks for
rem permission) in a window that stays open, so the result can be read. Close LMU first.
powershell -NoProfile -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -NoExit -File \"%~dp0uninstall.ps1\"'"
