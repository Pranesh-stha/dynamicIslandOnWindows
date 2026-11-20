# The island on every screen (PLAN.md Phase 9). Hands-free: sensors are
# "touched" with mouse-move messages posted to them, so your mouse is never
# moved and nothing is clicked. Needs two or more screens; skipped with one.
# The cases with fullscreen windows (a sensor off over a game) are in
# fullscreen_test.ps1, which clicks its own test windows.
#
#   pwsh -File tests\monitor_test.ps1 [-Config Release]

param([string]$Config = "Release")

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\$Config\Island.exe"
if (-not (Test-Path $exe)) { throw "Build first: $exe not found" }
$log = Join-Path $env:LOCALAPPDATA "Island\island.log"

Add-Type -AssemblyName System.Drawing
$null = [System.Drawing.Bitmap], [System.Drawing.Graphics]  # PowerShell 7.6+ loads the assemblies that hold them only when first used
$drawing = @([AppDomain]::CurrentDomain.GetAssemblies() | Where-Object { $_.Location -and $_.GetName().Name -match '^System\.(Drawing|Private\.Windows)' } | ForEach-Object Location)
Add-Type -Path (Join-Path $PSScriptRoot "TestHelpers.cs") -ReferencedAssemblies ($drawing + @("System.Threading", "System.Threading.Thread"))

$screens = [Visual]::Screens()
if ($screens.Count -lt 2) { "Skipped: only one screen connected."; return }
$main = $screens[0]; $other = $screens[1]
function Name($s) { "{0}x{1} at ({2}, {3})" -f ($s.Right - $s.Left), ($s.Bottom - $s.Top), $s.Left, $s.Top }

$results = [System.Collections.Generic.List[object]]::new()
function Check([string]$name, [bool]$ok, [string]$detail) {
    $results.Add([pscustomobject]@{ Test = $name; Result = $(if ($ok) { "PASS" } else { "FAIL" }); Detail = $detail })
}
function IslandRect { [Visual]::IslandRect() }
function IslandOn($screen) { [Visual]::OnScreen((IslandRect), $screen) }
function Height { $r = IslandRect; $r.Bottom - $r.Top }
function SensorVisible($screen) { $s = [Visual]::SensorOn($screen); $s -ne [IntPtr]::Zero -and [Visual]::IsWindowVisible($s) }
# Touch a screen's sensor; returns ms until the island's window is on that screen and revealed (-1: never).
function TouchAndWait($screen, [int]$timeoutMs = 600) {
    $sensor = [Visual]::SensorOn($screen)
    if ($sensor -eq [IntPtr]::Zero -or -not [Visual]::IsWindowVisible($sensor)) { return -1 }
    $watch = [Diagnostics.Stopwatch]::StartNew()
    [Visual]::TouchSensor($sensor)
    while ($watch.ElapsedMilliseconds -lt $timeoutMs) {
        if ((IslandOn $screen) -and (Height) -gt 3) { return [int]$watch.ElapsedMilliseconds }
        Start-Sleep -Milliseconds 5
    }
    return -1
}
function Start-Island([string]$arguments, [string]$path = $exe) {
    Start-Process $path -ArgumentList "--quit" -Wait; Start-Sleep -Milliseconds 300
    if ($arguments) { Start-Process $path -ArgumentList $arguments } else { Start-Process $path }
    Start-Sleep -Seconds 2
}

