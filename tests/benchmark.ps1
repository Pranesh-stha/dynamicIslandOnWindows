# Resource benchmark (PLAN.md §10, final test report). Hands-free: it only
# starts Island with test poses and posts pose messages to Island's own window.
# Every state is measured over a fixed background, and DWM (the compositor,
# which draws the whole screen) is measured without Island too, so the
# difference is Island's real cost. GPU % is the 3D engine of the GPU Island
# draws on. Island is restarted normally at the end. Takes about 5 minutes.
#
#   pwsh -File tests\benchmark.ps1 [-Exe <path>] [-Seconds 15] [-IdleSeconds 60]
#
# Default exe: the installed copy (what you run every day).

param([string]$Exe = (Join-Path $env:LOCALAPPDATA "Programs\Island\Island.exe"), [int]$Seconds = 15, [int]$IdleSeconds = 60)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not (Test-Path $Exe)) { throw "Not found: $Exe (install with tools\install.ps1, or pass -Exe)" }
$log = Join-Path $env:LOCALAPPDATA "Island\island.log"
Add-Type -AssemblyName System.Drawing
$drawing = [AppDomain]::CurrentDomain.GetAssemblies() | Where-Object { $_.Location -and $_.GetName().Name -match '^System\.(Drawing|Private\.Windows)' } | ForEach-Object Location
Add-Type -Path (Join-Path $PSScriptRoot "TestHelpers.cs") -ReferencedAssemblies ($drawing + @("System.Threading", "System.Threading.Thread"))
$screen = [Visual]::GetSystemMetrics(0)
$rows = [System.Collections.Generic.List[object]]::new()
$startups = [System.Collections.Generic.List[double]]::new()

function Quit-Island { if (Get-Process Island -ErrorAction SilentlyContinue) { Start-Process $Exe -ArgumentList "--quit" -Wait; Start-Sleep -Milliseconds 500 } }
function Start-Island([string]$arguments) {
    Quit-Island
    $p = if ($arguments) { Start-Process $Exe -ArgumentList $arguments -PassThru } else { Start-Process $Exe -PassThru }
    Start-Sleep -Seconds 4
    # Startup: process start -> the island's window first shown (from the log).
    $shown = Get-Content $log | Select-String "^(\d\d:\d\d:\d\d\.\d{3})  island shown" | Select-Object -First 1
    if ($shown) {
        $t = [datetime]::ParseExact($p.StartTime.ToString("yyyy-MM-dd") + " " + $shown.Matches[0].Groups[1].Value, "yyyy-MM-dd HH:mm:ss.fff", $null)
        $startups.Add(($t - $p.StartTime).TotalMilliseconds)
    }
}
function Gpu3d($samples, [int]$id) {
    if ($id -le 0) { return 0 }
    (($samples | ForEach-Object { ($_.CounterSamples | Where-Object { $_.InstanceName.StartsWith("pid_$($id)_") -and $_.InstanceName.EndsWith('engtype_3d') } | Measure-Object CookedValue -Sum).Sum }) | Measure-Object -Average).Average
}
function Measure-State([string]$label, [int]$seconds, [scriptblock]$during) {
    $island = Get-Process Island -ErrorAction SilentlyContinue
    $dwm = (Get-Process dwm).Id
    $job = if ($during) { Start-ThreadJob -ScriptBlock $during -ArgumentList $seconds } else { $null }
    $c0 = if ($island) { $island.TotalProcessorTime.TotalMilliseconds } else { 0 }
    $samples = Get-Counter "\GPU Engine(*)\Utilization Percentage" -SampleInterval 1 -MaxSamples $seconds -ErrorAction SilentlyContinue
    if ($job) { $job | Wait-Job | Receive-Job; $job | Remove-Job }
    $row = [ordered]@{ State = $label; "Island CPU %" = 0.0; "Island GPU %" = 0.0; "DWM GPU %" = [math]::Round((Gpu3d $samples $dwm), 2); "RAM MB" = 0.0; "GPU mem MB" = 0.0; Handles = 0; Threads = 0 }
    if ($island) {
        $island.Refresh()
        $row["Island CPU %"] = [math]::Round(($island.TotalProcessorTime.TotalMilliseconds - $c0) / ($seconds * 10), 3)
        $row["Island GPU %"] = [math]::Round((Gpu3d $samples $island.Id), 2)
        $row["RAM MB"] = [math]::Round((Get-Counter "\Process(Island)\Working Set - Private").CounterSamples[0].CookedValue / 1MB, 1)
        $mem = (Get-Counter "\GPU Process Memory(*)\Dedicated Usage", "\GPU Process Memory(*)\Shared Usage" -ErrorAction SilentlyContinue).CounterSamples | Where-Object { $_.InstanceName.StartsWith("pid_$($island.Id)_") }
        $row["GPU mem MB"] = [math]::Round((($mem | Measure-Object CookedValue -Sum).Sum) / 1MB, 1)
        $row.Handles = $island.HandleCount; $row.Threads = $island.Threads.Count
    }
    $rows.Add([pscustomobject]$row)
    $rows[-1] | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
}

