@echo off
rem Copyright (C) 2026 Anton Podolsky
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Builds the OpenXR layer XR_APILAYER_SIMOPTIX_frame_source.dll into bin\, with its manifest next to it,
rem and the check program bin\check\Le Mans Ultimate.exe (check\openxr_check.cpp). The check program
rem carries the game's file name because the layer only wakes up in a program of that name; it is
rem not the game.
rem The DLL links the C runtime in (/MT), so it needs nothing else to load.
rem Signs the DLL when SIMOPTIX_SIGN_THUMBPRINT names a code signing certificate, and fails if it can't.
rem Installing it for the game: install.bat (install.ps1 as administrator; README.md).
setlocal
call "%~dp0..\..\..\shared\build\vs_env.bat" || exit /b 1
cd /d "%~dp0"
if not exist obj mkdir obj
if not exist obj\check mkdir obj\check
if not exist bin mkdir bin
if not exist bin\check mkdir bin\check
cl /nologo /std:c++17 /EHsc /O2 /W3 /MT /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Fo:obj\ ^
   /I..\..\.. /I..\..\..\third_party\openxr ^
   openxr_layer_entry.cpp openxr_layer_calls.cpp openxr_layer_frames.cpp openxr_publisher.cpp ^
   openxr_layer_log.cpp openxr_frame_layout.cpp ^
   ..\..\..\shared\log.cpp ^
   /LD /Fe:bin\XR_APILAYER_SIMOPTIX_frame_source.dll ^
   /link /DEF:frame_source.def d3d11.lib dxgi.lib || exit /b 1
copy /y XR_APILAYER_SIMOPTIX_frame_source.json bin\ >nul || exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /W3 /MT /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS ^
   /Fo:obj\check\ ^
   /I. /I..\..\.. /I..\..\..\third_party\openxr check\openxr_check.cpp check\openxr_check_copies.cpp ^
   /Fe:"bin\check\Le Mans Ultimate.exe" /link d3d11.lib dxgi.lib || exit /b 1

rem Easy Anti-Cheat keeps an unsigned layer out of LMU. The certificate is found by its SHA-1
rem thumbprint in the user's store, where a cloud certificate shows only while its desktop app is
rem logged in (SimplySign Desktop for Certum's). The check after signing fails unless the chain
rem ends at a root Windows trusts. The timestamp keeps the signature valid after the certificate
rem expires: an expired one without it is reported as not valid.
if not defined SIMOPTIX_SIGN_THUMBPRINT (
   echo The layer is unsigned, so Easy Anti-Cheat keeps it out of LMU. SIMOPTIX_SIGN_THUMBPRINT names
   echo the code signing certificate to sign it with.
   exit /b 0
)
where signtool >nul 2>&1
if errorlevel 1 (
   echo error: signtool was not found. It comes with the Windows SDK of Visual Studio's C++ tools.
   exit /b 1
)
signtool sign /q /sha1 %SIMOPTIX_SIGN_THUMBPRINT% /fd SHA256 /tr http://time.certum.pl /td SHA256 ^
   bin\XR_APILAYER_SIMOPTIX_frame_source.dll || exit /b 1
signtool verify /q /pa bin\XR_APILAYER_SIMOPTIX_frame_source.dll || exit /b 1
echo The layer is signed.
