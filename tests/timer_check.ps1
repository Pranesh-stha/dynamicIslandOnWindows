# Hands-free check of the timer, stopwatch and charging moment (PLAN.md
# Phase 8). It never moves your mouse or takes focus: Island is started with
# tests-only options (--timer, --stopwatch, --charging-moment) and posed.
#
#   pwsh -File tests\timer_check.ps1 [-Config Release] [-Out <folder>]

param([string]$Config = "Release", [string]$Out = "")

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\$Config\Island.exe"
if (-not $Out) { $Out = Join-Path $env:TEMP "island-timer-check" }
New-Item -ItemType Directory -Force $Out | Out-Null

Add-Type -AssemblyName System.Drawing
$drawing = [AppDomain]::CurrentDomain.GetAssemblies() | Where-Object { $_.Location -and $_.GetName().Name -match '^System\.(Drawing|Private\.Windows)' } | ForEach-Object Location
Add-Type -Path (Join-Path $PSScriptRoot "TestHelpers.cs") -ReferencedAssemblies ($drawing + @("System.Threading", "System.Threading.Thread"))

$results = [System.Collections.Generic.List[string]]::new()
function Check([string]$name, [bool]$ok, [string]$detail) { $results.Add(("{0}  {1}  ({2})" -f $(if ($ok) { "PASS" } else { "FAIL" }), $name, $detail)) }
function Height() { $r = [Visual]::IslandRect(); $r.Bottom - $r.Top }
function Start-Island([string]$arguments) {
    & $exe --quit
    Start-Process $exe -ArgumentList "--glass-snapshot $arguments"
}

$screen = [Visual]::GetSystemMetrics(0)
[Visual]::Width = $screen; [Visual]::Height = 560
[Visual]::StartBackdrop()
try {
    # A 1-minute timer: the pill comes down by itself and stays.
    Start-Island "--timer 1"
    Start-Sleep -Milliseconds 1800
    [Visual]::Shot((Join-Path $Out "1-timer-pill.png"), 700, 110)
    Start-Sleep -Milliseconds 1100
    [Visual]::Shot((Join-Path $Out "2-timer-pill-1s-later.png"), 700, 110)
    $h = Height
    Check "running timer keeps the pill down" ($h -gt 20) "window $h px tall"
    [Visual]::Pose(2)
    Start-Sleep -Milliseconds 800
    [Visual]::Shot((Join-Path $Out "3-timer-peek.png"), 700, 110)
    [Visual]::Pose(3)
    Start-Sleep -Milliseconds 1000
    [Visual]::Shot((Join-Path $Out "4-timer-panel.png"), $screen, 560)
    [Visual]::Pose(0)   # let go: with the timer running it goes back to the pill, not out of sight
    Start-Sleep -Milliseconds 2200
    $h = Height
    Check "after the panel closes, the pill stays while the timer runs" ($h -gt 20) "window $h px tall"

    # The stopwatch.
    Start-Island "--stopwatch"
    Start-Sleep -Milliseconds 3300
    [Visual]::Shot((Join-Path $Out "5-stopwatch-pill.png"), 700, 110)

    # The charging moment: down for ~3 s, then tucked away again.
    Start-Island "--charging-moment"
    Start-Sleep -Milliseconds 1500
    Check "tucked away before the moment" ((Height) -le 3) "window $(Height) px tall"
    Start-Sleep -Milliseconds 700
    [Visual]::Shot((Join-Path $Out "6-charging.png"), 700, 110)
    Start-Sleep -Milliseconds 900
    [Visual]::Shot((Join-Path $Out "7-charging-filled.png"), 700, 110)
    Check "charging moment brings the pill down" ((Height) -gt 20) "window $(Height) px tall"
    Start-Sleep -Milliseconds 3500
    Check "tucked away again after it" ((Height) -le 3) "window $(Height) px tall"
}
finally {
    Start-Process $exe -ArgumentList "--quit" -Wait
    [Visual]::StopBackdrop()
}
$results
"Screenshots: $Out"
