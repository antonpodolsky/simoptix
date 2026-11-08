# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
# Removes the OpenXR layer: its registration with the OpenXR loader and its folder under
# C:\Program Files\simoptix\. Run as administrator, with the game closed.
# Its log in %LOCALAPPDATA%\simoptix stays.
$ErrorActionPreference = 'Stop'
$name = 'XR_APILAYER_SIMOPTIX_frame_source'
$target = 'C:\Program Files\simoptix\openxr_frame_source'
$key = 'HKLM:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { Write-Host 'Run this as administrator (it writes to Program Files and HKLM).'; exit 1 }
if (Get-Process -Name 'Le Mans Ultimate' -ErrorAction SilentlyContinue) {
    Write-Host 'Le Mans Ultimate is running: close it first.'; exit 1
}

$manifest = Join-Path $target "$name.json"
if ((Test-Path $key) -and ((Get-Item $key).Property -contains $manifest)) {
    Remove-ItemProperty -Path $key -Name $manifest
    Write-Host "Unregistered from $key"
} else {
    Write-Host 'Was not registered.'
}
if (Test-Path $target) {
    Remove-Item -Recurse -Force $target
    Write-Host "Removed $target"
}
$parent = Split-Path $target
if ((Test-Path $parent) -and -not (Get-ChildItem $parent)) { Remove-Item $parent }
