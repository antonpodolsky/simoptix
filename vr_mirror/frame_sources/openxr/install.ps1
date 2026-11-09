# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
# Installs the OpenXR layer for Le Mans Ultimate: copies the DLL and its manifest from bin\ (build.bat)
# to C:\Program Files\simoptix\openxr_frame_source\ and registers it with the OpenXR loader for
# every user (HKLM, as layers loaded into games should be). Run as administrator, with the game closed.
# From then on, every OpenXR program loads the layer; it only does anything in Le Mans Ultimate.
# Undo: uninstall.ps1. Switch off without uninstalling: set the registry value below to 1.
$ErrorActionPreference = 'Stop'
$name = 'XR_APILAYER_SIMOPTIX_frame_source'
$source = Join-Path $PSScriptRoot 'bin'
$target = 'C:\Program Files\simoptix\openxr_frame_source'
$key = 'HKLM:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { Write-Host 'Run this as administrator (it writes to Program Files and HKLM).'; exit 1 }
if (Get-Process -Name 'Le Mans Ultimate' -ErrorAction SilentlyContinue) {
    Write-Host 'Le Mans Ultimate is running: close it first.'; exit 1
}
foreach ($f in "$name.dll", "$name.json") {
    if (-not (Test-Path (Join-Path $source $f))) {
        Write-Host "Missing $source\$f`: run build.bat first."
        exit 1
    }
}

New-Item -ItemType Directory -Force $target | Out-Null
Copy-Item (Join-Path $source "$name.dll") $target -Force
Copy-Item (Join-Path $source "$name.json") $target -Force
$manifest = Join-Path $target "$name.json"
if (-not (Test-Path $key)) { New-Item -Path $key -Force | Out-Null }
New-ItemProperty -Path $key -Name $manifest -PropertyType DWord -Value 0 -Force | Out-Null

Write-Host "Installed to $target"
# Easy Anti-Cheat keeps the layer out of LMU unless Windows reports this as Valid.
$signature = Get-AuthenticodeSignature (Join-Path $target "$name.dll")
Write-Host "Signature: $($signature.Status) ($($signature.SignerCertificate.Subject))"
Write-Host "Registered in $key (0 = on)"
Write-Host "The layer's logs, one per start of LMU: $env:LOCALAPPDATA\simoptix\openxr_frame_source_<date>_<time>.log"
Write-Host 'Layers registered now:'
Get-Item $key | Select-Object -ExpandProperty Property | ForEach-Object {
    Write-Host "  $_ = $((Get-ItemProperty $key).$_)"
}
