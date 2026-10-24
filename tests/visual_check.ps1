# Visual check for Island (PLAN.md Phase 3+). Hands-free: it never moves
# your mouse, clicks or takes focus, so you can keep working while it runs.
#
# It shows a test backdrop under the island (white on the left, coloured
# stripes and text on the right) and starts Island with --pose, then switches
# the island between its states with the "Island.Pose" message. For each
# morph it records the pill's width frame by frame (so the spring timing can
# be checked, including a morph reversed halfway) and saves a screenshot.
#
#   pwsh -File tests\visual_check.ps1 [-Config Debug] [-Out <folder>]

param([string]$Config = "Release", [string]$Out = "")

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\$Config\Island.exe"
if (-not (Test-Path $exe)) { throw "Build first: $exe not found" }
if (-not $Out) { $Out = Join-Path $env:TEMP "island-visual-check" }
New-Item -ItemType Directory -Force $Out | Out-Null

# Modern .NET spreads System.Drawing over several assemblies; reference the loaded ones.
Add-Type -AssemblyName System.Drawing
$drawing = [AppDomain]::CurrentDomain.GetAssemblies() | Where-Object { $_.Location -and $_.GetName().Name -match '^System\.(Drawing|Private\.Windows)' } | ForEach-Object Location
Add-Type -Path (Join-Path $PSScriptRoot "TestHelpers.cs") -ReferencedAssemblies ($drawing + @("System.Threading", "System.Threading.Thread"))

$report = [System.Collections.Generic.List[string]]::new()
function Morph([string]$name, [int]$pose, [int]$row, [int]$ms) {
    [Visual]::Pose($pose)
    $report.Add("--- $name (width on row $row)")
    $report.Add([Visual]::Timeline($row, $ms))
}

# The control center is wide: the backdrop spans the screen (it lets clicks through).
[Visual]::Width = [Visual]::GetSystemMetrics(0)
[Visual]::Height = 760
[Visual]::StartBackdrop()
# --glass-snapshot: the clear glass from one frame taken before the island
# shows, so the island stays visible to these screenshots (normally it is
# left out of every capture).
Start-Process $exe -ArgumentList "--pose idle --glass-snapshot"
Start-Sleep -Milliseconds 1500
try {
    $r = [Visual]::IslandRect()
    $report.Add("idle window: $($r.Right - $r.Left) x $($r.Bottom - $r.Top) px at ($($r.Left), $($r.Top))")
    # Sample just under the pill's top edge, above any content.
    $row = $r.Bottom - [int](($r.Bottom - $r.Top) * 0.62)
    [Visual]::Shot((Join-Path $Out "1-idle.png"), 700, 110)

    Morph "idle -> peek" 2 $row 1100
    $r = [Visual]::IslandRect(); $report.Add("peek window after settling: $($r.Right - $r.Left) x $($r.Bottom - $r.Top) px")
    [Visual]::Shot((Join-Path $Out "2-peek.png"), 700, 110)

    Morph "peek -> expanded" 3 $row 1100
    $r = [Visual]::IslandRect(); $report.Add("expanded window after settling: $($r.Right - $r.Left) x $($r.Bottom - $r.Top) px")
    [Visual]::Shot((Join-Path $Out "3-expanded.png"), [Visual]::Width, [Visual]::Height)

    Morph "expanded -> idle" 1 $row 1100
    [Visual]::Shot((Join-Path $Out "4-idle-again.png"), 700, 110)

    # Change of mind halfway: expand, then go back after 120 ms. The width
    # must turn around smoothly, never jump.
    [Visual]::Pose(3)
    Start-Sleep -Milliseconds 120
    Morph "interrupted: expand, back to idle after 120 ms" 1 $row 1100

    # Auto-hide: released from its pose, the island tucks away (an invisible
    # strip along the top edge). Touching the strip brings the pill down;
    # the mouse "leaving" right after tucks it away again.
    [Visual]::Pose(0)
    Start-Sleep -Milliseconds 900
    $r = [Visual]::IslandRect(); $report.Add("tucked window: $($r.Right - $r.Left) x $($r.Bottom - $r.Top) px (an invisible strip; 2-3 px when auto-hide is on)")
    [Visual]::Shot((Join-Path $Out "6-tucked.png"), 700, 110)
    [Visual]::Touch()
    $report.Add("--- touch the top edge: the pill comes down, then tucks away (width on row $row)")
    $report.Add([Visual]::Timeline($row, 1300))

    # Closing the control center with auto-hide: it should visibly shrink
    # and slide up into the top edge, not just fade out where it is.
    [Visual]::Pose(3)
    Start-Sleep -Milliseconds 900
    [Visual]::Pose(0)
    $report.Add("--- control center closing into the top edge (width on row $row; starts after the 500 ms leave delay)")
    $report.Add([Visual]::Timeline($row, 1400))

    [Visual]::Pose(4)
    Start-Sleep -Milliseconds 1200
    [Visual]::Shot((Join-Path $Out "5-demo.png"), 900, 600)
}
finally {
    Start-Process $exe -ArgumentList "--quit" -Wait
    [Visual]::StopBackdrop()
}
$report
"Screenshots: $Out"
