# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
# Packs a release's built files into two zips for its GitHub Release (reference\release.md, step 7):
#   simoptix_obs_plugins_<version>.zip   both plugins, laid out as in OBS's plugin folder,
#                                        C:\ProgramData\obs-studio\plugins: <name>\bin\64bit\<name>.dll
#                                        and <name>\data\manifest.json
#   simoptix_openxr_layer_<version>.zip  the layer and its manifest in bin\, beside install.ps1,
#                                        uninstall.ps1 and uninstall.bat, which install it from there
# Refuses when a DLL's version isn't exactly the one in VERSION (a build off the release tag, or with
# changes), or when the layer's signature isn't valid.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File release_files.ps1 <output folder>
param([string]$OutputPath)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$version = ([IO.File]::ReadAllText((Join-Path $root 'VERSION'))).Trim()
$layerName = 'XR_APILAYER_SIMOPTIX_frame_source'
$layerBin = Join-Path $root 'vr_mirror\frame_sources\openxr\bin'
$plugins = [ordered]@{
    simoptix_vr_mirror = Join-Path $root 'vr_mirror\obs_plugin\bin'
    simoptix_overlays = Join-Path $root 'overlays\obs_plugin\bin'
}

$dlls = @($plugins.Keys | ForEach-Object { Join-Path $plugins[$_] "$_.dll" })
$dlls += Join-Path $layerBin "$layerName.dll"
foreach ($dll in $dlls) {
    if (-not (Test-Path $dll)) {
        Write-Host "error: $dll is missing. Build it first."
        exit 1
    }

    $built = (Get-Item $dll).VersionInfo.ProductVersion
    if ($built -ne $version) {
        Write-Host "error: $dll is version $built, not $version."
        Write-Host "Build it from the tag v$version, with nothing changed."
        exit 1
    }
}

$signature = Get-AuthenticodeSignature (Join-Path $layerBin "$layerName.dll")
if ($signature.Status -ne 'Valid') {
    Write-Host "error: the layer's signature is $($signature.Status)."
    Write-Host 'Sign it first (.agents\skills\dev\reference\openxr.md, "Signing").'
    exit 1
}

$staging = Join-Path $OutputPath "staging_$version"
if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }

$pluginsFolder = Join-Path $staging 'obs_plugins'
foreach ($name in $plugins.Keys) {
    $binFolder = Join-Path $pluginsFolder "$name\bin\64bit"
    $dataFolder = Join-Path $pluginsFolder "$name\data"
    New-Item -ItemType Directory -Force $binFolder, $dataFolder | Out-Null
    Copy-Item (Join-Path $plugins[$name] "$name.dll") $binFolder
    Copy-Item (Join-Path $plugins[$name] 'manifest.json') $dataFolder
}

$layerFolder = Join-Path $staging 'openxr_layer'
New-Item -ItemType Directory -Force (Join-Path $layerFolder 'bin') | Out-Null
foreach ($file in "$layerName.dll", "$layerName.json") {
    Copy-Item (Join-Path $layerBin $file) (Join-Path $layerFolder 'bin')
}
foreach ($script in 'install.ps1', 'uninstall.ps1', 'uninstall.bat') {
    Copy-Item (Join-Path $root "vr_mirror\frame_sources\openxr\$script") $layerFolder
}

$pluginsZip = Join-Path $OutputPath "simoptix_obs_plugins_$version.zip"
$layerZip = Join-Path $OutputPath "simoptix_openxr_layer_$version.zip"
Compress-Archive -Path (Join-Path $pluginsFolder '*') -DestinationPath $pluginsZip -Force
Compress-Archive -Path (Join-Path $layerFolder '*') -DestinationPath $layerZip -Force
Remove-Item -Recurse -Force $staging
Write-Host $pluginsZip
Write-Host $layerZip
