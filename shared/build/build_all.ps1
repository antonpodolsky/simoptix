# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds every part of the repo with its own build.bat, in the order of the dev skill's build table,
# and fails when a build fails or prints a warning of the compiler, the linker or the resource
# compiler. The checks on GitHub run it, and it runs the same on any PC:
#   powershell -NoProfile -ExecutionPolicy Bypass -File shared\build\build_all.ps1 [-LogDir <folder>]
# Each build's output goes to the console and to <part>.log in the log folder (obj\logs beside this
# script by default). SimOptix Setup builds both plugins itself, and carries the OpenXR layer, so the
# layer is built before it. Without LMU's headers, the plugins build without LMU and the fake LMU
# isn't built, and without Pimax's SDK the PVR layout check skips itself. Each says so in its log.
# The layer and setup are built unsigned. Nothing here signs.
param([string]$LogDir = (Join-Path $PSScriptRoot 'obj\logs'))

$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')

$builds = @(
    @{ Name = 'mirror_app'; Script = 'vr_mirror\mirror_app\build.bat' },
    @{ Name = 'e2e'; Script = 'vr_mirror\e2e\build.bat' },
    @{ Name = 'lmu_fake'; Script = 'shared\telemetry\games\lmu\fake\build.bat' },
    @{ Name = 'openxr_layer'; Script = 'vr_mirror\frame_sources\openxr\build.bat' },
    @{ Name = 'setup'; Script = 'setup\build.bat' },
    @{ Name = 'recording_analysis'; Script = 'vr_mirror\recording_analysis\build.bat' },
    @{ Name = 'pvr_layout_check'; Script = 'vr_mirror\frame_sources\pimax\check\build.bat' }
)

# cl's "warning C4244:", link's "warning LNK4098:" and rc's "warning RC4005:". Setup's own notes on an
# unsigned layer ("warning: the OpenXR layer ...") have no code, and don't count.
$warningPattern = '\bwarning [A-Z]+\d+:'

function Invoke-Build([hashtable]$build, [string]$logDir) {
    $script = Join-Path $repo $build.Script
    Write-Host "==== $($build.Name): $($build.Script)"

    $lines = @(& cmd /c "`"$script`" 2>&1" | ForEach-Object { Write-Host $_; $_ })
    $exitCode = $LASTEXITCODE
    [IO.File]::WriteAllLines((Join-Path $logDir "$($build.Name).log"), [string[]]$lines)

    return [pscustomobject]@{
        Name = $build.Name
        Failed = $exitCode -ne 0
        Warnings = @($lines | Where-Object { $_ -match $warningPattern })
    }
}

function Main {
    New-Item -ItemType Directory -Force $LogDir | Out-Null
    $results = @(foreach ($build in $builds) { Invoke-Build $build $LogDir })

    Write-Host '==== Summary'
    $bad = 0

    foreach ($result in $results) {
        if ($result.Failed) {
            Write-Host "$($result.Name): failed"
            $bad++
        } elseif ($result.Warnings.Count -gt 0) {
            Write-Host "$($result.Name): $($result.Warnings.Count) warning(s)"
            $result.Warnings | ForEach-Object { Write-Host "  $_" }
            $bad++
        } else {
            Write-Host "$($result.Name): built"
        }
    }

    Write-Host "Logs: $LogDir"
    return [int]($bad -gt 0)
}

exit (Main)
