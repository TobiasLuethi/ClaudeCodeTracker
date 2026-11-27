# Entfernt das Claude Touch Gadget von diesem PC: Autostart, Brücke, Mod und den Eintrag in settings.json.
# Die Kopplung (~/.claude/touch-dashboard/gadgets.json) bleibt nur erhalten, wenn -KeepPairing gesetzt ist.
param(
  [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'ClaudeTouchGadget'),
  [string]$ClaudeHome = (Join-Path $env:USERPROFILE '.claude'),
  [switch]$KeepPairing
)
$ErrorActionPreference = 'Stop'
Write-Host 'Claude Touch Gadget - Deinstallation' -ForegroundColor White

& (Join-Path $PSScriptRoot 'stop-bridge.ps1') -InstallDir $InstallDir

$link = Join-Path ([Environment]::GetFolderPath('Startup')) 'Claude Touch Gadget.lnk'
if (Test-Path $link) { Remove-Item $link -Force; Write-Host '   Autostart entfernt' }

$mod = Join-Path $InstallDir 'mod\touch-dashboard'
$helper = Join-Path $InstallDir 'bridge\settings-helper.mjs'
$settings = Join-Path $ClaudeHome 'settings.json'
$node = Get-Command node -ErrorAction SilentlyContinue
if ($node -and (Test-Path $helper) -and (Test-Path $settings)) {
  & $node.Source $helper remove $settings $mod
}

if (Test-Path $InstallDir) { Remove-Item $InstallDir -Recurse -Force; Write-Host "   $InstallDir entfernt" }

$pairing = Join-Path $ClaudeHome 'touch-dashboard\gadgets.json'
if (-not $KeepPairing -and (Test-Path $pairing)) { Remove-Item $pairing -Force; Write-Host '   Kopplung entfernt' }

Write-Host "`nFertig. Claude einmal neu starten, damit der Mod nicht mehr geladen wird." -ForegroundColor Green