$started = Get-Date
try {
    Start-Island ""
    $focus = [Visual]::GetForegroundWindow()

    # M1: the island starts on the main screen; a sensor waits at the top-middle of the other one.
    Check "M1 starts on the main screen" (IslandOn $main) "island at $((IslandRect).Left),$((IslandRect).Top)"
    $sensor = [Visual]::SensorOn($other)
    $r = [Visual]::RectOf($sensor)
    $centred = [Math]::Abs((($r.Left + $r.Right) / 2) - (($other.Left + $other.Right) / 2)) -le 1
    Check "M1 a sensor on the other screen" ((SensorVisible $other) -and $r.Top -eq $other.Top -and ($r.Bottom - $r.Top) -le 3 -and $centred) "$($r.Right - $r.Left)x$($r.Bottom - $r.Top) at ($($r.Left), $($r.Top))"
    Check "M1 no sensor on the island's own screen" (-not (SensorVisible $main)) ""

    # M7: the mouse at the top-middle of the other screen reaches its sensor.
    Check "M7 the other screen's sensor is reachable" ([Visual]::ReachesAtTop($sensor, $other)) ""

    # Something fullscreen on the main screen (e.g. a game you're playing)
    # keeps its sensor off, as it should: then the way back can't be tested.
    $mainCovered = [bool](Get-Content $log | Select-String "main screen: .* -> HIDE" | Select-Object -Last 1)
    if ($mainCovered) { "Note: something is fullscreen on the main screen, so there's no sensor there; M3 and M4 are skipped." }

    # M2: touch it: the island comes to that screen and down.
    $ms = TouchAndWait $other
    Check "M2 touching it brings the island there" ($ms -ge 0) "revealed on $(Name $other) after $ms ms"
    Start-Sleep -Milliseconds 400
    Check "M2 now a sensor on the main screen, none on this one" (($mainCovered -or (SensorVisible $main)) -and -not (SensorVisible $other)) $(if ($mainCovered) { "main screen covered: its sensor stays off" } else { "" })
    Check "M2 focus not taken" ([Visual]::GetForegroundWindow() -eq $focus) ""
    Start-Sleep -Milliseconds 800
    Check "M2 tucks away there, stays there" ((IslandOn $other) -and (Height) -le 3) "window $(Height) px tall"

    # M3: and back.
    if (-not $mainCovered) {
        $ms = TouchAndWait $main
        Check "M3 back to the main screen" ($ms -ge 0) "revealed after $ms ms"
        Start-Sleep -Milliseconds 1200
    }

    # M10: how long each move took (Island's own measure, from the log).
    $moves = @(Get-Content $log | Select-String "island moved here in (\d+) ms" | ForEach-Object { [int]$_.Matches[0].Groups[1].Value })
    Check "M10 a move takes < 150 ms" ($moves.Count -ge 1 -and ($moves | Measure-Object -Maximum).Maximum -lt 150) ("moves: " + ($moves -join ", ") + " ms")

    # M4: back and forth 2 x 20 times quickly: ends where touched last; memory
    # levels off and handles stay flat. (Each move redraws everything at the
    # other screen's scale; Windows' surface pool grows once, about 15 MB of
    # graphics memory, to fit the old drawings, kept 3-6 s (D11), next to the
    # new ones. The second round must add nothing to the first.)
    if ($mainCovered) { Check "M4 20 switches (skipped: main screen covered)" $true "" } else {
    function Mem { (Get-Counter "\Process(Island)\Working Set - Private").CounterSamples[0].CookedValue / 1MB }
    $p = Get-Process Island
    $memStart = Mem; $handlesBefore = $p.HandleCount
    $memRound = @()
    foreach ($round in 1..2) {
        foreach ($i in 1..20) {
            $target = if ($i % 2) { $other } else { $main }
            $null = TouchAndWait $target 400
            Start-Sleep -Milliseconds 80
        }
        Start-Sleep -Seconds 8  # the old layers are released (D11)
        $memRound += Mem
    }
    $p.Refresh()
    Check "M4 40 switches: ends where touched last" (IslandOn $main) ""
    Check "M4 memory levels off, handles flat" (($memRound[1] - $memRound[0]) -lt 8 -and $memRound[1] -lt 100 -and ($p.HandleCount - $handlesBefore) -lt 30) ("{0:N1} -> {1:N1} -> {2:N1} MB, {3} -> {4} handles" -f $memStart, $memRound[0], $memRound[1], $handlesBefore, $p.HandleCount)
    }

    # M9: a running timer's pill comes along and stays down there.
    Start-Island "--timer 25"
    $ms = TouchAndWait $other
    Start-Sleep -Milliseconds 1500
    Check "M9 a running timer's pill follows" ($ms -ge 0 -and (IslandOn $other) -and (Height) -gt 3) "window $(Height) px tall on $(Name $other)"

    # M8: followMouse off: no sensors, the main screen only (a copy with its own settings.json).
    $copy = Join-Path $env:TEMP "island-monitor-test"
    New-Item -ItemType Directory -Force $copy | Out-Null
    Copy-Item $exe (Join-Path $copy "Island.exe") -Force
    (Get-Content (Join-Path $root "config\settings.json") -Raw) -replace '"followMouse": true', '"followMouse": false' | Set-Content (Join-Path $copy "settings.json")
    Start-Island "" (Join-Path $copy "Island.exe")
    Check "M8 followMouse false: no sensors" (-not (SensorVisible $other) -and -not (SensorVisible $main) -and (IslandOn $main)) ""
    Start-Process (Join-Path $copy "Island.exe") -ArgumentList "--quit" -Wait

    # M11: clear glass copies the screen the island is on (not the one it started on).
    if (-not $mainCovered) {
        (Get-Content (Join-Path $root "config\settings.json") -Raw) -replace '"style": "\w+"', '"style": "clear"' | Set-Content (Join-Path $copy "settings.json")
        Start-Island "" (Join-Path $copy "Island.exe")
        $null = TouchAndWait $other; Start-Sleep -Milliseconds 1500
        $null = TouchAndWait $main; Start-Sleep -Milliseconds 1500
        $sizes = @(Get-Content $log | Select-String "glass: first frame .* \(screen (\d+x\d+)\)" | ForEach-Object { $_.Matches[0].Groups[1].Value })
        $want = @("{0}x{1}" -f ($other.Right - $other.Left), ($other.Bottom - $other.Top); "{0}x{1}" -f ($main.Right - $main.Left), ($main.Bottom - $main.Top))
        Check "M11 clear glass copies the screen it's on" ($sizes.Count -ge 2 -and $sizes[-2] -eq $want[0] -and $sizes[-1] -eq $want[1]) ("copied: " + ($sizes -join ", "))
        Start-Process (Join-Path $copy "Island.exe") -ArgumentList "--quit" -Wait
    }
}
finally {
    Start-Process $exe -ArgumentList "--quit" -Wait
    $installed = Join-Path $env:LOCALAPPDATA "Programs\Island\Island.exe"
    if (Test-Path $installed) { Start-Process $installed }  # your Island, back to normal
}

$crashes = @(Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Id = 1000; StartTime = $started } -ErrorAction SilentlyContinue | Where-Object { $_.Message -match 'Island|dwm' })
Check "no crashes (Island or dwm.exe)" ($crashes.Count -eq 0) "$($crashes.Count)"
$results | Format-Table -AutoSize | Out-String -Width 200
"Screens: main $(Name $main), other $(Name $other)"
if ($results.Result -contains "FAIL") { exit 1 }
