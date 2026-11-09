@echo off
rem Copyright (C) 2026 Anton Podolsky
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Builds SimOptix Setup, bin\simoptix_setup.exe, one file that carries VR Mirror, Overlays and the
rem OpenXR layer inside it, each with its manifest (setup.rc), and installs them. setup_main.cpp starts
rem it, setup_window.cpp is its window and setup_look.cpp draws it, setup_status.cpp decides from what
rem setup_probe.cpp reads, and setup_install.cpp and setup_uninstall.cpp make the changes, through
rem setup_payload.cpp, setup_signature.cpp, setup_registry.cpp and setup_system.cpp. setup_places.cpp
rem says where each part goes.
rem It builds both plugins first, because they check that they come from one build. It takes the layer
rem as it is in ..\vr_mirror\frame_sources\openxr\bin, because building it here would replace a signed
rem DLL with an unsigned one: build the layer, and sign it, before this. It links the C runtime in (/MT),
rem so the exe needs nothing beside it.
rem Its version comes from ..\shared\build\version.bat (VERSION at the repo's root, and git), and goes
rem into the exe's version resource (setup.rc) and the entry in Windows' installed apps.
setlocal
call "%~dp0..\shared\build\vs_env.bat" || exit /b 1
cd /d "%~dp0"
set "LAYER=..\vr_mirror\frame_sources\openxr\bin\XR_APILAYER_SIMOPTIX_frame_source"
if not exist "%LAYER%.dll" (
   echo error: the OpenXR layer isn't built. Run vr_mirror\frame_sources\openxr\build.bat, and sign it.
   exit /b 1
)
if not exist "%LAYER%.json" (
   echo error: the OpenXR layer's manifest is missing. Run vr_mirror\frame_sources\openxr\build.bat.
   exit /b 1
)
call ..\vr_mirror\obs_plugin\build.bat || exit /b 1
call ..\overlays\obs_plugin\build.bat || exit /b 1
cd /d "%~dp0"
if not exist obj mkdir obj
if not exist bin mkdir bin
call ..\shared\build\version.bat obj || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File make_icon.ps1 obj\setup.ico || exit /b 1
rc /nologo /i .. /i obj /fo obj\setup.res setup.rc || exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /W3 /MT /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Fo:obj\ ^
   /I.. /Iobj ^
   setup_main.cpp setup_window.cpp setup_controls.cpp setup_look.cpp setup_buttons.cpp setup_dialog.cpp ^
   setup_draw.cpp ^
   setup_theme.cpp setup_run.cpp setup_progress.cpp setup_questions.cpp setup_status.cpp setup_probe.cpp setup_install.cpp setup_uninstall.cpp ^
   setup_places.cpp setup_payload.cpp setup_signature.cpp setup_registry.cpp setup_system.cpp ^
   /Fe:bin\simoptix_setup.exe ^
   /link /SUBSYSTEM:WINDOWS obj\setup.res /MANIFEST:EMBED /MANIFESTINPUT:setup.manifest /MANIFESTUAC:NO ^
   user32.lib gdi32.lib gdiplus.lib dwmapi.lib shell32.lib ole32.lib advapi32.lib version.lib ^
   wintrust.lib crypt32.lib || exit /b 1
rem A fresh build of the layer is unsigned (a new worktree has no other), and setup would carry it. A
rem layer built at another commit carries another version than the plugins and setup.
powershell -NoProfile -Command ^
   "$s = (Get-AuthenticodeSignature '%LAYER%.dll').Status; if ($s -eq 'Valid') {" ^
   "Write-Host 'The OpenXR layer in setup is signed (Valid).' } else {" ^
   "Write-Host ('warning: the OpenXR layer in setup is not validly signed (' + $s + ').') };" ^
   "$v = (Get-Item '%LAYER%.dll').VersionInfo.ProductVersion; if ($v -ne '%SIMOPTIX_VERSION%') {" ^
   "Write-Host ('warning: the OpenXR layer in setup is version ' + $v + ', setup %SIMOPTIX_VERSION%.') }"
