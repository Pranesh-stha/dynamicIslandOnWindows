# Removes what tools\install.ps1 added: the installed Island, its Start menu
# entry and, if you turned it on, its "Start with Windows" shortcut. Your log
# (%LOCALAPPDATA%\Island) stays unless you add -RemoveLog.
#
#   pwsh -File tools\uninstall.ps1 [-RemoveLog]

param([switch]$RemoveLog)

$ErrorActionPreference = "Stop"
$target = Join-Path $env:LOCALAPPDATA "Programs\Island"
$exe = Join-Path $target "Island.exe"

if (Test-Path $exe) { & $exe --quit; Start-Sleep -Milliseconds 500 }
foreach ($link in (Join-Path ([Environment]::GetFolderPath("Programs")) "Island.lnk"),
                  (Join-Path ([Environment]::GetFolderPath("Startup")) "Island.lnk")) {
    if (Test-Path $link) { Remove-Item $link; "removed $link" }
}
if (Test-Path $target) { Remove-Item $target -Recurse; "removed $target" }
if ($RemoveLog) {
    $log = Join-Path $env:LOCALAPPDATA "Island"
    if (Test-Path $log) { Remove-Item $log -Recurse; "removed $log" }
}
"Island is uninstalled."
