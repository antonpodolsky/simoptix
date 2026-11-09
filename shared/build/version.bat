@echo off
rem Copyright (C) 2026 Anton Podolsky
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Sets SimOptix's version for the build script that calls it, in that script's own environment, and
rem writes it as a C header, simoptix_version.h, into the folder the first argument names (the
rem caller's obj\). Every build.bat that makes a shipped file calls it, after vs_env.bat.
rem The version is the number in VERSION at the repo's root, major.minor.patch (SemVer). A build that
rem isn't exactly on that version's release tag, v<version>, adds the commits since the newest release
rem tag and the commit as SemVer build metadata: 0.1.0+12.gabc1234, as git describe --tags gives them.
rem Before the first release tag, the commits are counted from the first. Changes to tracked files
rem that aren't committed add .dirty. A copy of the repo without git gives 0.1.0+unknown.
rem Sets:
rem   SIMOPTIX_VERSION          that string
rem   SIMOPTIX_VERSION_MAJOR, SIMOPTIX_VERSION_MINOR, SIMOPTIX_VERSION_PATCH
rem   SIMOPTIX_VERSION_COMMITS  the commits since the release tag, the file version's fourth number
rem   SIMOPTIX_FILE_VERSION     the file version, major.minor.patch.commits, as Windows shows it
rem   SIMOPTIX_VERSION_NUMBER   major * 1000000 + minor * 1000 + patch, for the OpenXR layer's
rem                             manifest, which takes one decimal number
rem Exits with 1 and says why when VERSION isn't three numbers (minor and patch below 1000), or when
rem the commit carries the release tag of another version and nothing is changed.
rem Knows nothing about what is built.
rem (No labels: they can fail in batch files checked out with LF line endings.)

if "%~1"=="" (
   echo error: version.bat needs the folder to write simoptix_version.h into.
   exit /b 1
)
for %%i in ("%~dp0..\..") do set "SIMOPTIX_ROOT=%%~fi"

set "SIMOPTIX_VERSION_MAJOR="
set "SIMOPTIX_VERSION_MINOR="
set "SIMOPTIX_VERSION_PATCH="
set "SIMOPTIX_VERSION_REST="
for /f "usebackq tokens=1-3* delims=." %%a in ("%SIMOPTIX_ROOT%\VERSION") do (
   set "SIMOPTIX_VERSION_MAJOR=%%a"
   set "SIMOPTIX_VERSION_MINOR=%%b"
   set "SIMOPTIX_VERSION_PATCH=%%c"
   set "SIMOPTIX_VERSION_REST=%%d"
)

rem set /a reads a part as a number. A part that isn't a plain number, or has a leading zero, comes
rem back different from the text.
set "SIMOPTIX_VERSION_BAD="
if defined SIMOPTIX_VERSION_REST set "SIMOPTIX_VERSION_BAD=1"
set /a "SIMOPTIX_VERSION_CHECK=SIMOPTIX_VERSION_MAJOR" 2>nul
if not "%SIMOPTIX_VERSION_CHECK%"=="%SIMOPTIX_VERSION_MAJOR%" set "SIMOPTIX_VERSION_BAD=1"
set /a "SIMOPTIX_VERSION_CHECK=SIMOPTIX_VERSION_MINOR" 2>nul
if not "%SIMOPTIX_VERSION_CHECK%"=="%SIMOPTIX_VERSION_MINOR%" set "SIMOPTIX_VERSION_BAD=1"
set /a "SIMOPTIX_VERSION_CHECK=SIMOPTIX_VERSION_PATCH" 2>nul
if not "%SIMOPTIX_VERSION_CHECK%"=="%SIMOPTIX_VERSION_PATCH%" set "SIMOPTIX_VERSION_BAD=1"
if defined SIMOPTIX_VERSION_BAD (
   echo error: "%SIMOPTIX_ROOT%\VERSION" must hold one version, three numbers like 0.1.0.
   exit /b 1
)
set /a "SIMOPTIX_VERSION_CHECK=SIMOPTIX_VERSION_MAJOR / 2000 + SIMOPTIX_VERSION_MINOR / 1000"
set /a "SIMOPTIX_VERSION_CHECK+=SIMOPTIX_VERSION_PATCH / 1000"
if not "%SIMOPTIX_VERSION_CHECK%"=="0" (
   echo error: the version in "%SIMOPTIX_ROOT%\VERSION" is too large.
   echo The major number must be below 2000, the minor and patch numbers below 1000.
   exit /b 1
)
set "SIMOPTIX_VERSION=%SIMOPTIX_VERSION_MAJOR%.%SIMOPTIX_VERSION_MINOR%.%SIMOPTIX_VERSION_PATCH%"
set /a "SIMOPTIX_VERSION_NUMBER=SIMOPTIX_VERSION_MAJOR * 1000000 + SIMOPTIX_VERSION_MINOR * 1000"
set /a "SIMOPTIX_VERSION_NUMBER+=SIMOPTIX_VERSION_PATCH"
set "SIMOPTIX_FILE_VERSION=%SIMOPTIX_VERSION%"

set "SIMOPTIX_VERSION_COMMIT="
set "SIMOPTIX_VERSION_TAG="
set "SIMOPTIX_VERSION_COMMITS=0"
set "SIMOPTIX_VERSION_DIRTY="
for /f "usebackq delims=" %%i in (`git -C "%SIMOPTIX_ROOT%" rev-parse --short HEAD 2^>nul`) do (
   set "SIMOPTIX_VERSION_COMMIT=%%i"
)
for /f "usebackq delims=" %%i in (
   `git -C "%SIMOPTIX_ROOT%" describe --tags --match "v[0-9]*" --abbrev^=0 2^>nul`
) do set "SIMOPTIX_VERSION_TAG=%%i"
if defined SIMOPTIX_VERSION_TAG (
   for /f "usebackq delims=" %%i in (
      `git -C "%SIMOPTIX_ROOT%" rev-list --count %SIMOPTIX_VERSION_TAG%..HEAD 2^>nul`
   ) do set "SIMOPTIX_VERSION_COMMITS=%%i"
)
if defined SIMOPTIX_VERSION_COMMIT if not defined SIMOPTIX_VERSION_TAG (
   for /f "usebackq delims=" %%i in (`git -C "%SIMOPTIX_ROOT%" rev-list --count HEAD 2^>nul`) do (
      set "SIMOPTIX_VERSION_COMMITS=%%i"
   )
)
for /f "usebackq delims=" %%i in (
   `git -C "%SIMOPTIX_ROOT%" status --porcelain --untracked-files^=no 2^>nul`
) do set "SIMOPTIX_VERSION_DIRTY=1"

set "SIMOPTIX_VERSION_ON_TAG="
if defined SIMOPTIX_VERSION_TAG if "%SIMOPTIX_VERSION_COMMITS%"=="0" set "SIMOPTIX_VERSION_ON_TAG=1"
if defined SIMOPTIX_VERSION_DIRTY set "SIMOPTIX_VERSION_ON_TAG="
if defined SIMOPTIX_VERSION_ON_TAG if not "%SIMOPTIX_VERSION_TAG%"=="v%SIMOPTIX_VERSION%" (
   echo error: this commit is tagged %SIMOPTIX_VERSION_TAG%, but VERSION says %SIMOPTIX_VERSION%.
   exit /b 1
)
set "SIMOPTIX_VERSION_META="
if not defined SIMOPTIX_VERSION_COMMIT set "SIMOPTIX_VERSION_META=unknown"
if defined SIMOPTIX_VERSION_COMMIT if not "%SIMOPTIX_VERSION_COMMITS%"=="0" (
   set "SIMOPTIX_VERSION_META=%SIMOPTIX_VERSION_COMMITS%.g%SIMOPTIX_VERSION_COMMIT%"
)
if defined SIMOPTIX_VERSION_COMMIT if defined SIMOPTIX_VERSION_DIRTY (
   set "SIMOPTIX_VERSION_META=%SIMOPTIX_VERSION_COMMITS%.g%SIMOPTIX_VERSION_COMMIT%.dirty"
)
set "SIMOPTIX_FILE_VERSION=%SIMOPTIX_FILE_VERSION%.%SIMOPTIX_VERSION_COMMITS%"
if defined SIMOPTIX_VERSION_META set "SIMOPTIX_VERSION=%SIMOPTIX_VERSION%+%SIMOPTIX_VERSION_META%"

if not exist "%~1" mkdir "%~1"
> "%~1\simoptix_version.h" (
   echo // Written by shared\build\version.bat for this build, from VERSION and git. Not committed.
   echo #pragma once
   echo #define SIMOPTIX_VERSION "%SIMOPTIX_VERSION%"
   echo #define SIMOPTIX_VERSION_MAJOR %SIMOPTIX_VERSION_MAJOR%
   echo #define SIMOPTIX_VERSION_MINOR %SIMOPTIX_VERSION_MINOR%
   echo #define SIMOPTIX_VERSION_PATCH %SIMOPTIX_VERSION_PATCH%
   echo #define SIMOPTIX_VERSION_COMMITS %SIMOPTIX_VERSION_COMMITS%
   echo #define SIMOPTIX_FILE_VERSION "%SIMOPTIX_FILE_VERSION%"
   echo #define SIMOPTIX_FILE_VERSION_PARTS %SIMOPTIX_FILE_VERSION:.=,%
) || exit /b 1
echo SimOptix %SIMOPTIX_VERSION%
exit /b 0
