# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
# Writes SimOptix Setup's icon, the design's placeholder logo: a rounded square in the band's teal
# (#3CC4CC), its corners 23% of its size, at 16, 24, 32, 48 and 256 px. Each size is a PNG inside the
# .ico, which Windows Vista and later read. build.bat runs it into obj\, so the icon is never committed.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File make_icon.ps1 <output .ico>
param([string]$OutputPath)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$sizes = 16, 24, 32, 48, 256
$teal = [System.Drawing.Color]::FromArgb(255, 0x3C, 0xC4, 0xCC)

function New-IconImage([int]$size) {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $graphics.Clear([System.Drawing.Color]::Transparent)

    $diameter = [single]($size * 0.23 * 2)
    $far = [single]$size - $diameter
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc(0, 0, $diameter, $diameter, 180, 90)
    $path.AddArc($far, 0, $diameter, $diameter, 270, 90)
    $path.AddArc($far, $far, $diameter, $diameter, 0, 90)
    $path.AddArc(0, $far, $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    $brush = New-Object System.Drawing.SolidBrush $teal
    $graphics.FillPath($brush, $path)

    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $brush.Dispose()
    $path.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
    return , $stream.ToArray()
}

$images = @($sizes | ForEach-Object { , (New-IconImage $_) })

# The .ico format: a 6-byte header, a 16-byte entry per image, then the images.
$output = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $output
$writer.Write([uint16]0)
$writer.Write([uint16]1)
$writer.Write([uint16]$sizes.Count)

$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $side = if ($sizes[$i] -ge 256) { 0 } else { $sizes[$i] }  # 0 stands for 256
    $writer.Write([byte]$side)
    $writer.Write([byte]$side)
    $writer.Write([byte]0)
    $writer.Write([byte]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]32)
    $writer.Write([uint32]$images[$i].Length)
    $writer.Write([uint32]$offset)
    $offset += $images[$i].Length
}
foreach ($image in $images) {
    $writer.Write($image)
}

New-Item -ItemType Directory -Force ([IO.Path]::GetDirectoryName($OutputPath)) | Out-Null
[IO.File]::WriteAllBytes($OutputPath, $output.ToArray())
$writer.Dispose()
