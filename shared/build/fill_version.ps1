# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
# Writes a copy of a text file with the build's version filled in: @SIMOPTIX_VERSION@ becomes
# SIMOPTIX_VERSION and @SIMOPTIX_VERSION_NUMBER@ becomes SIMOPTIX_VERSION_NUMBER, both set by
# version.bat, which the calling build.bat has run. The copy is UTF-8 without a byte-order mark.
# Used for the OBS plugins' manifest.json and the OpenXR layer's manifest.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File fill_version.ps1 <template> <output>
param([string]$TemplatePath, [string]$OutputPath)
$ErrorActionPreference = 'Stop'

foreach ($name in 'SIMOPTIX_VERSION', 'SIMOPTIX_VERSION_NUMBER') {
    if (-not [Environment]::GetEnvironmentVariable($name)) {
        Write-Host "error: $name isn't set: run shared\build\version.bat first."
        exit 1
    }
}

$text = [IO.File]::ReadAllText($TemplatePath)
$text = $text.Replace('@SIMOPTIX_VERSION_NUMBER@', $env:SIMOPTIX_VERSION_NUMBER)
$text = $text.Replace('@SIMOPTIX_VERSION@', $env:SIMOPTIX_VERSION)

if ($text.Contains('@SIMOPTIX_')) {
    Write-Host "error: $TemplatePath has a placeholder that fill_version.ps1 doesn't know."
    exit 1
}

New-Item -ItemType Directory -Force ([IO.Path]::GetDirectoryName($OutputPath)) | Out-Null
[IO.File]::WriteAllText($OutputPath, $text, (New-Object Text.UTF8Encoding $false))
