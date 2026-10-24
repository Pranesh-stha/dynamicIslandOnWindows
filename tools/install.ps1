# Installs Island for your user account (no admin rights needed):
#   - copies Island.exe to %LOCALAPPDATA%\Programs\Island
#   - copies settings.json there the first time (later installs keep your edits)
#   - adds "Island" to the Start menu, so you can search for it and open it
#   - starts it
# "Start with Windows" is a tick-box in Island's tray menu (off until you turn it on).
#
#   pwsh -File tools\install.ps1            (uses the Release build)
#   pwsh -File tools\install.ps1 -Build     (builds it first)
#
# Remove it again with tools\uninstall.ps1.

param([switch]$Build)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$built = Join-Path $root "build\Release\Island.exe"
$target = Join-Path $env:LOCALAPPDATA "Programs\Island"
$startMenu = Join-Path ([Environment]::GetFolderPath("Programs")) "Island.lnk"

if ($Build) {
    Push-Location $root
    try {
        cmake --preset default | Out-Null
        cmake --build --preset release
        if ($LASTEXITCODE -ne 0) { throw "The build failed." }
    }
    finally { Pop-Location }
}
if (-not (Test-Path $built)) { throw "No Release build yet: run with -Build, or build with 'cmake --build --preset release'." }

# A running Island (installed or from the build folder) closes first.
& $built --quit
Start-Sleep -Milliseconds 500

New-Item -ItemType Directory -Force $target | Out-Null
Copy-Item $built (Join-Path $target "Island.exe") -Force
$settings = Join-Path $target "settings.json"
if (-not (Test-Path $settings)) {
    Copy-Item (Join-Path $root "config\settings.json") $settings
    "settings: $settings (new)"
} else {
    "settings: $settings (kept yours)"
}

# The Start menu entry: a normal shortcut, with Island's icon.
$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut($startMenu)
$link.TargetPath = Join-Path $target "Island.exe"
$link.WorkingDirectory = $target
$link.Description = "Island: a Dynamic Island for Windows"
$link.Save()
"Start menu: $startMenu"

# If "Start with Windows" is on, it already points at the installed copy.
Start-Process (Join-Path $target "Island.exe")
"Installed and started: $target\Island.exe"
"Open it any time from the Start menu (search 'Island'). Quit it from its tray icon (^ next to the clock)."