# Morphs back and forth all the time: pill -> panel -> Peek -> panel ...
# ([NullString]::Value: PowerShell would pass $null as "", a window title.)
$morphing = {
    param($seconds)
    Add-Type -Name W -Namespace M -MemberDefinition '[DllImport("user32")] public static extern System.IntPtr FindWindow(string c, string n); [DllImport("user32")] public static extern uint RegisterWindowMessage(string s); [DllImport("user32")] public static extern bool PostMessage(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);'
    $msg = [M.W]::RegisterWindowMessage("Island.Pose"); $end = (Get-Date).AddSeconds($seconds); $poses = 1, 3, 2, 3
    for ($i = 0; (Get-Date) -lt $end; $i++) { [void][M.W]::PostMessage([M.W]::FindWindow("Island.Pill", [NullString]::Value), $msg, [IntPtr]$poses[$i % 4], [IntPtr]::Zero); Start-Sleep -Milliseconds 700 }
}

$crashesFrom = Get-Date
[Visual]::Width = $screen; [Visual]::Height = 560
[Visual]::StartBackdrop()
try {
    Quit-Island
    Measure-State "no Island (still background)" $Seconds $null
    Start-Island ""
    Measure-State "tucked away, idle ($IdleSeconds s)" $IdleSeconds $null
    Start-Island "--pose idle"
    Measure-State "pill showing" $Seconds $null
    Start-Island "--pose peek"
    Measure-State "Peek (hover)" $Seconds $null
    Start-Island "--pose expanded"
    Measure-State "control center open" $Seconds $null
    Start-Island "--timer 25"
    Measure-State "timer running (pill ticking)" $Seconds $null
    Start-Island "--pose idle"
    Measure-State "morphing nonstop" $Seconds $morphing
}
finally { [Visual]::StopBackdrop() }

# Motion behind: a pattern moving 60 times a second (like a video). It takes a
# few seconds to start (it compiles its window first).
Quit-Island
$moving = Start-Process pwsh -ArgumentList "-NoProfile", "-File", (Join-Path $PSScriptRoot "moving_backdrop.ps1"), "-Seconds", "130" -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 6
try {
    Measure-State "no Island (video-like motion)" $Seconds $null
    Start-Island "--pose idle"
    Measure-State "pill over motion" $Seconds $null
    Start-Island "--pose expanded"
    Measure-State "control center over motion" $Seconds $null
}
finally { Stop-Process -Id $moving.Id -ErrorAction SilentlyContinue }

$peak = (Get-Process Island).PeakWorkingSet64
Quit-Island
Start-Process $Exe   # back to normal
$csv = Join-Path $env:TEMP "island-benchmark.csv"
$rows | Export-Csv $csv -NoTypeInformation
"`n=== Summary === (Island CPU: % of one core; RAM: private working set = Task Manager's Memory column; also in $csv)"
$rows | Format-Table -AutoSize | Out-String -Width 220
"startup (process start -> island shown): " + (($startups | ForEach-Object { "{0:N0}" -f $_ }) -join ", ") + " ms"
"peak working set of the last run (incl. shared Windows DLLs): {0:N1} MB" -f ($peak / 1MB)
$crashes = @(Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Id = 1000; StartTime = $crashesFrom } -ErrorAction SilentlyContinue | Where-Object { $_.Message -match 'Island|dwm' })
"crashes (Island or dwm.exe) during the benchmark: $($crashes.Count)"
