# Now-playing test for Island (PLAN.md Phase 4). Hands-free and silent.
#
# A test player (in Windows PowerShell 5.1, which can use Windows' media
# classes directly) plays a silent file at volume 0 with a test title, artist
# and artwork. Windows lists it as a media session like any other player.
# Island should switch to it within a second (a playing session beats a
# paused one), show its details, follow pause, and switch back when it ends.
# Your own music is never touched.
#
#   pwsh -File tests\media_test.ps1 [-Config Debug] [-Out <folder>]

param([string]$Config = "Release", [string]$Out = "")

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\$Config\Island.exe"
if (-not (Test-Path $exe)) { throw "Build first: $exe not found" }
if (-not $Out) { $Out = Join-Path $env:TEMP "island-media-test" }
New-Item -ItemType Directory -Force $Out | Out-Null
$log = Join-Path $env:LOCALAPPDATA "Island\island.log"

# 1. Test files: 30 s of silence, and a 400x300 artwork (not square, to
#    check that the middle is cut out).
$wav = Join-Path $Out "silence.wav"
$rate = 8000; $samples = $rate * 30
$stream = [System.IO.File]::Create($wav); $w = New-Object System.IO.BinaryWriter $stream
$w.Write([Text.Encoding]::ASCII.GetBytes("RIFF")); $w.Write([int](36 + $samples * 2)); $w.Write([Text.Encoding]::ASCII.GetBytes("WAVEfmt "))
$w.Write([int]16); $w.Write([int16]1); $w.Write([int16]1); $w.Write([int]$rate); $w.Write([int]($rate * 2)); $w.Write([int16]2); $w.Write([int16]16)
$w.Write([Text.Encoding]::ASCII.GetBytes("data")); $w.Write([int]($samples * 2)); $w.Write((New-Object byte[] ($samples * 2))); $w.Close()

Add-Type -AssemblyName System.Drawing
$art = Join-Path $Out "artwork.png"
$bmp = New-Object System.Drawing.Bitmap 400, 300
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::DarkSlateBlue)
$g.FillEllipse([System.Drawing.Brushes]::Orange, 110, 60, 180, 180)
$g.FillRectangle([System.Drawing.Brushes]::Red, 0, 0, 50, 300)  # cut off if the middle square is taken
$g.Dispose(); $bmp.Save($art, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()

# 2. The test player: plays for 8 s, pauses for 5 s, then ends.
$player = Join-Path $Out "player.ps1"
@"
`$null = [Windows.Media.Playback.MediaPlayer, Windows.Media, ContentType = WindowsRuntime]
`$null = [Windows.Media.Core.MediaSource, Windows.Media, ContentType = WindowsRuntime]
`$null = [Windows.Storage.Streams.RandomAccessStreamReference, Windows.Storage.Streams, ContentType = WindowsRuntime]
`$source = [Windows.Media.Core.MediaSource]::CreateFromUri([Uri]'$wav')
`$item = New-Object Windows.Media.Playback.MediaPlaybackItem `$source
`$props = `$item.GetDisplayProperties()
`$props.Type = [Windows.Media.MediaPlaybackType]::Music
`$props.MusicProperties.Title = 'Island test track'
`$props.MusicProperties.Artist = 'Island test artist'
`$props.Thumbnail = [Windows.Storage.Streams.RandomAccessStreamReference]::CreateFromUri([Uri]'$art')
`$item.ApplyDisplayProperties(`$props)
`$player = New-Object Windows.Media.Playback.MediaPlayer
`$player.Volume = 0
`$player.Source = `$item
`$player.Play()
Start-Sleep -Seconds 8
`$player.Pause()
Start-Sleep -Seconds 5
`$player.Dispose()
"@ | Set-Content $player -Encoding UTF8

# 3. Test helpers: the backdrop (so screenshots never include your own
#    windows), poses and screenshots.
$drawing = [AppDomain]::CurrentDomain.GetAssemblies() | Where-Object { $_.Location -and $_.GetName().Name -match '^System\.(Drawing|Private\.Windows)' } | ForEach-Object Location
Add-Type -Path (Join-Path $PSScriptRoot "TestHelpers.cs") -ReferencedAssemblies ($drawing + @("System.Threading", "System.Threading.Thread"))
function Shot([string]$name, [int]$w, [int]$h) { [Visual]::Shot((Join-Path $Out $name), $w, $h) }
function MediaLines { Get-Content $log | Select-String "media: " | ForEach-Object Line }

$results = [System.Collections.Generic.List[object]]::new()
function Check([string]$name, [bool]$ok, [string]$detail) {
    $results.Add([pscustomobject]@{ Test = $name; Result = $(if ($ok) { "PASS" } else { "FAIL" }); Detail = $detail })
}

[Visual]::StartBackdrop()
Start-Process $exe -ArgumentList "--pose peek --glass-snapshot"  # visible to screenshots (see visual_check.ps1)
Start-Sleep -Seconds 2
$before = MediaLines
try {
    $started = Get-Date
    $test = Start-Process powershell.exe -ArgumentList "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $player -WindowStyle Hidden -PassThru

    # It should switch to the test session within a second of it playing.
    $switched = $null
    for ($i = 0; $i -lt 60 -and -not $switched; $i++) {
        Start-Sleep -Milliseconds 100
        $switched = MediaLines | Where-Object { $_ -match "showing Powershell" } | Select-Object -First 1
    }
    $switchMs = [int]((Get-Date) - $started).TotalMilliseconds
    Check "switches to the playing test session" ([bool]$switched) "seen in the log after ~$switchMs ms (includes starting the player)"

    Start-Sleep -Milliseconds 1500
    Shot "1-peek-playing.png" 700 110
    [Visual]::Pose(3); Start-Sleep -Milliseconds 1200
    Shot "2-expanded-playing.png" 900 260

    # After 8 s the test player pauses; the island must follow.
    Start-Sleep -Seconds 6
    [Visual]::Pose(2); Start-Sleep -Milliseconds 1000
    Shot "3-peek-paused.png" 700 110

    # When it ends, the island goes back to another app's session.
    $test.WaitForExit(15000) | Out-Null
    Start-Sleep -Seconds 2.5
    $after = MediaLines | Select-Object -Last 1
    Check "switches back when the test player ends" ($after -notmatch "Powershell") "last: $after"
}
finally {
    if ($test -and -not $test.HasExited) { $test.Kill() }
    Start-Process $exe -ArgumentList "--quit" -Wait
    [Visual]::StopBackdrop()
}
$results | Format-Table -AutoSize | Out-String -Width 200
"Media log:"; MediaLines
"Screenshots: $Out (check 1-peek-playing: title, pause icon, orange-circle artwork without the red strip)"
