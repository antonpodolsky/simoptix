# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
# Wraps a text file (e.g. warp.hlsli) in a C++ header as a raw string constant, so a host can
# compile the shared shader code without shipping the file.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File embed_text.ps1 <input> <output.h> <name>
param([string]$InputPath, [string]$OutputPath, [string]$Name)
$text = [IO.File]::ReadAllText($InputPath)
$header = "// Generated from $([IO.Path]::GetFileName($InputPath)) by embed_text.ps1. Do not edit.`r`n" +
          "#pragma once`r`n" +
          "static const char $Name[] = R`"EMBED($text)EMBED`";`r`n"
New-Item -ItemType Directory -Force ([IO.Path]::GetDirectoryName($OutputPath)) | Out-Null
[IO.File]::WriteAllText($OutputPath, $header, (New-Object Text.UTF8Encoding $false))
