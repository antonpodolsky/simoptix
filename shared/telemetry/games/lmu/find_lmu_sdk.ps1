# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
# Prints the folder of Le Mans Ultimate's telemetry headers (Support\SharedMemoryInterface in the game's
# folder), for the builds that read LMU (shared\telemetry\sources.bat runs it when LMU_SDK isn't set).
# It looks in every Steam library: Steam's own folder (from the registry), then the libraries listed in
# its steamapps\libraryfolders.vdf. Prints nothing if the game isn't found.
$ErrorActionPreference = 'SilentlyContinue'
$steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam').SteamPath
if (-not $steam) { $steam = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam').InstallPath }
if (-not $steam) { exit 0 }
$libraries = @($steam)
$vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
if (Test-Path $vdf) {
    foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
        $libraries += $m.Groups[1].Value.Replace('\\', '\')
    }
}
foreach ($library in $libraries) {
    $sdk = Join-Path $library 'steamapps\common\Le Mans Ultimate\Support\SharedMemoryInterface'
    if (Test-Path (Join-Path $sdk 'SharedMemoryInterface.hpp')) {
        Write-Output ([IO.Path]::GetFullPath($sdk))
        exit 0
    }
}
