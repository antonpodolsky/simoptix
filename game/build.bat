@echo off
rem Copyright (C) 2026 Anton Podolsky
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Builds the demo game, bin\simoptix_game.exe, from game_main.cpp and the files beside it. It links the
rem C runtime in (/MT) and loads an installed OpenXR loader at run time. The shader compiler it uses,
rem d3dcompiler_47.dll, comes with Windows. So it needs nothing beside it.
setlocal
call "%~dp0..\shared\build\vs_env.bat" || exit /b 1
cd /d "%~dp0"
if not exist obj mkdir obj
if not exist bin mkdir bin
cl /nologo /std:c++17 /EHsc /O2 /W3 /MT /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Fo:obj\ ^
   /I.. /I..\third_party\openxr ^
   game_main.cpp game_settings.cpp game_view.cpp game_grid.cpp game_openxr.cpp ^
   /Fe:bin\simoptix_game.exe /link d3d11.lib dxgi.lib d3dcompiler.lib || exit /b 1
