# Draws Island's icon (a glass pill on a dark rounded square) at the sizes
# Windows uses and packs them into src\Island.ico. Run it again after changing
# the drawing; the .ico is checked in, so builds don't need this script.
#
#   pwsh -File tools\make_icon.ps1

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Add-Type -AssemblyName System.Drawing

function RoundedRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $path.AddArc($x, $y, $d, $d, 180, 90)
    $path.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $path.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $path.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    return $path
}

function DrawIcon([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.Clear([System.Drawing.Color]::Transparent)
    $s = [float]$size

    # A dark rounded square with a soft blue-violet glow, like a screen at night.
    $tile = RoundedRect ($s * 0.04) ($s * 0.04) ($s * 0.92) ($s * 0.92) ($s * 0.22)
    $bg = New-Object System.Drawing.Drawing2D.LinearGradientBrush ([System.Drawing.PointF]::new(0, 0)), ([System.Drawing.PointF]::new($s, $s)),
        ([System.Drawing.Color]::FromArgb(255, 44, 52, 92)), ([System.Drawing.Color]::FromArgb(255, 18, 20, 30))
    $g.FillPath($bg, $tile)

    # The island: a glass pill near the top.
    $pw = $s * 0.62; $ph = $s * 0.22
    $px = ($s - $pw) / 2; $py = $s * 0.2
    $pill = RoundedRect $px $py $pw $ph ($ph / 2)
    $glass = New-Object System.Drawing.Drawing2D.LinearGradientBrush ([System.Drawing.PointF]::new(0, $py)), ([System.Drawing.PointF]::new(0, $py + $ph)),
        ([System.Drawing.Color]::FromArgb(255, 236, 240, 255)), ([System.Drawing.Color]::FromArgb(255, 160, 172, 214))
    $g.FillPath($glass, $pill)

    # Two "sound bars" lines under it, hinting at the panel.
    if ($size -ge 32) {
        $bar = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(150, 190, 200, 255))
        $bh = [math]::Max(1.5, $s * 0.06)
        foreach ($i in 0, 1) {
            $bw = $s * (0.5 - 0.14 * $i)
            $g.FillPath($bar, (RoundedRect (($s - $bw) / 2) ($s * (0.56 + 0.13 * $i)) $bw $bh ($bh / 2)))
        }
    }
    $g.Dispose()
    return $bmp
}

# ICO file: a header, one directory entry per size, then each image as PNG.
$sizes = 16, 20, 24, 32, 40, 48, 64, 256
$images = foreach ($size in $sizes) {
    $bmp = DrawIcon $size
    $stream = New-Object System.IO.MemoryStream
    $bmp.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    , $stream.ToArray()
}
$out = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $out
$writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $dim = if ($sizes[$i] -ge 256) { 0 } else { $sizes[$i] }
    $writer.Write([byte]$dim); $writer.Write([byte]$dim); $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([uint16]1); $writer.Write([uint16]32)
    $writer.Write([uint32]$images[$i].Length); $writer.Write([uint32]$offset)
    $offset += $images[$i].Length
}
foreach ($image in $images) { $writer.Write($image) }
$writer.Flush()
[System.IO.File]::WriteAllBytes((Join-Path $root "src\Island.ico"), $out.ToArray())
# A preview for the docs.
(DrawIcon 256).Save((Join-Path $root "tools\icon-preview.png"))
"wrote src\Island.ico ($($sizes -join ', ') px)"
