@echo off
rem Copyright (C) 2026 Anton Podolsky
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Builds SimOptix Setup, bin\simoptix_setup.exe, one file that carries VR Mirror, Overlays and the
rem OpenXR layer inside it (setup.rc) and installs them. setup_main.cpp starts it, setup_window.cpp is its
rem window and setup_look.cpp draws it, setup_status.cpp decides from what setup_probe.cpp reads, and
rem setup_install.cpp and setup_uninstall.cpp make the changes, through setup_payload.cpp,
rem setup_signature.cpp, setup_registry.cpp and setup_system.cpp. setup_places.cpp says where each part
rem goes.
rem It builds both plugins first, because they check that they come from one build. It takes the layer
rem as it is in ..\vr_mirror\frame_sources\openxr\bin, because building it here would replace a signed
rem DLL with an unsigned one: build the layer, and sign it, before this. It links the C runtime in (/MT),
rem so the exe needs nothing beside it.
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
rc /nologo /fo obj\setup.res setup.rc || exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /W3 /MT /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Fo:obj\ ^
   /I.. ^
   setup_main.cpp setup_window.cpp setup_look.cpp setup_status.cpp setup_probe.cpp setup_install.cpp ^
   setup_uninstall.cpp setup_places.cpp setup_payload.cpp setup_signature.cpp setup_registry.cpp ^
   setup_system.cpp ^
   /Fe:bin\simoptix_setup.exe ^
   /link /SUBSYSTEM:WINDOWS obj\setup.res /MANIFEST:EMBED /MANIFESTINPUT:setup.manifest /MANIFESTUAC:NO ^
   user32.lib gdi32.lib shell32.lib ole32.lib advapi32.lib version.lib wintrust.lib crypt32.lib ^
   || exit /b 1
rem A fresh build of the layer is unsigned (a new worktree has no other), and setup would carry it.
powershell -NoProfile -Command ^
   "$s = (Get-AuthenticodeSignature '%LAYER%.dll').Status; if ($s -eq 'Valid') {" ^
   "Write-Host 'The OpenXR layer in setup is signed (Valid).' } else {" ^
   "Write-Host ('warning: the OpenXR layer in setup is not validly signed (' + $s + ').') }"
