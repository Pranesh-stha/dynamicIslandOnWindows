# Soak test for Island (PLAN.md Phase 7). Hands-free: it never moves your
# mouse or takes focus. It makes the island appear and tuck away again and
# again (a mouse-move message posted to the island, as if the mouse touched
# the top edge), which starts and stops the clear glass's screen capture
# each time, and checks that memory and handles stay flat.
#
#   pwsh -File tests\soak_test.ps1 [-Cycles 60] [-Config Release]
#
# For the long (48 h) soak, just leave Island running and use
#   pwsh -File tests\soak_test.ps1 -Watch -Hours 48
# which only samples memory and handles every 10 minutes into a CSV.

param([int]$Cycles = 60, [string]$Config = "Release", [switch]$Watch, [double]$Hours = 48)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\$Config\Island.exe"

function Sample($p) {
    $p.Refresh()
    $ws = (Get-Counter "\Process(Island)\Working Set - Private" -ErrorAction SilentlyContinue).CounterSamples[0].CookedValue
    [pscustomobject]@{ Time = Get-Date -Format "yyyy-MM-dd HH:mm:ss"; MemoryMB = [math]::Round($ws / 1MB, 1); Handles = $p.HandleCount; Threads = $p.Threads.Count; CpuSeconds = [math]::Round($p.TotalProcessorTime.TotalSeconds, 2) }
}

if ($Watch) {
    $p = Get-Process Island -ErrorAction Stop
    $csv = Join-Path $env:LOCALAPPDATA "Island\soak.csv"
    "Sampling Island every 10 minutes for $Hours h into $csv (Ctrl+C to stop)"
    $end = (Get-Date).AddHours($Hours)
    while ((Get-Date) -lt $end) {
        Sample $p | Export-Csv $csv -Append -NoTypeInformation
        Start-Sleep -Seconds 600
    }
    return
}

Add-Type -AssemblyName System.Drawing
$drawing = [AppDomain]::CurrentDomain.GetAssemblies() | Where-Object { $_.Location -and $_.GetName().Name -match '^System\.(Drawing|Private\.Windows)' } | ForEach-Object Location
Add-Type -Path (Join-Path $PSScriptRoot "TestHelpers.cs") -ReferencedAssemblies ($drawing + @("System.Threading", "System.Threading.Thread"))

$started = Get-Date
& $exe --quit
Start-Process $exe
Start-Sleep -Seconds 3
$p = Get-Process Island
$before = Sample $p
"before: $($before.MemoryMB) MB, $($before.Handles) handles, $($before.Threads) threads"
for ($i = 1; $i -le $Cycles; $i++) {
    [Visual]::Touch()        # comes down (capture starts) ...
    Start-Sleep -Milliseconds 900   # ... and tucks away 300 ms after the "mouse" leaves (capture stops)
    if ($i % 20 -eq 0) { $s = Sample $p; "after $i cycles: $($s.MemoryMB) MB, $($s.Handles) handles" }
}
Start-Sleep -Seconds 5
$after = Sample $p
"after:  $($after.MemoryMB) MB, $($after.Handles) handles, $($after.Threads) threads"
$crashes = @(Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Id = 1000; StartTime = $started } -ErrorAction SilentlyContinue | Where-Object { $_.Message -match 'Island|dwm' })
"crashes (Island or dwm.exe) during the test: $($crashes.Count)"
$grew = $after.Handles - $before.Handles
if ($crashes.Count -eq 0 -and $grew -lt 40 -and $after.MemoryMB - $before.MemoryMB -lt 15) { "PASS" } else { "CHECK: handles +$grew, memory +$([math]::Round($after.MemoryMB - $before.MemoryMB, 1)) MB" }
